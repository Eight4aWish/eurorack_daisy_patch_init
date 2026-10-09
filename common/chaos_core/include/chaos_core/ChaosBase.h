#pragma once
// chaos_core — platform-independent chaotic-attractor DSP.
//
// Nothing here touches Arduino, a HAL, or an audio library: the only dependency
// is <math.h>, so the same sources build for Teensy 4.1 (Cortex-M7), Daisy /
// STM32H7, or a host compiler for offline testing. Integration, I/O and UI
// belong to the platform layer that owns these objects.

#include <math.h>
#include <stdint.h>

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

    // Natural rotation frequency over the CHAOS x CHAR plane, measured on the host
    // by tools/pitchmap.cpp and emitted into PitchTables.h. f[] is row-major,
    // `rows` CHAOS values from cLo to cHi by `cols` CHAR values from hLo to hHi, in
    // cycles per unit of simulated time. refCentre/refAmp describe X (mean, and
    // sqrt(2) x standard deviation), so a reference cosine can sit where X swings.
    struct PitchGrid {
        int   rows, cols;
        float cLo, cHi, hLo, hHi;
        const float* f;
        float refCentre, refAmp;
    };

    // What kind of pitch a system has (docs/SECRET.md, section 2). Descriptive:
    // Voice treats every class the same way, time-scaling plus TAME's push, and
    // the push's ceiling (tameDriveMax) is what differs per model.
    enum PitchClass : unsigned char {
        PITCH_COHERENT,     // rotation rate barely moves: locks to the push
        PITCH_FORCED,       // driven by its own equation: sings that drive's subharmonics
        PITCH_INCOHERENT    // no stable rotation: an average pitch at best
    };

    // ─── ChaosBase ────────────────────────────────────────────────────────────────
    // Abstract base for all chaotic algorithms. Subclasses populate metadata fields
    // in their constructors and implement the pure-virtual methods: init,
    // setParams, stepSample, getX, getY, and saveState/loadState.
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
            return scheduleFor(simRate, sampleRate, dtBase);
        }

        // As above, with the step held to `maxDt` rather than dtBase: stableDt(),
        // where the safe step depends on a parameter. Identical arithmetic when
        // maxDt == dtBase.
        StepSchedule scheduleFor(float simRate, float sampleRate, float maxDt) const {
            StepSchedule s;
            if (!(sampleRate > 0.0f)) return s;              // false for NaN too
            if (!(maxDt > 0.0f) || maxDt > dtBase) maxDt = dtBase;

            // The CPU ceiling is per second, so how many steps that allows per
            // sample falls as the sample rate rises — which is correct: a higher
            // rate means less real time per sample to spend.
            const float maxSteps = maxStepsPerSecond / sampleRate;

            float desiredDt = simRate / sampleRate;
            const float hi  = maxDt * (maxSteps > 1.0f ? maxSteps : 1.0f);
            if (!(desiredDt > 1.0e-5f)) desiredDt = 1.0e-5f;  // false for NaN too
            else if (desiredDt > hi)    desiredDt = hi;

            if (desiredDt <= maxDt) { s.stepDt = desiredDt; s.stepsPerSample = 1.0f; }
            else                    { s.stepDt = maxDt;     s.stepsPerSample = desiredDt / maxDt; }
            return s;
        }

        // ─── Pitch and TAME ───────────────────────────────────────────────────
        //
        // Voice::setPitch() plays a note in Hz rather than a simulated-time rate:
        // simRate = hz / naturalFreq(), so the attractor's own rotation lands on
        // the note. How exactly it lands, and how TAME tightens it, is per class.
        PitchClass       pitchClass  = PITCH_COHERENT;
        const PitchGrid* pitchGrid   = nullptr;   // set from PitchTables.h
        float            fNatDefault = 0.16f;     // cycles per sim time, if no grid

        // TAME's push at full TAME, as a fraction of X's own rate (2 pi fNat x
        // refAmp). Measured push-only, 2026-10-09 (docs/SECRET.md, section 2):
        // 7.5% phase-locks Rossler within 5 cents with its chaos intact, and more
        // starts to bend the attractor. Zero where a push only adds noise --
        // Lorenz and Chua never lock, and their clarity halves under one -- so
        // TAME leaves those free.
        float tameDriveMax = 0.075f;

        // Largest numerically safe step at these parameters. dtBase unless the
        // system's stiffness moves with a parameter, as Van der Pol's does with mu.
        virtual float stableDt(float chaos, float charV) const {
            (void)chaos; (void)charV; return dtBase;
        }

        // Natural rotation frequency, in cycles per unit of simulated time:
        // bilinear in the measured grid, clamped at its edges (MOD CV can push
        // CHAOS past the pot range the grid covers).
        virtual float naturalFreq(float chaos, float charV) const {
            const PitchGrid* g = pitchGrid;
            if (!g || g->rows < 2 || g->cols < 2) return fNatDefault;
            float u = (chaos - g->cLo) / (g->cHi - g->cLo) * (float)(g->rows - 1);
            float v = (charV - g->hLo) / (g->hHi - g->hLo) * (float)(g->cols - 1);
            if (!(u > 0.0f)) u = 0.0f; else if (u > (float)(g->rows - 1)) u = (float)(g->rows - 1);
            if (!(v > 0.0f)) v = 0.0f; else if (v > (float)(g->cols - 1)) v = (float)(g->cols - 1);
            int i = (int)u; if (i > g->rows - 2) i = g->rows - 2;
            int j = (int)v; if (j > g->cols - 2) j = g->cols - 2;
            const float fu = u - (float)i, fv = v - (float)j;
            const float* r0 = g->f + i * g->cols;
            const float* r1 = r0 + g->cols;
            const float a = r0[j] + (r0[j + 1] - r0[j]) * fv;
            const float b = r1[j] + (r1[j + 1] - r1[j]) * fv;
            return a + (b - a) * fu;
        }
        float refCentre() const { return pitchGrid ? pitchGrid->refCentre : 0.0f; }
        float refAmp()    const { return pitchGrid ? pitchGrid->refAmp    : 1.0f; }

        // TAME's drive, written by Voice before every step: a cosine at the note,
        // sampled at the start (D0), middle (DH) and end (D1) of the step so RK4
        // sees it as the smooth forcing it is. Each algorithm adds it to dx.
        //
        // Pure forcing, not diffusive coupling. K(ref - x) was tried first, and
        // its -Kx half is extra damping on X: it changed the system it was meant
        // to entrain, pulling Rossler up to 160 cents flat before it locked.
        // Forcing leaves the autonomous dynamics alone and only pulls. Zero leaves
        // every trajectory bit-identical to an unforced one.
        float tameD0 = 0.0f, tameDH = 0.0f, tameD1 = 0.0f;

        // State access, for a host that wants to hold or restore a trajectory.
        // TAME's sync pulled back to a snapshot through these until 2026-10-09;
        // nothing in Voice uses them now.
        static constexpr int kMaxState = 8;
        virtual int  saveState(float* s) const = 0;   // returns the count written
        virtual void loadState(const float* s) = 0;

        virtual ~ChaosBase() {}
        virtual void  init()                                          = 0;
        virtual void  setParams(float chaos, float rate, float charV) = 0;
        virtual void  stepSample()                                    = 0;
        virtual float getX() const                                    = 0;
        virtual float getY() const                                    = 0;

        // How many times the divergence guard has re-seeded this attractor. A
        // re-seed is a jump in the output, so a host hunting clicks can tell a
        // guard trip from its other causes. Written from stepSample() only, one
        // word, so a control loop may read it while the audio runs.
        uint32_t guardTrips = 0;

    protected:
        // The divergence guard's re-seed: init(), counted. Every stepSample()
        // guard calls this rather than init() directly; trajectories are
        // unchanged.
        void reseed() { ++guardTrips; init(); }

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
