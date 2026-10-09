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
//   - TAME adds a small periodic push at the note to dX: a cosine, rising with
//     TAME squared to the model's own maximum (ChaosBase::tameDriveMax, a fraction
//     of X's own rate). That is the equation plus a weak outside drive. Systems
//     with a coherent rotation phase-lock to it with their chaos intact; driven
//     systems keep singing their own subharmonics; Lorenz and Chua ignore a drive
//     altogether, so their maximum is zero and TAME leaves them free.
//
// Until 2026-10-09 TAME's upper half also pulled the state back to a stored
// snapshot once a cycle (SYNC): hard sync, which imposed the pitch by looping a
// slice of the attractor. Removed at David's direction: the module plays what the
// equations do, and a model that will not be tamed is left untamed.

#include <math.h>
#include "chaos_core/ChaosBase.h"

namespace chaos_core {

    // cos(2 pi p) for p in [0, 1). Polynomial, error below 3e-5: enough for a
    // reference signal, and far cheaper than cosf on a Cortex-M7.
    inline float cos2pi(float p) {
        float a = fabsf(p - 0.5f);                 // cos(2 pi p) = -cos(2 pi a)
        float sgn = -1.0f;
        if (a > 0.25f) { a = 0.5f - a; sgn = 1.0f; }
        const float t2 = 39.4784176f * a * a;      // (2 pi a)^2, a in [0, 1/4]
        return sgn * (1.0f + t2 * (-0.5f + t2 * (1.0f / 24.0f
                     + t2 * (-1.0f / 720.0f + t2 * (1.0f / 40320.0f)))));
    }

    class Voice {
    public:
        enum EnvStage : unsigned char {
            ENV_OPEN, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_CLOSED,
            ENV_OPENING, ENV_CLOSING   // drone <-> gated switches, see render()
        };
        // How long a drone <-> gated switch takes to open or shut the VCA. Short
        // enough to read as immediate, long enough not to click.
        static constexpr float kModeRampMs = 5.0f;
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

            // The push: squared, so the first half of the knob stays near
            // free-running (phase slips live there), up to the model's maximum.
            float dmax = a->tameDriveMax;
            if (!(dmax > 0.0f)) dmax = 0.0f;
            const float drive = dmax * tame * tame * 6.2831853f * fNat * a->refAmp();

            const StepSchedule sch =
                a->scheduleFor(simRate, sampleRate_, a->stableDt(chaos, charV));
            a->setParams(chaos, sch.stepDt, charV);
            float s = sch.stepsPerSample;
            if (!(s >= 1.0f))            s = 1.0f;          // false for NaN too
            else if (s > kStepsAbsMax)   s = kStepsAbsMax;
            stepsPerSample_ = s;
            effectiveDt_    = sch.stepDt * s;

            drive_      = drive;
            refIncStep_ = fNat * sch.stepDt;           // cycles of the note per step
            tame_       = TAME_ON;
        }

        // For a display, or a test: the push amplitude in use (0 at TAME 0, and on
        // a model whose maximum is 0).
        float driveAmp() const { return drive_; }

        // RK4 steps per audio sample the current pitch asks for, before the load
        // governor's scale: the number that sets the CPU cost.
        float stepsPerSample() const { return stepsPerSample_; }

        // Simulated time actually advanced per audio sample, after the schedule's
        // clamps — the number worth putting on a display.
        float effectiveDt() const { return effectiveDt_; }

        void setEnvEnabled(bool e) { envEnabled_ = e; }   // off -> VCA stays open (drone)
        void setEnvGate(bool g)    { envGate_    = g; }   // gate input, level not edge
        EnvStage envStage() const  { return envStage_; }

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
            if (!envEnabled_) {
                // Disabled: VCA open (drone), gate ignored. Hold the edge
                // detector low rather than tracking the live gate, so a gate that
                // is already high when the envelope is switched on still reads as
                // a rising edge and starts the note straight away, instead of
                // leaving the voice closed until the gate next cycles.
                if (envStage_ != ENV_OPEN) envStage_ = ENV_OPENING;
                envGatePrev_ = false;
            } else {
                if (envStage_ == ENV_OPEN || envStage_ == ENV_OPENING) envStage_ = ENV_CLOSING;
                const bool g = envGate_;
                if (g && !envGatePrev_)      envStage_ = ENV_ATTACK;    // rising -> (re)trigger
                else if (!g && envGatePrev_ && envStage_ != ENV_CLOSED && envStage_ != ENV_CLOSING)
                    envStage_ = ENV_RELEASE;
                envGatePrev_ = g;
            }

            const float gL = a->gainL, gR = a->gainR;
            float steps = stepsPerSample_ * loadScale_;
            if (steps < 1.0f) steps = 1.0f;

            if (tame_ == TAME_OFF) {
                // setParams() path: exactly the Teensy build's loop.
                for (int i = 0; i < n; i++) {
                    stepAcc_ += steps;
                    const int k = (int)stepAcc_;
                    stepAcc_ -= (float)k;
                    for (int j = 0; j < k; j++) a->stepSample();
                    envAdvance();
                    output(a->getX() * gL, a->getY() * gR, outL, outR, i);
                }
                return;
            }

            const bool force = drive_ > 0.0f;
            if (!force) clearDrive(a);

            for (int i = 0; i < n; i++) {
                stepAcc_ += steps;
                const int k = (int)stepAcc_;          // >= 1: steps >= 1
                stepAcc_ -= (float)k;
                const float inc = refIncStep_;
                for (int j = 0; j < k; j++) {
                    if (force) {
                        float h = refPhase_ + 0.5f * inc; if (h >= 1.0f) h -= 1.0f;
                        float e = refPhase_ + inc;        if (e >= 1.0f) e -= 1.0f;
                        a->tameD0 = drive_ * cos2pi(refPhase_);
                        a->tameDH = drive_ * cos2pi(h);
                        a->tameD1 = drive_ * cos2pi(e);
                    }
                    a->stepSample();
                    refPhase_ += inc;
                    if (refPhase_ >= 1.0f) {
                        refPhase_ -= 1.0f;
                        if (refPhase_ >= 1.0f) refPhase_ = 0.0f;
                    }
                }
                envAdvance();
                output(a->getX() * gL, a->getY() * gR, outL, outR, i);
            }
        }

    private:
        enum TameState : unsigned char { TAME_OFF, TAME_ON };

        static void clearDrive(ChaosBase* a) { a->tameD0 = a->tameDH = a->tameD1 = 0.0f; }

        // Soft limiter, DC blocker and envelope, from pre-limiter values.
        void output(float l, float r, float* outL, float* outR, int i) {
            l = tanhf(l);
            r = tanhf(r);
            l -= dcL_; dcL_ += l * dcCoeff_;
            r -= dcR_; dcR_ += r * dcCoeff_;
            outL[i] = l * envLevel_;
            outR[i] = r * envLevel_;
        }

        void refreshEnv() {
            const float perMs = sampleRate_ / 1000.0f;
            float atk = atkMs_ * perMs; if (atk < 1.0f) atk = 1.0f;
            float dec = decMs_ * perMs; if (dec < 1.0f) dec = 1.0f;
            float rel = relMs_ * perMs; if (rel < 1.0f) rel = 1.0f;
            envAtkInc_ = 1.0f / atk;
            envRampInc_ = 1.0f / (kModeRampMs * perMs);
            envDecMul_ = expf(-6.908f / dec);      // ln(0.001) ~= -6.908
            envRelMul_ = expf(-6.908f / rel);
        }

        void envAdvance() {
            switch (envStage_) {
                case ENV_ATTACK:
                    envLevel_ += envAtkInc_;
                    if (envLevel_ >= 1.0f) { envLevel_ = 1.0f; envStage_ = ENV_DECAY; }
                    break;
                case ENV_DECAY:
                    envLevel_ = envSus_ + (envLevel_ - envSus_) * envDecMul_;
                    if (envLevel_ - envSus_ <= 0.0008f) { envLevel_ = envSus_; envStage_ = ENV_SUSTAIN; }
                    break;
                case ENV_SUSTAIN: envLevel_ = envSus_; break;
                case ENV_RELEASE:
                    envLevel_ *= envRelMul_;
                    if (envLevel_ <= 0.0008f) { envLevel_ = 0.0f; envStage_ = ENV_CLOSED; }
                    break;
                case ENV_OPENING:
                    envLevel_ += envRampInc_;
                    if (envLevel_ >= 1.0f) { envLevel_ = 1.0f; envStage_ = ENV_OPEN; }
                    break;
                case ENV_CLOSING:
                    envLevel_ -= envRampInc_;
                    if (envLevel_ <= 0.0f) { envLevel_ = 0.0f; envStage_ = ENV_CLOSED; }
                    break;
                case ENV_OPEN:   envLevel_ = 1.0f; break;
                case ENV_CLOSED: envLevel_ = 0.0f; break;
            }
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
        EnvStage       envStage_   = ENV_OPEN;
        float          envLevel_   = 1.0f;
        float          atkMs_ = 10.0f, decMs_ = 200.0f, relMs_ = 200.0f;
        float          envAtkInc_ = 0.0f, envDecMul_ = 0.0f, envRelMul_ = 0.0f, envSus_ = 0.5f;
        float          envRampInc_ = 0.0f;

        // TAME
        TameState tame_ = TAME_OFF;
        float drive_ = 0.0f;
        float refPhase_ = 0.0f, refIncStep_ = 0.0f;
    };

}  // namespace chaos_core
