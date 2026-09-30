#pragma once
// chaos_core — platform-independent chaotic-attractor DSP.
//
// Nothing here touches Arduino, a HAL, or an audio library: the only dependency
// is <math.h>, so the same sources build for Teensy 4.1 (Cortex-M7), Daisy /
// STM32H7, or a host compiler for offline testing. Integration, I/O and UI
// belong to the platform layer that owns these objects.

#include <math.h>

namespace chaos_core {

    // The sample rate the per-second figures below were originally measured at.
    // Nothing is required to run here; it is the reference the numbers came from.
    constexpr float kRefSampleRate = 44100.0f;

    // How to realise a requested pitch: a step size that is safe to integrate at,
    // and how many such steps to run per audio sample (fractional — the caller
    // carries the remainder in an accumulator).
    struct StepSchedule {
        float stepDt        = 0.0f;
        float stepsPerSample = 1.0f;
    };

    // ─── ChaosBase ────────────────────────────────────────────────────────────────
    // Abstract base for all chaotic algorithms. Subclasses populate metadata fields
    // in their constructors and implement the four pure-virtual methods.
    class ChaosBase {
    public:
        const char* name       = "?";
        const char* chaosLabel = "c";   // display label for CHAOS param
        const char* charLabel  = "a";   // display label for CHAR param
        float chaosMin = 0.0f,   chaosMax = 1.0f;
        float charMin  = 0.0f,   charMax  = 1.0f;

        // ─── Rate, in three parts that must not be confused ───────────────────
        //
        // `dt` here means simulated time advanced per audio sample, so the pitch
        // heard is dt x sampleRate. That makes a per-sample dt a sample-rate
        // dependent quantity, and the three limits below split along that line:
        //
        //   dtBase            — largest numerically-safe integration STEP. A
        //                       property of the equations and of RK4. Does NOT
        //                       depend on sample rate.
        //   simRateMin/Max    — the pot's pitch range, in simulated time units
        //                       per SECOND. Does not depend on sample rate.
        //   maxStepsPerSecond — the CPU ceiling, in integration steps per
        //                       SECOND. Does not depend on sample rate.
        //
        // These were per-sample quantities (`rateMax`, `oversampleMax`) until the
        // engine had to run at something other than 44.1 kHz. Holding pitch as a
        // per-sample dt meant every range measured at 44.1 kHz came out ~1.1
        // octaves sharp at 96 kHz, and the per-sample step cap came out over
        // twice as permissive as the CPU budget it was measured against — the
        // audio block would overrun and the load governor would be left to catch
        // it. Both are now per-second and survive the move.
        float simRateMin = 44.1f, simRateMax = 4410.0f;
        float dtBase     = 0.05f;
        // Pitch above what `dtBase` alone can reach is produced by oversampling
        // (multiple steps per audio sample) rather than by growing dt, so V/Oct
        // tracks without diverging. This caps that: oversampling is the one
        // control that buys pitch with CPU, so the ceiling is per-algorithm — a
        // Duffing step costs ~6x a Rossler step (three cosf calls), and a single
        // global cap is either unsafe for the expensive systems or needlessly
        // tight for the cheap ones. Values are set so every algorithm tops out at
        // a similar share of the cycle budget. Raise one only against a measured
        // CPU figure, and note that figure is per second now, not per sample.
        float maxStepsPerSecond = 64.0f * kRefSampleRate;
        float modScale = 1.0f;          // chaos-param units per volt of MOD CV
        // Absolute limits the chaos parameter may be driven to once MOD CV is
        // added. These are not decoration: several systems lose their bounded
        // attractor just outside the pot range — Rossler below c~1.25, Coupled
        // Rossler below c~0.5 — so a CV that overshoots turns the voice into a
        // repeating re-seed. Always at least [chaosMin, chaosMax], so the pot's
        // own range stays reachable; beyond that, measured per algorithm.
        float modMin = 0.0f, modMax = 1.0f;
        // Pre-tanh amplitude scale. The host writes tanhf(state * gain) * 32000,
        // so the tanh is a soft limiter and these are a voicing decision, not a
        // safety one — nothing can exceed full scale whatever they are set to.
        //
        // Set by measurement so all six algorithms sit at the same level: gain =
        // atanh(0.90) / median peak |state| over a 5x5x3 sweep of the pot range.
        // The MEDIAN matters. Calibrating on the maximum drags everything down —
        // and for Chua the maximum is its divergence spike (max|x| lands exactly
        // on divergeBound), which is not a thing anyone plays. So normal settings
        // land at 90% of full scale and the loud corners saturate into the tanh,
        // which is what it is there for. L and R are measured separately because
        // getX() and getY() are different state variables with different extents
        // (Van der Pol's y swings 6x its x).
        float gainL    = 0.12f, gainR = 0.12f;
        float xMin = -1.0f, xRange = 2.0f;     // plot window
        float yMin = -1.0f, yRange = 2.0f;
        float cvScaleX = 0.5f, cvScaleY = 0.5f; // state → ±5V CV
        // Divergence guard. RK4 runs away at the edges of some parameter ranges,
        // and once the state is non-finite every later step inherits it: the
        // voice goes silent with X/Y stuck on a rail until the algorithm is
        // changed. stepSample() tests its state against this bound and re-seeds
        // via init() instead, turning a dead module into a brief glitch.
        //
        // Set well above the attractor's natural extent. Divergence is
        // exponential, so a runaway crosses any threshold within a handful of
        // samples, while a healthy trajectory never approaches one — the cost of
        // a generous bound is a few more samples of garbage, the cost of a tight
        // one is re-seeding a perfectly good trajectory.
        float divergeBound = 1.0e3f;

        // Turn a requested pitch (simulated time units per second) into a step
        // size and a step count for one audio sample at `sampleRate`.
        //
        // Below dtBase the whole advance fits in one step, so it runs one. Above
        // it the step is held at dtBase and the extra time is bought with more
        // steps, which is what keeps stiff systems bounded under V/Oct.
        StepSchedule scheduleFor(float simRate, float sampleRate) const {
            StepSchedule s;
            if (!(sampleRate > 0.0f)) return s;              // false for NaN too

            // The CPU ceiling is per second, so how many steps that allows per
            // sample falls as the sample rate rises — which is correct: a higher
            // rate means less real time per sample to spend.
            const float maxSteps = maxStepsPerSecond / sampleRate;

            float desiredDt = simRate / sampleRate;
            const float hi  = dtBase * (maxSteps > 1.0f ? maxSteps : 1.0f);
            if (!(desiredDt > 1.0e-5f)) desiredDt = 1.0e-5f;  // false for NaN too
            else if (desiredDt > hi)    desiredDt = hi;

            if (desiredDt <= dtBase) { s.stepDt = desiredDt; s.stepsPerSample = 1.0f; }
            else                     { s.stepDt = dtBase;    s.stepsPerSample = desiredDt / dtBase; }
            return s;
        }

        virtual ~ChaosBase() {}
        virtual void  init()                                          = 0;
        virtual void  setParams(float chaos, float rate, float charV) = 0;
        virtual void  stepSample()                                    = 0;
        virtual float getX() const                                    = 0;
        virtual float getY() const                                    = 0;

    protected:
        // True once `v` has left the region any healthy trajectory stays in.
        // Apply to the variable `divergeBound` was chosen for.
        bool diverged(float v) const {
            return !isfinite(v) || fabsf(v) > divergeBound;
        }
        // The unrecoverable case only, without the magnitude test — for state
        // that legitimately ranges wider than the bound (Chua's z reaches past
        // the value that catches a runaway in x, and Van der Pol's y past the
        // one that catches x), where a shared bound would reset healthy runs.
        static bool nonFinite(float v) { return !isfinite(v); }
    };

}  // namespace chaos_core
