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

#include <math.h>
#include "chaos_core/ChaosBase.h"

namespace chaos_core {

    class Voice {
    public:
        enum EnvStage : unsigned char {
            ENV_OPEN, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_CLOSED
        };

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
            if (a) a->init();        // initialise state before making live
            dcL_ = dcR_ = 0.0f;      // flush DC history on switch
            stepAcc_   = 0.0f;
            loadScale_ = 1.0f;       // the throttle described the old algorithm's cost
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
            const StepSchedule sch = a->scheduleFor(simRate, sampleRate_);
            a->setParams(chaos, sch.stepDt, charV);
            float s = sch.stepsPerSample;
            if (!(s >= 1.0f))            s = 1.0f;          // false for NaN too
            else if (s > kStepsAbsMax)   s = kStepsAbsMax;
            stepsPerSample_ = s;
            effectiveDt_    = sch.stepDt * s;   // exact in both schedule branches
        }

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

            for (int i = 0; i < n; i++) {
                stepAcc_ += steps;
                const int k = (int)stepAcc_;
                stepAcc_ -= (float)k;
                for (int j = 0; j < k; j++) a->stepSample();

                envAdvance();

                float l = tanhf(a->getX() * gL);
                float r = tanhf(a->getY() * gR);
                l -= dcL_; dcL_ += l * dcCoeff_;
                r -= dcR_; dcR_ += r * dcCoeff_;
                outL[i] = l * envLevel_;
                outR[i] = r * envLevel_;
            }
        }

    private:
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
    };

}  // namespace chaos_core
