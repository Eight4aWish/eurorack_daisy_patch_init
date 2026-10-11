#pragma once
// chaos_core::Voice — one chaotic oscillator, rendered to a block of samples.
//
// Everything here is platform-free: an attractor, the oversampling schedule that
// turns a requested pitch into integration steps, a gate-driven AD/SR envelope
// acting as a VCA, DC blocking and the output soft-limiter. It knows nothing
// about audio libraries, interrupts or sample formats — a host binds it by
// calling setSampleRate() once, setParams() from its control loop, and render()
// from its audio callback.
//
// The split is deliberate. What a platform layer still owns is only the parts
// that genuinely are platform: the audio callback and its buffers, whatever
// measures block cost (see setLoadScale), the critical section around
// setParams(), and the conversion from the float samples render() produces to
// whatever the codec wants.
//
// Sample rate is a parameter, not an assumption. Pitch, envelope times and the
// DC blocker's corner are all specified in real-world units and converted here,
// so the same voice runs at 44.1 kHz or 96 kHz without retuning.
//
// Two ways to drive it. setParams() takes a simulated-time rate, as the Teensy
// build did, and is bit-identical to it. setPitch() takes a note in Hz and a
// TAME amount (docs/SECRET.md, section 2):
//
//   - The rate is scaled so the attractor's measured natural frequency lands on
//     the note (ChaosBase::naturalFreq). TAME = 0 is just that: the free voice,
//     in tune as far as the system has a pitch to tune.
//   - TAME is delayed feedback control (Pyragas 1992). It adds
//     K (v(t - T) - v(t)) to one equation: v a state variable (X unless the model
//     names another, ChaosBase::tameVar), T one period of the note, and
//     K = ChaosBase::tameGain x TAME^2 x 2 pi fNat. On a periodic orbit of the
//     equations with period T the term is zero, so what TAME holds the voice on
//     is one of the system's own unstable orbits, not an imposed waveform: rough
//     to clean as the knob turns, with the control doing a few percent of the
//     work. A model no gain tames without forcing (Lorenz, Chua, Lorenz-Lu-Chen)
//     has a gain of 0, and TAME leaves it free.
//
// What TAME was before (docs/SECRET.md, section 2): until 2026-10-09 a cosine
// push at the note, plus over its upper half a pull back to a stored snapshot
// once a cycle (SYNC) -- hard sync, a looped slice of the attractor. Then the push
// alone, which the scaling had already made redundant: inaudible on most models.
// Delayed feedback replaced it on 2026-10-10.

#include <math.h>
#include "chaos_core/ChaosBase.h"

namespace chaos_core {

    class Voice {
    public:
        enum EnvStage : unsigned char {
            ENV_OPEN, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_CLOSED,
            ENV_OPENING, ENV_CLOSING   // drone <-> gated switches, see render()
        };
        // How long a drone <-> gated switch takes to open or shut the VCA. Short
        // enough to read as immediate, long enough not to click.
        static constexpr float kModeRampMs = 5.0f;
        // Below this the VCA counts as shut (-60 dB): a gate re-seeds only then.
        static constexpr float kSilentLevel = 0.001f;
        // Longest a gate's attack waits for its channel to cross zero.
        static constexpr float kOnsetMaxMs = 25.0f;
        // Corner frequency of the output DC blocker. Low enough to leave the
        // lowest musical content alone, high enough to remove the offset an
        // attractor sitting off-centre would otherwise put on the output.
        static constexpr float kDcBlockHz = 4.9f;

        // Backstop above every algorithm's own step cap. A NaN step count would
        // pass a plain `s < 1` test and then stall the integrator for good:
        // stepAcc_ becomes NaN, (int)stepAcc_ is always 0, no step ever runs,
        // and only setAlgo() clears it.
        static constexpr float kStepsAbsMax = 256.0f;

        void setSampleRate(float sr) {
            if (!(sr > 0.0f)) return;                 // false for NaN too
            sampleRate_ = sr;
            dcCoeff_    = 6.2831853f * kDcBlockHz / sr;
            refreshEnv();                             // times are held in ms
        }
        float sampleRate() const { return sampleRate_; }

        void setAlgo(ChaosBase* a) {
            if (a == algo_) return;
            if (algo_) clearDrive(algo_);     // leave the outgoing one undriven
            if (a) { a->init(); clearDrive(a); }   // initialise state before making live
            dcL_ = dcR_ = 0.0f;      // flush DC history on switch
            stepAcc_   = 0.0f;
            loadScale_ = 1.0f;       // the throttle described the old algorithm's cost
            tame_ = TAME_OFF; drive_ = 0.0f;
            histFilled_ = 0;                           // the history was another attractor's
            algo_ = a;               // atomic pointer store on a 32-bit target
        }

        ChaosBase* algo() const { return algo_; }

        float getX() const { ChaosBase* a = algo_; return a ? a->getX() : 0.0f; }
        float getY() const { ChaosBase* a = algo_; return a ? a->getY() : 0.0f; }

        // One control-rate update: bifurcation parameter, secondary parameter, and
        // pitch as simulated time units per second. Writes several floats, so a
        // host whose audio runs in an interrupt should hold it off across this
        // call — each store is atomic but the set is not, and a whole block
        // integrated from a half-written set can put a system somewhere neither
        // the old nor the new setting was.
        void setParams(float chaos, float charV, float simRate) {
            ChaosBase* a = algo_;
            if (!a) return;
            tame_ = TAME_OFF; drive_ = 0.0f;
            clearDrive(a);
            const StepSchedule sch = a->scheduleFor(simRate, sampleRate_);
            a->setParams(chaos, sch.stepDt, charV);
            float s = sch.stepsPerSample;
            if (!(s >= 1.0f))            s = 1.0f;          // false for NaN too
            else if (s > kStepsAbsMax)   s = kStepsAbsMax;
            stepsPerSample_ = s;
            effectiveDt_    = sch.stepDt * s;   // exact in both schedule branches
        }

        // Play a note: CHAOS, CHAR, pitch in Hz and TAME 0..1 (see the top of
        // this file). Same interrupt caveat as setParams().
        void setPitch(float chaos, float charV, float hz, float tame) {
            ChaosBase* a = algo_;
            if (!a) return;
            if (!(hz > 0.1f)) hz = 0.1f;                        // false for NaN too
            if (!(tame > 0.0f)) tame = 0.0f; else if (tame > 1.0f) tame = 1.0f;

            float fNat = a->naturalFreq(chaos, charV);
            if (!(fNat > 1.0e-3f)) fNat = 1.0e-3f;
            const float simRate = hz / fNat;

            // The feedback gain: squared, so the first half of the knob stays
            // near free-running, up to the model's own gain.
            float gain = a->tameGain;
            if (!(gain > 0.0f)) gain = 0.0f;
            const float k = gain * tame * tame * 6.2831853f * fNat;

            const StepSchedule sch =
                a->scheduleFor(simRate, sampleRate_, a->stableDt(chaos, charV));
            a->setParams(chaos, sch.stepDt, charV);
            float s = sch.stepsPerSample;
            if (!(s >= 1.0f))            s = 1.0f;          // false for NaN too
            else if (s > kStepsAbsMax)   s = kStepsAbsMax;
            stepsPerSample_ = s;
            effectiveDt_    = sch.stepDt * s;

            drive_    = k;
            tauSteps_ = 1.0f / (fNat * sch.stepDt);    // one note period, in steps
            tame_     = TAME_ON;
        }

        // For a display, or a test: the feedback gain in use, K above (0 at TAME 0,
        // and on a model whose gain is 0).
        float feedbackGain() const { return drive_; }

        // Gated mode: re-seed the attractor on a gate's rising edge, as the Teensy
        // build did -- the transient back onto the attractor is the percussive
        // part of a hit -- but only from silence. If the last note is still
        // sounding (its release, or a gate with a short gap) the envelope
        // retriggers from where it is and the trajectory carries on unbroken.
        // Re-seeding under an open VCA was a jump in the output, up to 0.4 of full
        // scale with long gates and a long decay. A 2 ms choke before the re-seed
        // was tried first and was worse: a VCA cut is a click too. The feedback's
        // history goes with the old trajectory; control resumes a note period on.
        void setReseedOnGate(bool on) { reseedOnGate_ = on; }
        // Re-seeds done, for a host hunting clicks. Written by render() only.
        uint32_t reseeds() const { return reseeds_; }

        // RK4 steps per audio sample the current pitch asks for, before the load
        // governor's scale: the number that sets the CPU cost.
        float stepsPerSample() const { return stepsPerSample_; }

        // Simulated time actually advanced per audio sample, after the schedule's
        // clamps — the number worth putting on a display.
        float effectiveDt() const { return effectiveDt_; }

        void setEnvEnabled(bool e) { envEnabled_ = e; }   // off -> VCA stays open (drone)
        void setEnvGate(bool g)    { envGate_    = g; }   // gate input, level not edge
        EnvStage envStage() const  { return envStage_[0]; }   // L; R runs its own
        float    envLevel() const  { return envLevel_[0]; }
        // Start each attack at a zero crossing (default on). See render().
        void     setOnsetAtZero(bool on) { onsetAtZero_ = on; }

        // Attack linear; decay/release exponential (~ -60 dB). Decay approaches the
        // sustain level, release falls from there to zero.
        void setEnvADSR(float atkMs, float decMs, float sustain, float relMs) {
            atkMs_ = atkMs; decMs_ = decMs; relMs_ = relMs;
            envSus_ = (sustain < 0.0f) ? 0.0f : (sustain > 1.0f ? 1.0f : sustain);
            refreshEnv();
        }

        // A host that measures its own block cost can throttle the step rate here
        // rather than letting a block overrun. The policy belongs to the host: only
        // it knows what a block is allowed to cost. 1.0 = no throttling.
        void  setLoadScale(float k) {
            if (!(k > 0.0f)) k = 0.02f;                    // false for NaN too
            loadScale_ = (k > 1.0f) ? 1.0f : k;
        }
        float loadScale() const { return loadScale_; }

        // Render `n` samples into two float buffers, nominally -1..+1. Gate and
        // enable edges are resolved once per call, at block rate.
        void render(float* outL, float* outR, int n) {
            ChaosBase* a = algo_;    // single atomic load — consistent for this block
            if (!a) {
                for (int i = 0; i < n; i++) { outL[i] = 0.0f; outR[i] = 0.0f; }
                return;
            }

            // Switching between drone and gated ramps the VCA over kModeRampMs in
            // either direction. It used to snap -- fully open to fully shut in one
            // sample, and back -- and both were clicks.
            // The envelope runs once per channel: L carries X and R carries Y,
            // and each attack waits for its own channel to cross zero, so the
            // VCA opens where the waveform is near nothing (kOnsetMaxMs at most).
            // Opening a fast attack wherever the waveform happened to be -- often
            // far from zero -- was a click at every note at short AD; a slow
            // attack hid it, which is why AD past halfway cured it.
            if (!envEnabled_) {
                // Disabled: VCA open (drone), gate ignored. Hold the edge
                // detector low rather than tracking the live gate, so a gate that
                // is already high when the envelope is switched on still reads as
                // a rising edge and starts the note straight away, instead of
                // leaving the voice closed until the gate next cycles.
                for (int c = 0; c < 2; c++) {
                    if (envStage_[c] != ENV_OPEN) envStage_[c] = ENV_OPENING;
                    armed_[c] = false;
                }
                envGatePrev_ = false;
            } else {
                for (int c = 0; c < 2; c++)
                    if (envStage_[c] == ENV_OPEN || envStage_[c] == ENV_OPENING) envStage_[c] = ENV_CLOSING;
                const bool g = envGate_;
                if (g && !envGatePrev_) {                               // rising -> (re)trigger
                    const float lv = envLevel_[0] > envLevel_[1] ? envLevel_[0] : envLevel_[1];
                    if (reseedOnGate_ && lv <= kSilentLevel) reseedPending_ = true;
                    for (int c = 0; c < 2; c++) {
                        if (onsetAtZero_) { armed_[c] = true; armAge_[c] = 0; }
                        else envStage_[c] = ENV_ATTACK;
                    }
                } else if (!g && envGatePrev_) {
                    // A channel still waiting for its crossing keeps its note: the
                    // attack runs when the crossing comes and releases at the top.
                    for (int c = 0; c < 2; c++)
                        if (!armed_[c] && envStage_[c] != ENV_CLOSED && envStage_[c] != ENV_CLOSING)
                            envStage_[c] = ENV_RELEASE;
                }
                envGatePrev_ = g;
            }

            const float gL = a->gainL, gR = a->gainR;
            float steps = stepsPerSample_ * loadScale_;
            if (steps < 1.0f) steps = 1.0f;

            if (tame_ == TAME_OFF) {
                // setParams() path: exactly the Teensy build's loop.
                for (int i = 0; i < n; i++) {
                    if (reseedPending_) reseed(a);
                    stepAcc_ += steps;
                    const int k = (int)stepAcc_;
                    stepAcc_ -= (float)k;
                    for (int j = 0; j < k; j++) a->stepSample();
                    envAdvance(0); envAdvance(1);
                    output(a->getX() * gL, a->getY() * gR, outL, outR, i);
                }
                return;
            }

            // No feedback (TAME 0, or a gain of 0): nothing to add, and no history
            // kept, so none goes stale before TAME comes back up.
            const bool control = drive_ > 0.0f;
            if (!control) { clearDrive(a); histFilled_ = 0; }

            for (int i = 0; i < n; i++) {
                if (reseedPending_) reseed(a);
                stepAcc_ += steps;
                const int k = (int)stepAcc_;          // >= 1: steps >= 1
                stepAcc_ -= (float)k;
                for (int j = 0; j < k; j++) {
                    if (control) {
                        // v(t - T) at the step's start, middle and end, against v(t)
                        // there, extrapolated along the last step's slope.
                        float x0 = a->tameState();
                        if (a->tameAngle) {          // unwrap, so the history is continuous
                            thetaU_ += wrapAngle(x0 - thetaRaw_); thetaRaw_ = x0; x0 = thetaU_;
                        }
                        const int   n  = histHead_;
                        hist_[n] = x0;
                        histHead_ = (n + 1) & (kHist - 1);
                        if (histFilled_ < kHist) histFilled_++;
                        const float slope = x0 - histPrev_;
                        histPrev_ = x0;
                        if (tauSteps_ < (float)(kHist - 4) && (float)histFilled_ > tauSteps_ + 3.0f) {
                            float e0 = delayed(n, tauSteps_)        - x0;
                            float eh = delayed(n, tauSteps_ - 0.5f) - (x0 + 0.5f * slope);
                            float e1 = delayed(n, tauSteps_ - 1.0f) - (x0 + slope);
                            if (a->tameAngle) { e0 = wrapAngle(e0); eh = wrapAngle(eh); e1 = wrapAngle(e1); }
                            a->tameD0 = drive_ * e0;
                            a->tameDH = drive_ * eh;
                            a->tameD1 = drive_ * e1;
                        } else {
                            clearDrive(a);
                        }
                    }
                    a->stepSample();
                }
                envAdvance(0); envAdvance(1);
                output(a->getX() * gL, a->getY() * gR, outL, outR, i);
            }
        }

    private:
        enum TameState : unsigned char { TAME_OFF, TAME_ON };

        static void clearDrive(ChaosBase* a) { a->tameD0 = a->tameDH = a->tameD1 = 0.0f; }

        // A gate's re-seed, from silence (see setReseedOnGate).
        void reseed(ChaosBase* a) {
            a->init();
            histFilled_    = 0;
            reseedPending_ = false;
            onsetResync_   = true;    // the jump to the seed is not a zero crossing
            // Centre the DC blocker on the seed, so the new note's output starts
            // at exactly zero -- the attack can open at once, with nothing to
            // click. Left on the old trajectory's mean it sat the new one off
            // centre, and the first zero crossing came 15-18 ms late.
            dcL_ = tanhf(a->getX() * a->gainL);
            dcR_ = tanhf(a->getY() * a->gainR);
            for (int c = 0; c < 2; c++)
                if (armed_[c]) { armed_[c] = false; envStage_[c] = ENV_ATTACK; }
            ++reseeds_;
        }

        // The feedback's history of v, one entry per step, read back with linear
        // interpolation. 4096 steps covers a note period down to ~12 Hz at one
        // step per sample; below that the feedback switches itself off.
        static constexpr int kHist = 4096;
        static float wrapAngle(float d) {
            while (d >  3.14159265f) d -= 6.28318531f;
            while (d < -3.14159265f) d += 6.28318531f;
            return d;
        }
        float delayed(int n, float d) const {
            const float pos = (float)(n + kHist) - d;     // d < kHist: never negative
            const int   i0  = (int)pos;
            const float f   = pos - (float)i0;
            const float a0 = hist_[i0 & (kHist - 1)], a1 = hist_[(i0 + 1) & (kHist - 1)];
            return a0 + (a1 - a0) * f;
        }

        // Soft limiter, DC blocker and envelope, from pre-limiter values.
        void output(float l, float r, float* outL, float* outR, int i) {
            l = tanhf(l);
            r = tanhf(r);
            l -= dcL_; dcL_ += l * dcCoeff_;
            r -= dcR_; dcR_ += r * dcCoeff_;
            if (onsetResync_) { prev_[0] = l; prev_[1] = r; onsetResync_ = false; }
            onset(0, l);
            onset(1, r);
            outL[i] = l * envLevel_[0];
            outR[i] = r * envLevel_[1];
        }

        void refreshEnv() {
            const float perMs = sampleRate_ / 1000.0f;
            float atk = atkMs_ * perMs; if (atk < 1.0f) atk = 1.0f;
            float dec = decMs_ * perMs; if (dec < 1.0f) dec = 1.0f;
            float rel = relMs_ * perMs; if (rel < 1.0f) rel = 1.0f;
            envAtkInc_ = 1.0f / atk;
            envRampInc_ = 1.0f / (kModeRampMs * perMs);
            onsetMax_   = (int)(kOnsetMaxMs * perMs);
            envDecMul_ = expf(-6.908f / dec);      // ln(0.001) ~= -6.908
            envRelMul_ = expf(-6.908f / rel);
        }

        void envAdvance(int c) {
            switch (envStage_[c]) {
                case ENV_ATTACK:
                    envLevel_[c] += envAtkInc_;
                    if (envLevel_[c] >= 1.0f) { envLevel_[c] = 1.0f; envStage_[c] = envGate_ ? ENV_DECAY : ENV_RELEASE; }
                    break;
                case ENV_DECAY:
                    envLevel_[c] = envSus_ + (envLevel_[c] - envSus_) * envDecMul_;
                    if (envLevel_[c] - envSus_ <= 0.0008f) { envLevel_[c] = envSus_; envStage_[c] = ENV_SUSTAIN; }
                    break;
                case ENV_SUSTAIN: envLevel_[c] = envSus_; break;
                case ENV_RELEASE:
                    envLevel_[c] *= envRelMul_;
                    if (envLevel_[c] <= 0.0008f) { envLevel_[c] = 0.0f; envStage_[c] = ENV_CLOSED; }
                    break;
                case ENV_OPENING:
                    envLevel_[c] += envRampInc_;
                    if (envLevel_[c] >= 1.0f) { envLevel_[c] = 1.0f; envStage_[c] = ENV_OPEN; }
                    break;
                case ENV_CLOSING:
                    envLevel_[c] -= envRampInc_;
                    if (envLevel_[c] <= 0.0f) { envLevel_[c] = 0.0f; envStage_[c] = ENV_CLOSED; }
                    break;
                case ENV_OPEN:   envLevel_[c] = 1.0f; break;
                case ENV_CLOSED: envLevel_[c] = 0.0f; break;
            }
        }

        // A gate's attack, held until this channel's pre-VCA signal crosses zero.
        void onset(int c, float v) {
            if (armed_[c]) {
                if ((v >= 0.0f) != (prev_[c] >= 0.0f) || ++armAge_[c] >= onsetMax_) {
                    armed_[c] = false;
                    envStage_[c] = ENV_ATTACK;
                }
            }
            prev_[c] = v;
        }

        ChaosBase* algo_ = nullptr;
        float sampleRate_ = kRefSampleRate;
        float dcCoeff_    = 6.2831853f * kDcBlockHz / kRefSampleRate;
        float dcL_ = 0.0f, dcR_ = 0.0f;
        float stepsPerSample_ = 1.0f, stepAcc_ = 0.0f, effectiveDt_ = 0.0f;
        volatile float loadScale_ = 1.0f;

        volatile bool  envEnabled_ = false;   // off by default — a drone voice
        volatile bool  envGate_    = false;
        bool           envGatePrev_ = false;
        EnvStage       envStage_[2] = {ENV_OPEN, ENV_OPEN};    // per channel, L then R
        float          envLevel_[2] = {1.0f, 1.0f};
        bool           armed_[2]    = {false, false};          // attack waiting for zero
        int            armAge_[2]   = {0, 0};
        float          prev_[2]     = {0.0f, 0.0f};            // last pre-VCA sample
        int            onsetMax_    = 1200;
        bool           onsetAtZero_ = true, onsetResync_ = false;
        float          atkMs_ = 10.0f, decMs_ = 200.0f, relMs_ = 200.0f;
        float          envAtkInc_ = 0.0f, envDecMul_ = 0.0f, envRelMul_ = 0.0f, envSus_ = 0.5f;
        float          envRampInc_ = 0.0f;
        bool           reseedOnGate_ = false, reseedPending_ = false;
        uint32_t       reseeds_ = 0;

        // TAME
        TameState tame_ = TAME_OFF;
        float drive_ = 0.0f;                     // K, the feedback gain in use
        float tauSteps_ = 1.0f, histPrev_ = 0.0f;
        float thetaU_ = 0.0f, thetaRaw_ = 0.0f;
        int   histHead_ = 0, histFilled_ = 0;
        float hist_[kHist] = {};
    };

}  // namespace chaos_core
