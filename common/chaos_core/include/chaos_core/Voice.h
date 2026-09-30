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
//   - FORCE (coherent and forced systems), in two halves. Up to TAME 0.5 a
//     cosine at the note is added to dX, rising to 5% of X's own rate: the voice
//     goes from free, through phase slips, to phase-locked chaos -- an exact
//     pitch under a still-chaotic waveform, the pitched-but-gritty middle. Above
//     0.5 the drive holds and SYNC's pull fades in, until TAME 1 is strictly
//     periodic. Measured (tools/tametest.cpp): stronger drive alone does not
//     get there -- past ~10% it throws Rossler into period-2 and chaos instead.
//   - SYNC (incoherent systems, where drive alone never locks): Ogham's hard
//     sync, softened. A snapshot is taken on the attractor, and at each cycle of
//     the note the state moves TAME of the way back to it: 1 re-seeds exactly
//     (strictly periodic), less lets the chaos leak through. The jump is hidden
//     by a short output crossfade.
//
// The reference runs in simulated time under FORCE (it has to keep pace with
// the dynamics it drives) and in real time under SYNC (so the note stays
// exact even when the step cap or the load governor slows the integration).

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
            ENV_OPEN, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_CLOSED
        };
        // AUTO picks FORCE or SYNC from the algorithm's PitchClass.
        enum TameMode : unsigned char { TAME_AUTO, TAME_FORCE, TAME_SYNC };

        // FORCE: drive amplitude at the top of its range, as a fraction of X's
        // own rate of change (2 pi f_nat x refAmp), and the TAME value where the
        // drive tops out and the sync pull begins. Measured: 1-7.5% locks most
        // coherent settings with the chaos intact; 15% and up breaks them.
        static constexpr float kDriveMax   = 0.05f;
        static constexpr float kForceSplit = 0.5f;
        // A snapshot survives parameter moves smaller than this fraction of the
        // pot range, so CV on CHAOS or CHAR does not keep switching sync off.
        static constexpr float kSnapTolerance = 0.05f;
        // Cycles of the note to wait before taking a snapshot, so it comes from
        // a settled -- under FORCE, locked -- trajectory. See syncStep().
        static constexpr int kSettleCycles = 8;
        // Output crossfade over a sync jump: this long, or a tenth of a cycle
        // if that is shorter.
        static constexpr float kDeclickMs = 0.5f;

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
            tame_ = TAME_OFF; drive_ = 0.0f; syncW_ = 0.0f;
            dclL_ = dclR_ = 0.0f;
            snapValid_ = false; snapStale_ = false;   // a snapshot belongs to one attractor
            captureWait_ = 0;
            algo_ = a;               // atomic pointer store on a 32-bit target
        }

        // Drop the sync snapshot, so the next one is taken afresh. Call it where
        // the platform re-inits the attractor (a gate edge): the old snapshot
        // would otherwise yank the new trajectory straight back.
        void invalidateSnapshot() { snapValid_ = false; snapStale_ = false; captureWait_ = 0; }
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
            tame_ = TAME_OFF; drive_ = 0.0f; syncW_ = 0.0f;
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
        // this file). Same interrupt caveat as setParams(). `mode` overrides the
        // algorithm's own choice of FORCE or SYNC -- the panel's B2.
        void setPitch(float chaos, float charV, float hz, float tame,
                      TameMode mode = TAME_AUTO) {
            ChaosBase* a = algo_;
            if (!a) return;
            if (!(hz > 0.1f)) hz = 0.1f;                        // false for NaN too
            if (!(tame > 0.0f)) tame = 0.0f; else if (tame > 1.0f) tame = 1.0f;
            if (mode == TAME_AUTO)
                mode = (a->pitchClass == PITCH_INCOHERENT) ? TAME_SYNC : TAME_FORCE;

            float fNat = a->naturalFreq(chaos, charV);
            if (!(fNat > 1.0e-3f)) fNat = 1.0e-3f;
            const float simRate = hz / fNat;

            // FORCE: drive over the lower part of the knob, squared so its
            // first half stays near free-running (phase slips live there), then
            // the sync pull, linear, over the upper part.
            //
            // SYNC: the pull alone, as 1 - (1 - TAME)^2. Measured on Lorenz and
            // Chua, pulls below ~0.7 mostly inject a jump per cycle into chaos
            // that stays chaotic (clarity 0.1-0.3, under the free voice's 0.5),
            // and the pitched-but-gritty zone is pulls of ~0.8-0.97. This taper
            // puts that zone in the upper-middle of the knob (TAME 0.55-0.8)
            // rather than squeezing it into the last fifth. Whether the lower
            // half's once-a-cycle jumps are useful grit is for the ear.
            float drive = 0.0f, pull = 1.0f - (1.0f - tame) * (1.0f - tame);
            if (mode == TAME_FORCE) {
                const float lo = (tame < kForceSplit) ? tame / kForceSplit : 1.0f;
                drive = kDriveMax * lo * lo * 6.2831853f * fNat * a->refAmp();
                pull  = (tame > kForceSplit) ? (tame - kForceSplit) / (1.0f - kForceSplit) : 0.0f;
            }

            const StepSchedule sch =
                a->scheduleFor(simRate, sampleRate_, a->stableDt(chaos, charV));
            a->setParams(chaos, sch.stepDt, charV);
            float s = sch.stepsPerSample;
            if (!(s >= 1.0f))            s = 1.0f;          // false for NaN too
            else if (s > kStepsAbsMax)   s = kStepsAbsMax;
            stepsPerSample_ = s;
            effectiveDt_    = sch.stepDt * s;

            drive_ = drive;
            refIncStep_   = fNat * sch.stepDt;         // FORCE: cycles per step
            refIncSample_ = hz / sampleRate_;          // SYNC: cycles per sample
            syncW_     = pull;
            float tau  = kDeclickMs * 0.001f;
            if (tau > 0.1f / hz) tau = 0.1f / hz;
            dclMul_    = expf(-1.0f / (tau * sampleRate_));

            // A snapshot from well away from here is from another attractor:
            // retake it once the trajectory has settled (syncStep). Small moves
            // keep it, and
            // the loop simply integrates under the new settings from the old
            // start -- still periodic, with the timbre following the knobs.
            const float cTol = kSnapTolerance * (a->chaosMax - a->chaosMin);
            const float hTol = kSnapTolerance * (a->charMax - a->charMin);
            if (fabsf(chaos - snapChaos_) > cTol || fabsf(charV - snapChar_) > hTol) {
                snapStale_ = true;
                captureWait_ = 0;
                snapChaos_ = chaos; snapChar_ = charV;
            }
            tame_ = (mode == TAME_SYNC) ? TAME_ON_SYNC : TAME_ON_FORCE;
        }
        void setSyncEvery(int n) { syncEvery_ = (n < 1) ? 1 : (n > 64 ? 64 : n); }

        // For a display, or a test: the drive amplitude in use (0 unless FORCE),
        // and whether SYNC currently holds a snapshot to pull back to.
        float driveAmp()    const { return drive_; }
        bool  syncHolding() const { return snapValid_; }

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

            if (!envEnabled_) {
                // Disabled: VCA fully open (drone), gate ignored. Hold the edge
                // detector low rather than tracking the live gate, so a gate that
                // is already high when the envelope is switched on still reads as
                // a rising edge and starts the note straight away, instead of
                // leaving the voice closed until the gate next cycles.
                envStage_ = ENV_OPEN; envLevel_ = 1.0f; envGatePrev_ = false;
            } else {
                if (envStage_ == ENV_OPEN) { envStage_ = ENV_CLOSED; envLevel_ = 0.0f; }
                const bool g = envGate_;
                if (g && !envGatePrev_)      envStage_ = ENV_ATTACK;    // rising -> (re)trigger
                else if (!g && envGatePrev_ && envStage_ != ENV_CLOSED) envStage_ = ENV_RELEASE;
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

            const bool sync  = (tame_ == TAME_ON_SYNC);
            const bool force = (tame_ == TAME_ON_FORCE) && drive_ > 0.0f;
            const bool pull  = syncW_ > 0.0f;
            if (!force) clearDrive(a);

            for (int i = 0; i < n; i++) {
                stepAcc_ += steps;
                const int k = (int)stepAcc_;          // >= 1: steps >= 1
                stepAcc_ -= (float)k;
                const float inc = sync ? refIncSample_ / (float)k : refIncStep_;
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
                        if (pull && (!snapValid_ || snapStale_)) captureWait_++;
                    }
                    if (pull) syncStep(a, inc, gL, gR);
                }
                envAdvance();
                const float pl = a->getX() * gL + dclL_;
                const float pr = a->getY() * gR + dclR_;
                dclL_ *= dclMul_; dclR_ *= dclMul_;
                output(pl, pr, outL, outR, i);
            }
        }

    private:
        enum TameState : unsigned char { TAME_OFF, TAME_ON_FORCE, TAME_ON_SYNC };

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

        // SYNC's per-step bookkeeping. Once per cycle of the note (every
        // syncEvery_ cycles), pull the state syncW_ of the way back to the
        // snapshot, carrying the output's jump in the declick offsets.
        //
        // When the snapshot is taken matters most. It has to come from the
        // settled, locked trajectory: taken two cycles after a cold start, before
        // FORCE's drive had locked the phase, every pull dragged the phase back
        // towards an unlocked point, the drive could not hold both, and at pull
        // 0.2-0.3 Rossler slipped a cycle every ~36 -- 35-45 cents flat, periodic
        // and wrong. So capture waits kSettleCycles, first time and every time.
        //
        // Where in the cycle is a smaller matter: the snapshot is taken at a
        // peak of X, and syncAcc_ -- a second phase at the note's rate, zeroed at
        // the capture -- brings every pull back to that same point, so a jump
        // lands where it changes the amplitude rather than when X crosses zero.
        // Being a fixed offset from the reference, it also keeps to one phase of
        // FORCE's drive. A stale snapshot is pulled to until its replacement.
        void syncStep(ChaosBase* a, float inc, float gL, float gR) {
            const float x = a->getX();
            const bool peak = (x1_ > x2_) && (x <= x1_);
            x2_ = x1_; x1_ = x;
            if ((!snapValid_ || snapStale_) && captureWait_ >= kSettleCycles) {
                if (peak) {
                    a->saveState(snap_);
                    snapValid_ = true; snapStale_ = false;
                    captureWait_ = 0; cycleCount_ = 0; syncAcc_ = 0.0f;
                    return;
                }
            }
            if (!snapValid_) return;
            syncAcc_ += inc;
            if (syncAcc_ < 1.0f) return;
            syncAcc_ -= 1.0f;
            if (syncAcc_ >= 1.0f) syncAcc_ = 0.0f;
            if (++cycleCount_ < syncEvery_) return;
            cycleCount_ = 0;
            const float bl = a->getX() * gL + dclL_, br = a->getY() * gR + dclR_;
            a->blendState(snap_, syncW_);
            dclL_ = bl - a->getX() * gL;
            dclR_ = br - a->getY() * gR;
            x1_ = x2_ = a->getX();            // a pull is not a peak
        }

        void refreshEnv() {
            const float perMs = sampleRate_ / 1000.0f;
            float atk = atkMs_ * perMs; if (atk < 1.0f) atk = 1.0f;
            float dec = decMs_ * perMs; if (dec < 1.0f) dec = 1.0f;
            float rel = relMs_ * perMs; if (rel < 1.0f) rel = 1.0f;
            envAtkInc_ = 1.0f / atk;
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

        // TAME
        TameState tame_ = TAME_OFF;
        float drive_ = 0.0f, syncW_ = 0.0f;
        float refPhase_ = 0.0f, refIncStep_ = 0.0f, refIncSample_ = 0.0f;
        float dclL_ = 0.0f, dclR_ = 0.0f, dclMul_ = 0.0f;
        float snap_[ChaosBase::kMaxState] = {};
        bool  snapValid_ = false, snapStale_ = false;
        float snapChaos_ = -1.0e30f, snapChar_ = -1.0e30f;
        int   syncEvery_ = 1, cycleCount_ = 0, captureWait_ = 0;
        float syncAcc_ = 0.0f, x1_ = 0.0f, x2_ = 0.0f;
    };

}  // namespace chaos_core
