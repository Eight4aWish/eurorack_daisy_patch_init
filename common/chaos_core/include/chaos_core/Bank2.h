#pragma once
// Bank 2: six systems chosen by ear on 2026-10-01 from nine measured candidates
// (docs/SECRET.md, section 3). Each was characterised, mapped with periodmap,
// given pitch tables (PitchTablesBank2.h) and passed tametest before promotion.
// They share OdeModel's RK4 stepper; Attractors.h holds bank 1.

#include <math.h>
#include "chaos_core/OdeModel.h"
#include "chaos_core/PitchTablesBank2.h"

namespace chaos_core {

    // ─── Driven damped pendulum ───────────────────────────────────────────────
    // theta'' + gamma theta' + sin theta = g cos(phi),  phi' = omega
    // Baker & Gollub's set: gamma = 0.5, omega = 2/3; g 0.9 period-1, 1.07
    // period-2, 1.15 chaotic, 1.35 period-1, 1.45 period-2, 1.5 chaotic.
    // CHAOS = g (drive), CHAR = omega (drive frequency, against the pendulum's own
    // small-swing frequency of 1/2pi): sets libration versus rotation.
    // Out: X = sin(theta) (rotation wraps, so not theta), Y = angular velocity.
    class ChaosPendulum : public OdeModel<3, ChaosPendulum> {
    public:
        ChaosPendulum() {
            name = "PENDULUM"; chaosLabel = "g"; charLabel = "w";
            chaosMin = 0.9f;  chaosMax = 1.55f;
            charMin  = 0.4f;  charMax  = 0.8f;    // measured: chaos 0.44-0.72, period-1 above ~0.76
            dtBase = 0.1f;   divergeBound = 100.0f;
            simRateMin = 100.0f; simRateMax = 10000.0f;
            maxStepsPerSecond = 150000.0f;   // measured on the Alchemy Lab (400 MHz, 2026-10-01): TAME 1 at the cap <= 65% of a block; ~1675 cyc/step (sin, cos)
            modScale = 0.1f;  modMin = chaosMin; modMax = chaosMax;
            gainL = 1.472f; gainR = 0.674f;   // measured, characterise
            xMin = -1.0f; xRange = 2.0f; yMin = -3.0f; yRange = 6.0f;
            cvScaleX = 4.5f; cvScaleY = 1.5f;
            pitchClass = PITCH_FORCED;   // pitch is the drive's, like Duffing
            pitchGrid  = &kPitchGrid_PENDULUM;
        }
        float naturalFreq(float, float charV) const override { return charV * 0.15915494f; }
        void initState(float* s) const { s[0] = 0.1f; s[1] = 0.0f; s[2] = 0.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            ds[0] = s[1] + drive;
            ds[1] = -kGamma * s[1] - sinf(s[0]) + chaos_ * cosf(s[2]);
            ds[2] = char_;
        }
        void wrap() { s_[0] = wrapPi(s_[0]); if (s_[2] > 6.28318531f) s_[2] -= 6.28318531f; }
        bool escaped() const { return nonFinite(s_[0]) || diverged(s_[1]) || nonFinite(s_[2]); }
        void blendState(const float* snap, float w) override {
            s_[0] = wrapPi(s_[0] + w * shortWay(s_[0], snap[0]));
            s_[1] += w * (snap[1] - s_[1]);
            s_[2] = wrapPi(s_[2] + w * shortWay(s_[2], snap[2]));
            if (s_[2] < 0.0f) s_[2] += 6.28318531f;
        }
        float getX() const override { return sinf(s_[0]); }
        float getY() const override { return s_[1]; }
    private:
        static constexpr float kGamma = 0.5f;
    };

    // ─── Lorenz–Lü–Chen unified system ────────────────────────────────────────
    // Lü, Chen, Cheng & Celikovsky (2002): one parameter alpha bridges the three.
    //   dx = (25a + 10)(y - x)
    //   dy = (r - 35a) x - x z + (29a - 1) y        (r = 28 in the paper)
    //   dz = x y - (a + 8)/3 z
    // alpha 0 is Lorenz, 0.8 Lü, 1 Chen; chaotic throughout. CHAOS = alpha (the
    // morph), CHAR = r, the paper's 28 opened up as a rho-like control.
    class ChaosUnified : public OdeModel<3, ChaosUnified> {
    public:
        ChaosUnified() {
            name = "LORENZ LU CHEN"; chaosLabel = "a"; charLabel = "r";
            chaosMin = 0.0f;  chaosMax = 1.0f;
            // r 24-34 was chaos everywhere, like Lorenz before its range was
            // widened. To 130 it crosses into a period-doubling cascade at every
            // alpha: around 50 on the Chen side, 90-160 on the Lorenz side.
            charMin  = 24.0f; charMax  = 130.0f;
            dtBase = 0.004f; divergeBound = 600.0f;   // z runs near r
            simRateMin = 50.0f; simRateMax = 2000.0f;
            maxStepsPerSecond = 600000.0f;   // measured on the Alchemy Lab (400 MHz, 2026-10-01): TAME 1 at the cap <= 65% of a block; ~432 cyc/step
            modScale = 0.1f;  modMin = chaosMin; modMax = chaosMax;
            gainL = 0.0505f; gainR = 0.0691f;   // measured, characterise
            xMin = -45.0f; xRange = 92.0f; yMin = -86.0f; yRange = 166.0f;
            cvScaleX = 0.10f; cvScaleY = 0.055f;
            pitchClass = PITCH_INCOHERENT;   // lobe switching, as Lorenz
            pitchGrid  = &kPitchGrid_LORENZ_LU_CHEN;
        }
        void initState(float* s) const { s[0] = 1.0f; s[1] = 1.0f; s[2] = 20.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            const float a = chaos_;
            ds[0] = (25.0f * a + 10.0f) * (s[1] - s[0]) + drive;
            ds[1] = (char_ - 35.0f * a) * s[0] - s[0] * s[2] + (29.0f * a - 1.0f) * s[1];
            ds[2] = s[0] * s[1] - (a + 8.0f) * (1.0f / 3.0f) * s[2];
        }
        bool escaped() const { return anyDiverged(3); }
        float getX() const override { return s_[0]; }
        float getY() const override { return s_[2] - char_; }   // centred, as Lorenz's z - rho
    };

    // ─── Moore–Spiegel ────────────────────────────────────────────────────────
    // Thermally excited oscillator (1966): x''' = -x'' - (T - R + R x^2) x' - T x
    // As a system: dx = y, dy = z, dz = -z - (T - R + R x^2) y - T x.
    // CHAOS = R, CHAR = T.
    class ChaosMooreSpiegel : public OdeModel<3, ChaosMooreSpiegel> {
    public:
        ChaosMooreSpiegel() {
            name = "MOORE SPIEGEL"; chaosLabel = "R"; charLabel = "T";
            chaosMin = 10.0f; chaosMax = 40.0f;
            charMin  = 4.0f;  charMax  = 9.0f;
            dtBase = 0.01f;  divergeBound = 200.0f;
            simRateMin = 50.0f; simRateMax = 4000.0f;
            maxStepsPerSecond = 450000.0f;   // measured on the Alchemy Lab (400 MHz, 2026-10-01): TAME 1 at the cap <= 65% of a block; ~567 cyc/step
            modScale = 1.0f;  modMin = chaosMin; modMax = chaosMax;
            gainL = 0.599f; gainR = 0.195f;   // measured, characterise
            xMin = -2.8f; xRange = 5.6f; yMin = -11.3f; yRange = 22.6f;
            cvScaleX = 1.6f; cvScaleY = 0.4f;
            pitchClass = PITCH_COHERENT;
            pitchGrid  = &kPitchGrid_MOORE_SPIEGEL;
        }
        void initState(float* s) const { s[0] = 0.1f; s[1] = 0.0f; s[2] = 0.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            ds[0] = s[1] + drive;
            ds[1] = s[2];
            ds[2] = -s[2] - (char_ - chaos_ + chaos_ * s[0] * s[0]) * s[1] - char_ * s[0];
        }
        bool escaped() const { return anyDiverged(3); }
        float getX() const override { return s_[0]; }
        float getY() const override { return s_[1]; }
    };

    // ─── Forced Brusselator ───────────────────────────────────────────────────
    // dx = A + x^2 y - (B + 1) x + F cos(phi),  dy = B x - x^2 y,  phi' = omega
    // Tomita & Kai's chaotic set: A = 0.4, B = 1.2, omega 0.81, F 0.05.
    // The unforced limit cycle needs B > 1 + A^2 = 1.16. CHAOS = F (forcing),
    // CHAR = omega. Pitch is the drive's.
    class ChaosBrusselator : public OdeModel<3, ChaosBrusselator> {
    public:
        ChaosBrusselator() {
            name = "BRUSSELATOR"; chaosLabel = "F"; charLabel = "w";
            chaosMin = 0.02f; chaosMax = 0.2f;
            charMin  = 0.6f;  charMax  = 1.0f;
            dtBase = 0.1f;   divergeBound = 50.0f;
            simRateMin = 100.0f; simRateMax = 10000.0f;
            maxStepsPerSecond = 225000.0f;   // measured on the Alchemy Lab (400 MHz, 2026-10-01): TAME 1 at the cap <= 65% of a block; ~1136 cyc/step (cos)
            modScale = 0.02f; modMin = chaosMin; modMax = chaosMax;
            gainL = 1.975f; gainR = 0.914f;   // measured, characterise
            xMin = -0.3f; xRange = 1.7f; yMin = -2.2f; yRange = 2.6f;
            cvScaleX = 3.3f; cvScaleY = 2.0f;
            pitchClass = PITCH_FORCED;
            pitchGrid  = &kPitchGrid_BRUSSELATOR;
        }
        float naturalFreq(float, float charV) const override { return charV * 0.15915494f; }
        void initState(float* s) const { s[0] = kA; s[1] = kB / kA; s[2] = 0.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            const float x = s[0], y = s[1];
            ds[0] = kA + x * x * y - (kB + 1.0f) * x + chaos_ * cosf(s[2]) + drive;
            ds[1] = kB * x - x * x * y;
            ds[2] = char_;
        }
        void wrap() { if (s_[2] > 6.28318531f) s_[2] -= 6.28318531f; }
        bool escaped() const { return diverged(s_[0]) || diverged(s_[1]) || nonFinite(s_[2]); }
        void blendState(const float* snap, float w) override {
            s_[0] += w * (snap[0] - s_[0]);
            s_[1] += w * (snap[1] - s_[1]);
            s_[2] = wrapPi(s_[2] + w * shortWay(s_[2], snap[2]));
            if (s_[2] < 0.0f) s_[2] += 6.28318531f;
        }
        float getX() const override { return s_[0] - kA; }
        float getY() const override { return s_[1] - kB / kA; }
    private:
        static constexpr float kA = 0.4f, kB = 1.2f;
    };

    // ─── Chaotic Colpitts ─────────────────────────────────────────────────────
    // The single-transistor oscillator, normalised (Maggio, De Feo & Kennedy 1999):
    //   dx1 = g / (Q (1 - k)) (-n(x2) + x3)
    //   dx2 = g / (Q k) x3
    //   dx3 = -Q k (1 - k) / g (x1 + x2) - x3 / Q
    //   n(x2) = exp(-x2) - 1    (the transistor's exponential)
    // k = 0.5 fixed. CHAOS = g (loop gain), CHAR = Q (resonator quality).
    class ChaosColpitts : public OdeModel<3, ChaosColpitts> {
    public:
        ChaosColpitts() {
            name = "COLPITTS"; chaosLabel = "g"; charLabel = "Q";
            chaosMin = 2.0f;  chaosMax = 6.0f;
            charMin  = 1.0f;  charMax  = 3.0f;
            dtBase = 0.02f;  divergeBound = 400.0f;    // x2 reaches ~66 where bounded
            simRateMin = 50.0f; simRateMax = 4000.0f;
            maxStepsPerSecond = 225000.0f;   // measured on the Alchemy Lab (400 MHz, 2026-10-01): TAME 1 at the cap <= 65% of a block; ~1130 cyc/step (exp)
            modScale = 0.3f;  modMin = chaosMin; modMax = chaosMax;
            gainL = 0.0596f; gainR = 0.462f;   // measured, characterise
            xMin = -4.0f; xRange = 71.0f; yMin = -5.6f; yRange = 13.7f;
            cvScaleX = 0.07f; cvScaleY = 0.6f;
            pitchClass = PITCH_COHERENT;
            pitchGrid  = &kPitchGrid_COLPITTS;
        }
        void initState(float* s) const { s[0] = 0.1f; s[1] = 0.0f; s[2] = 0.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            const float g = chaos_, Q = char_;
            float e = -s[1]; if (e > 30.0f) e = 30.0f;   // exp overflows past ~88
            const float n = expf(e) - 1.0f;
            ds[0] = g / (Q * (1.0f - kK)) * (-n + s[2]) + drive;
            ds[1] = g / (Q * kK) * s[2];
            ds[2] = -Q * kK * (1.0f - kK) / g * (s[0] + s[1]) - s[2] / Q;
        }
        bool escaped() const { return anyDiverged(3); }
        float getX() const override { return s_[1]; }
        float getY() const override { return s_[2]; }
    private:
        static constexpr float kK = 0.5f;
    };

    // ─── Hindmarsh–Rose neuron ────────────────────────────────────────────────
    // dx = y - x^3 + 3 x^2 - z + I,  dy = 1 - 5 x^2 - y,  dz = r (4 (x + 1.6) - z)
    // Spikes (fast) in bursts (slow, 1/r). I 1.3 regular bursting, about 3.25
    // chaotic bursting, above 3.5 tonic spiking. CHAOS = I, CHAR = r.
    // The spike rate is the pitch; the bursts are a rhythm underneath it.
    class ChaosHindmarshRose : public OdeModel<3, ChaosHindmarshRose> {
    public:
        ChaosHindmarshRose() {
            name = "HINDMARSH ROSE"; chaosLabel = "I"; charLabel = "r";
            chaosMin = 1.35f; chaosMax = 4.0f;    // measured: rests, silent, below ~1.3
            charMin  = 0.001f; charMax = 0.02f;
            dtBase = 0.02f;  divergeBound = 200.0f;   // y reaches ~-17 where bounded
            simRateMin = 50.0f; simRateMax = 4000.0f;
            maxStepsPerSecond = 550000.0f;   // measured on the Alchemy Lab (400 MHz, 2026-10-01): TAME 1 at the cap <= 65% of a block; ~472 cyc/step
            modScale = 0.3f;  modMin = chaosMin; modMax = chaosMax;
            gainL = 0.549f; gainR = 0.173f;   // measured, characterise (centred outputs)
            xMin = -1.0f; xRange = 4.2f; yMin = -8.8f; yRange = 17.6f;
            cvScaleX = 2.2f; cvScaleY = 0.3f;
            pitchClass = PITCH_INCOHERENT;
            pitchGrid  = &kPitchGrid_HINDMARSH_ROSE;
        }
        void initState(float* s) const { s[0] = -1.6f; s[1] = -10.0f; s[2] = 2.0f; }
        void deriv(const float* s, float drive, float* ds) const {
            const float x = s[0];
            ds[0] = s[1] - x * x * x + 3.0f * x * x - s[2] + chaos_ + drive;
            ds[1] = 1.0f - 5.0f * x * x - s[1];
            ds[2] = char_ * (4.0f * (x + 1.6f) - s[2]);
        }
        bool escaped() const { return anyDiverged(3); }
        // Centred on their measured means: the membrane rests near -0.93 and only
        // spikes upward, so uncentred the output DC blocker pushed the spikes
        // past full scale (10,795 clipped samples in the first audition).
        float getX() const override { return s_[0] + 0.925f; }
        float getY() const override { return s_[1] + 7.9f; }
    };

}  // namespace chaos_core
