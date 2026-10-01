#pragma once
// Candidates in reserve for a later bank: measured, tabled and passing tametest,
// but not chosen for bank 2 (docs/SECRET.md, section 3). Host tools only: the
// firmware builds src/Registry.cpp and nothing here, so a candidate never changes
// what Secret plays until it is promoted into a bank.

#include <math.h>
#include "chaos_core/OdeModel.h"

namespace chaos_core {

    // ─── Rikitake two-disc dynamo ─────────────────────────────────────────────
    // A model of the geomagnetic field's polarity reversals (Rikitake 1958):
    //   dx = -mu x + y z,  dy = -mu y + (z - a) x,  dz = 1 - x y
    // x and y flip sign together at irregular intervals: under the pitch, a
    // rhythm. Chaotic at mu = 2, a = 5. CHAOS = mu (reversed), CHAR = a.
    //
    // Replaces the hyperchaotic Rössler, which was measured unplayable: in
    // Rössler's 1979 form it stays bounded only at isolated points near
    // a = 0.25, d = 0.05 (3 of 117 on a fine grid), swinging to ~170 even there,
    // and escapes to infinity everywhere else.
    class CandRikitake : public OdeModel<3, CandRikitake> {
    public:
        CandRikitake() {
            name = "RIKITAKE"; chaosLabel = "m"; charLabel = "a";
            // Reversed, so CHAOS up is more chaotic as on every other model:
            // measured, mu above ~2.6 is a period-3 band and below ~2.2 chaos.
            chaosMin = 2.9f;  chaosMax = 1.0f;
            charMin  = 3.0f;  charMax  = 7.0f;
            dtBase = 0.02f;  divergeBound = 200.0f;
            simRateMin = 50.0f; simRateMax = 4000.0f;
            maxStepsPerSecond = 64.0f * kRefSampleRate;  // P class
            modScale = 0.2f;  modMin = chaosMin; modMax = chaosMax;
            gainL = 0.247f; gainR = 0.492f;   // measured, characterise
            xMin = -8.7f; xRange = 17.8f; yMin = -5.2f; yRange = 10.2f;
            cvScaleX = 0.5f; cvScaleY = 0.9f;
            pitchClass = PITCH_INCOHERENT;   // reversals, as Lorenz's lobes
        }
        void initState(float* s) const { s[0] = 1.0f; s[1] = 0.5f; s[2] = 0.5f; }
        void deriv(const float* s, float drive, float* ds) const {
            ds[0] = -chaos_ * s[0] + s[1] * s[2] + drive;
            ds[1] = -chaos_ * s[1] + (s[2] - char_) * s[0];
            ds[2] = 1.0f - s[0] * s[1];
        }
        bool escaped() const { return anyDiverged(3); }
        float getX() const override { return s_[0]; }
        float getY() const override { return s_[1]; }
    };

    // ─── Shimizu–Morioka ──────────────────────────────────────────────────────
    // dx = y, dy = (1 - z) x - lambda y, dz = -alpha z + x^2
    // Lorenz-like lobe switching, slower. Chaotic near lambda 0.75-0.81,
    // alpha 0.375-0.45. CHAOS = lambda, CHAR = alpha.
    class CandShimizuMorioka : public OdeModel<3, CandShimizuMorioka> {
    public:
        CandShimizuMorioka() {
            name = "SHIMIZU MORIOKA"; chaosLabel = "l"; charLabel = "a";
            chaosMin = 0.4f;  chaosMax = 0.95f;   // measured: period-1, 2, 4 cascade below ~0.6
            charMin  = 0.3f;  charMax  = 0.5f;
            dtBase = 0.05f;  divergeBound = 100.0f;
            simRateMin = 50.0f; simRateMax = 4000.0f;
            maxStepsPerSecond = 64.0f * kRefSampleRate;  // P class
            modScale = 0.05f; modMin = chaosMin; modMax = chaosMax;
            gainL = 1.141f; gainR = 1.233f;   // measured, characterise
            xMin = -1.5f; xRange = 3.0f; yMin = -1.0f; yRange = 2.7f;
            cvScaleX = 3.0f; cvScaleY = 2.8f;
            pitchClass = PITCH_INCOHERENT;
        }
        void initState(float* s) const { s[0] = 0.1f; s[1] = 0.0f; s[2] = 0.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            ds[0] = s[1] + drive;
            ds[1] = (1.0f - s[2]) * s[0] - chaos_ * s[1];
            ds[2] = -char_ * s[2] + s[0] * s[0];
        }
        bool escaped() const { return anyDiverged(3); }
        float getX() const override { return s_[0]; }
        float getY() const override { return s_[2] - 1.0f; }   // centred, roughly
    };

    // ─── Genesio–Tesi ─────────────────────────────────────────────────────────
    // Jerk system: x''' = -a x'' - b x' - c x + x^2
    // dx = y, dy = z, dz = -c x - b y - a z + x^2.  Chaotic at a 0.44, b 1.1, c 1.
    // Escapes to infinity just outside its chaotic band. CHAOS = a, CHAR = b.
    class CandGenesioTesi : public OdeModel<3, CandGenesioTesi> {
    public:
        CandGenesioTesi() {
            name = "GENESIO TESI"; chaosLabel = "a"; charLabel = "b";
            // Measured: escapes to infinity in the corner a < ~0.48 with b < ~1.05,
            // right against the chaotic band. Trimmed to a sliver, kept as an edge
            // that stutters (re-seeds), as Chua's corner is.
            chaosMin = 0.42f; chaosMax = 0.55f;
            charMin  = 1.04f; charMax  = 1.2f;
            dtBase = 0.05f;  divergeBound = 50.0f;
            simRateMin = 50.0f; simRateMax = 4000.0f;
            maxStepsPerSecond = 64.0f * kRefSampleRate;  // P class
            modScale = 0.02f; modMin = chaosMin; modMax = chaosMax;
            gainL = 1.511f; gainR = 1.870f;   // measured, characterise
            xMin = -0.6f; xRange = 1.9f; yMin = -0.7f; yRange = 1.7f;
            cvScaleX = 3.5f; cvScaleY = 4.5f;
            pitchClass = PITCH_COHERENT;
        }
        void initState(float* s) const { s[0] = 0.1f; s[1] = 0.0f; s[2] = 0.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            ds[0] = s[1] + drive;
            ds[1] = s[2];
            ds[2] = -kC * s[0] - char_ * s[1] - chaos_ * s[2] + s[0] * s[0];
        }
        bool escaped() const { return anyDiverged(3); }
        float getX() const override { return s_[0]; }
        float getY() const override { return s_[1]; }
    private:
        static constexpr float kC = 1.0f;
    };

    // Defined in src/Candidates.cpp, which only host tools build.
    constexpr int N_CANDIDATES = 3;
    extern ChaosBase* candidates[N_CANDIDATES];

}  // namespace chaos_core
