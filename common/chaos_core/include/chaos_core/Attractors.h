#pragma once
// The six continuous-ODE attractors, each integrated with RK4 in stepSample().
// Constructors carry the per-algorithm metadata (parameter ranges, safe step
// size, oversampling ceiling, audio gains, plot window, CV scaling); the
// platform layer reads those rather than hard-coding any of it.

#include "chaos_core/ChaosBase.h"

namespace chaos_core {

    // ─── ChaosRossler ─────────────────────────────────────────────────────────────
    // dx = -y - z,  dy = x + a*y,  dz = b + z*(x - c)
    // CHAOS = c (bifurcation, 2–8),  CHAR = a (spiral tightness, 0.1–0.4)
    class ChaosRossler : public ChaosBase {
    public:
        ChaosRossler() {
            name       = "ROSSLER";
            chaosLabel = "c"; charLabel = "a";
            chaosMin   = 2.0f;   chaosMax = 8.0f;
            simRateMin = 88.2f;  simRateMax = 4410.0f;   // was dt 0.002-0.1 per sample at 44.1k
            dtBase     = 0.1f;
            maxStepsPerSecond = 64.0f * kRefSampleRate;   // ~85 cyc/step, 64 steps/sample at 44.1k
            divergeBound  = 200.0f;   // outputs reach ~13 where stable
            // charMax was 0.4, where Rossler has no bounded attractor for c >= 3:
            // it escapes to infinity. Not a step-size problem — it escapes at dt
            // 500x smaller too — so the range itself has to exclude it. 0.36
            // leaves margin below the ~0.385 threshold.
            charMin    = 0.1f;   charMax  = 0.36f;
            modScale   = 1.0f;
            modMin = 1.5f;    modMax = 10.0f;   // escapes below c~1.25, stable to ~10.5
            gainL      = 0.144f; gainR    = 0.150f;   // level-matched, see ChaosBase
            xMin       = -11.0f; xRange   = 24.0f;
            yMin       = -11.0f; yRange   = 22.0f;
            cvScaleX   = 0.50f;  cvScaleY = 0.50f;
        }
        void init() override { x_ = 0.1f; y_ = 0.0f; z_ = 0.0f; }
        void setParams(float chaos, float rate, float charV) override {
            c_ = chaos; dt_ = rate; a_ = charV;
        }
        void stepSample() override {
            float dx1 = -y_ - z_;
            float dy1 = x_ + a_*y_;
            float dz1 = b_ + z_*(x_ - c_);
            float x2 = x_ + 0.5f*dt_*dx1, y2 = y_ + 0.5f*dt_*dy1, z2 = z_ + 0.5f*dt_*dz1;
            float dx2 = -y2 - z2, dy2 = x2 + a_*y2, dz2 = b_ + z2*(x2 - c_);
            float x3 = x_ + 0.5f*dt_*dx2, y3 = y_ + 0.5f*dt_*dy2, z3 = z_ + 0.5f*dt_*dz2;
            float dx3 = -y3 - z3, dy3 = x3 + a_*y3, dz3 = b_ + z3*(x3 - c_);
            float x4 = x_ + dt_*dx3, y4 = y_ + dt_*dy3, z4 = z_ + dt_*dz3;
            float dx4 = -y4 - z4, dy4 = x4 + a_*y4, dz4 = b_ + z4*(x4 - c_);
            x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
            y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
            z_ += dt_/6.0f*(dz1 + 2*dz2 + 2*dz3 + dz4);
            if (diverged(x_) || diverged(y_) || diverged(z_)) init();
        }
        float getX() const override { return x_; }
        float getY() const override { return y_; }
    private:
        float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
        float a_ = 0.2f, b_ = 0.2f, c_ = 5.7f;
        float dt_ = 0.05f;
    };

    // ─── ChaosVanDerPol ───────────────────────────────────────────────────────────
    // dx/dt = y,   dy/dt = mu*(1 - x^2)*y - x
    // CHAOS = mu (nonlinearity, 0.1–8): low = near-sine, high = relaxation osc
    // Start on limit cycle (x=2, y=0) so amplitude is correct from first sample.
    class ChaosVanDerPol : public ChaosBase {
    public:
        ChaosVanDerPol() {
            name       = "VAN DER POL";
            chaosLabel = "u"; charLabel = "a";
            chaosMin   = 0.1f;   chaosMax = 8.0f;
            simRateMin = 88.2f;  simRateMax = 6615.0f;   // was dt 0.002-0.15 per sample at 44.1k
            dtBase     = 0.15f;
            maxStepsPerSecond = 64.0f * kRefSampleRate;   // ~83 cyc/step, 64 steps/sample at 44.1k
            divergeBound  = 20.0f;    // unchanged; |x| peaks ~2.0
            charMin    = 0.0f;   charMax  = 1.0f;  // reserved
            modScale   = 1.0f;
            modMin = -1.5f;   modMax = 8.5f;    // stable -1.88..8.66
            gainL      = 0.729f; gainR    = 0.230f;   // level-matched, see ChaosBase
            xMin       = -3.0f;  xRange   = 6.0f;
            yMin       = -8.0f;  yRange   = 16.0f;
            cvScaleX   = 2.00f;  cvScaleY = 0.60f;
        }
        void init() override { x_ = 2.0f; y_ = 0.0f; }
        void setParams(float chaos, float rate, float charV) override {
            mu_ = chaos;
            // Cap dt for numerical stability: VdP stiffness ∝ mu; RK4 diverges if dt*mu too large
            dt_ = fminf(rate, 1.0f / (mu_ + 2.0f));
            (void)charV;
        }
        void stepSample() override {
            float dx1 = y_;
            float dy1 = mu_*(1.0f - x_*x_)*y_ - x_;
            float x2 = x_ + 0.5f*dt_*dx1, y2 = y_ + 0.5f*dt_*dy1;
            float dx2 = y2, dy2 = mu_*(1.0f - x2*x2)*y2 - x2;
            float x3 = x_ + 0.5f*dt_*dx2, y3 = y_ + 0.5f*dt_*dy2;
            float dx3 = y3, dy3 = mu_*(1.0f - x3*x3)*y3 - x3;
            float x4 = x_ + dt_*dx3, y4 = y_ + dt_*dy3;
            float dx4 = y4, dy4 = mu_*(1.0f - x4*x4)*y4 - x4;
            x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
            y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
            if (diverged(x_) || nonFinite(y_)) init();
        }
        float getX() const override { return x_; }
        float getY() const override { return y_; }
    private:
        float x_ = 2.0f, y_ = 0.0f;
        float mu_ = 1.0f, dt_ = 0.05f;
    };

    // ─── ChaosLorenz ──────────────────────────────────────────────────────────────
    // dx = sigma*(y-x),  dy = x*(rho-z)-y,  dz = x*y - beta*z
    // CHAOS = rho (bifurcation, 24–180),  CHAR = sigma (6–14)
    // getY() returns z-rho (centred around 0) for both audio and plot.
    class ChaosLorenz : public ChaosBase {
    public:
        ChaosLorenz() {
            name       = "LORENZ";
            chaosLabel = "r"; charLabel = "s";
            // rho 24-180, not 24-32. The old range sat entirely above the chaos
            // onset near 24.74, so the CHAOS pot never crossed a bifurcation and
            // Lorenz was noise everywhere -- alone among the six in having no
            // pitched setting to find. A host sweep of the period order (distinct
            // clusters in successive maxima of z-rho) puts the usable windows well
            // above the old ceiling:
            //
            //   rho    28   99.6  100.5   148   150   160   166   216   260   350
            //   period 46      8      3     8     4     2     2     8     2     1
            //
            // 180 keeps canonical rho=28 at the bottom of the travel and lands the
            // 148-166 period-doubling cascade (8 -> 4 -> 2) across the top ~11%,
            // so the knob finally sweeps chaos into pitch the way it does on every
            // other algorithm. The narrow window at rho~100 is real but only ~1.2
            // wide -- 0.4% of pot travel here, so not findable by hand; reaching it
            // would mean giving up the cascade, which is the better window.
            //
            // Bench-found and worth keeping: rho 104.5, sigma 6.69, dt 0.0043 --
            // a plucked-string voice, and the two outputs sit an octave apart
            // because getX() is the lobe-switching x while getY() is z-rho, which
            // peaks twice per orbit. It is on a small period-2/4 island, so it is
            // as fragile as it is good. tools/periodmap.cpp maps where these are.
            chaosMin   = 24.0f;  chaosMax = 180.0f;
            simRateMin = 44.1f;  simRateMax = 132.3f;   // was dt 0.001-0.003 per sample at 44.1k
            dtBase     = 0.003f;
            maxStepsPerSecond = 64.0f * kRefSampleRate;   // ~89 cyc/step, 64 steps/sample at 44.1k
            // The guard tests raw z, which sits near rho -- not z-rho. At rho=180
            // |z| reaches ~230 and at the MOD ceiling ~290, so the old 200 would
            // have put the whole top of the range into permanent re-seeding.
            divergeBound  = 600.0f;
            charMin    = 6.0f;   charMax  = 14.0f;
            modScale   = 2.0f;
            modMin = 22.0f;   modMax = 200.0f;   // bounded to rho=200 on a host sweep
            // Re-measured over the wider rho range (tools/characterise.cpp). The
            // median peak |state| roughly doubles, so the gains halve -- which
            // means canonical rho=28 now sits ~5 dB below where it used to. That
            // is the deliberate trade: calibrating on the median keeps the high-rho
            // periodic windows, the reason the range was widened at all, clean
            // rather than saturated into the tanh.
            gainL      = 0.044f; gainR    = 0.0245f;  // level-matched, see ChaosBase
            xMin       = -58.0f; xRange   = 116.0f;
            yMin       = -120.0f; yRange  = 235.0f;  // z-rho, measured -118.7..+112.9
            cvScaleX   = 0.085f; cvScaleY = 0.041f;  // full state -> +/-4.9 V, was set for rho<=32
        }
        void init() override { x_ = 0.1f; y_ = 0.0f; z_ = 0.0f; }
        void setParams(float chaos, float rate, float charV) override {
            rho_ = chaos; dt_ = rate; sigma_ = charV;
        }
        void stepSample() override {
            float dx1 = sigma_*(y_ - x_);
            float dy1 = x_*(rho_ - z_) - y_;
            float dz1 = x_*y_ - beta_*z_;
            float x2 = x_ + 0.5f*dt_*dx1, y2 = y_ + 0.5f*dt_*dy1, z2 = z_ + 0.5f*dt_*dz1;
            float dx2 = sigma_*(y2-x2), dy2 = x2*(rho_-z2)-y2, dz2 = x2*y2-beta_*z2;
            float x3 = x_ + 0.5f*dt_*dx2, y3 = y_ + 0.5f*dt_*dy2, z3 = z_ + 0.5f*dt_*dz2;
            float dx3 = sigma_*(y3-x3), dy3 = x3*(rho_-z3)-y3, dz3 = x3*y3-beta_*z3;
            float x4 = x_ + dt_*dx3, y4 = y_ + dt_*dy3, z4 = z_ + dt_*dz3;
            float dx4 = sigma_*(y4-x4), dy4 = x4*(rho_-z4)-y4, dz4 = x4*y4-beta_*z4;
            x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
            y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
            z_ += dt_/6.0f*(dz1 + 2*dz2 + 2*dz3 + dz4);
            if (diverged(x_) || diverged(y_) || diverged(z_)) init();
        }
        float getX() const override { return x_; }
        float getY() const override { return z_ - rho_; }  // centred: audio + plot
    private:
        float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
        float sigma_ = 10.0f, beta_ = 2.667f, rho_ = 28.0f;
        float dt_ = 0.002f;
    };

    // ─── ChaosChua ────────────────────────────────────────────────────────────────
    // Chua circuit — double-scroll attractor.
    // dx = alpha*(y - x - f(x)),  dy = x - y + z,  dz = -beta*y
    // f(x): piecewise-linear Chua diode, negative slope in centre region.
    // CHAOS = alpha (8–16),  CHAR = beta (20–35)
    // Audio: x→L, z→R  (y amplitude is tiny, ~±0.5, not suitable for audio)
    class ChaosChua : public ChaosBase {
    public:
        ChaosChua() {
            name       = "CHUA";
            chaosLabel = "a"; charLabel = "b";
            chaosMin   = 8.0f;   chaosMax = 11.0f;   // double-scroll bounded ~8.5–10.5
            simRateMin = 44.1f;  simRateMax = 352.8f;   // was dt 0.001-0.008 per sample at 44.1k
            dtBase     = 0.008f;
            maxStepsPerSecond = 32.0f * kRefSampleRate;   // ~178 cyc/step - 2x a Rossler step, 32 steps/sample at 44.1k
            divergeBound  = 8.0f;     // backstop behind the b clamp in setParams
            charMin    = 12.0f;  charMax  = 16.0f;   // canonical 14.286 near centre
            modScale   = 1.0f;
            modMin = 6.0f;    modMax = 11.0f;   // pinned to chaosMax; b is clamped instead
            gainL      = 0.644f; gainR    = 0.456f;   // level-matched, see ChaosBase
            xMin       = -5.0f;  xRange   = 10.0f;
            yMin       = -6.0f;  yRange   = 12.0f;  // z axis for phase plot
            cvScaleX   = 1.30f;  cvScaleY = 1.00f;
        }
        void init() override { x_ = 0.5f; y_ = 0.0f; z_ = 0.0f; }
        // Chua's unbounded region sits *inside* its own pot range: above a~9.25
        // the attractor only stays bounded while b clears a floor that rises with
        // a (measured at dtBase: b >= 12.0 + 1.6*(a - 9.25)). Deliberately NOT
        // clamped. Past that floor the divergence guard re-seeds repeatedly,
        // which is a stuttering burst rather than a dead voice — and that texture
        // is the point of the algorithm, not a defect to engineer out. A clamp
        // was tried and removed: it silenced the bottom of the CHAR pot across
        // the top third of CHAOS, which is exactly where the character lives.
        // Leave the guard as the only backstop; it catches the unrecoverable case.
        void setParams(float chaos, float rate, float charV) override {
            alpha_ = chaos; dt_ = rate; beta_ = charV;
        }
        void stepSample() override {
            float h1 = chuaF(x_);
            float dx1 = alpha_*(y_ - x_ - h1),  dy1 = x_ - y_ + z_,  dz1 = -beta_*y_;
            float x2 = x_+0.5f*dt_*dx1, y2 = y_+0.5f*dt_*dy1, z2 = z_+0.5f*dt_*dz1;
            float h2 = chuaF(x2);
            float dx2 = alpha_*(y2 - x2 - h2), dy2 = x2 - y2 + z2, dz2 = -beta_*y2;
            float x3 = x_+0.5f*dt_*dx2, y3 = y_+0.5f*dt_*dy2, z3 = z_+0.5f*dt_*dz2;
            float h3 = chuaF(x3);
            float dx3 = alpha_*(y3 - x3 - h3), dy3 = x3 - y3 + z3, dz3 = -beta_*y3;
            float x4 = x_+dt_*dx3, y4 = y_+dt_*dy3, z4 = z_+dt_*dz3;
            float h4 = chuaF(x4);
            float dx4 = alpha_*(y4 - x4 - h4), dy4 = x4 - y4 + z4, dz4 = -beta_*y4;
            x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
            y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
            z_ += dt_/6.0f*(dz1 + 2*dz2 + 2*dz3 + dz4);
            if (diverged(x_) || nonFinite(z_)) init();
        }
        float getX() const override { return x_; }
        float getY() const override { return z_; }
    private:
        inline float chuaF(float x) const {
            if (x >  1.0f) return m1_*x + (m0_ - m1_);
            if (x < -1.0f) return m1_*x - (m0_ - m1_);
            return m0_*x;
        }
        float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
        float alpha_ = 9.0f, beta_ = 14.286f;
        // Standard double-scroll slopes: m0 inner, m1 outer — BOTH must be negative.
        // With m0=-8/7, m1=-5/7 the system has three equilibria at x=0 and x=±1.5.
        static constexpr float m0_ = -8.0f / 7.0f;
        static constexpr float m1_ = -5.0f / 7.0f;
        float dt_ = 0.005f;
    };

    // ─── ChaosDuffing ─────────────────────────────────────────────────────────────
    // Forced nonlinear oscillator — double-well potential with periodic drive.
    // Autonomous 3-variable form: track phase φ = ω·t as a state variable.
    // dx = y,   dy = -δy - αx - βx³ + γcos(φ),   dφ = ω
    // α=-1, β=1 (double-well), δ=0.3 (damping) — fixed.
    // CHAOS = γ (drive amplitude, 0.1–0.8): low = periodic, high = chaotic
    // CHAR  = ω (drive frequency, 0.8–1.4): sets the base pitch
    // Audio: x→L, y→R. Frequency ≈ ω·dt·44100 / 2π Hz.
    class ChaosDuffing : public ChaosBase {
    public:
        ChaosDuffing() {
            name       = "DUFFING";
            chaosLabel = "g"; charLabel  = "w";
            chaosMin   = 0.1f;   chaosMax = 0.8f;
            simRateMin = 220.5f;  simRateMax = 4410.0f;   // was dt 0.005-0.1 per sample at 44.1k
            dtBase     = 0.1f;
            maxStepsPerSecond = 8.0f * kRefSampleRate;   // ~543 cyc/step - 3x cosf, 6x a Rossler step, 8 steps/sample at 44.1k
            divergeBound  = 50.0f;    // outputs peak ~2.2
            charMin    = 0.8f;   charMax  = 1.4f;
            modScale   = 0.35f;
            modMin = -1.9f;   modMax = 2.8f;    // verified stable; unchanged
            gainL      = 0.992f; gainR    = 1.631f;   // level-matched, see ChaosBase
            xMin       = -2.0f;  xRange   = 4.0f;
            yMin       = -2.5f;  yRange   = 5.0f;
            cvScaleX   = 3.00f;  cvScaleY = 2.50f;
        }
        void init() override { x_ = 1.0f; y_ = 0.0f; phi_ = 0.0f; }
        void setParams(float chaos, float rate, float charV) override {
            gamma_ = chaos; dt_ = rate; omega_ = charV;
        }
        void stepSample() override {
            float c1 = cosf(phi_);
            float dx1 = y_;
            float dy1 = -delta_*y_ - alpha_*x_ - beta_*x_*x_*x_ + gamma_*c1;
            float x2 = x_+0.5f*dt_*dx1, y2 = y_+0.5f*dt_*dy1;
            float p2 = phi_ + 0.5f*dt_*omega_;
            float c2 = cosf(p2);
            float dx2 = y2;
            float dy2 = -delta_*y2 - alpha_*x2 - beta_*x2*x2*x2 + gamma_*c2;
            float x3 = x_+0.5f*dt_*dx2, y3 = y_+0.5f*dt_*dy2;
            // p3 = p2 (midpoint forcing phase is the same for both RK4 k2 and k3 stages)
            float c3 = c2;
            float dx3 = y3;
            float dy3 = -delta_*y3 - alpha_*x3 - beta_*x3*x3*x3 + gamma_*c3;
            float x4 = x_+dt_*dx3, y4 = y_+dt_*dy3;
            float p4 = phi_ + dt_*omega_;
            float c4 = cosf(p4);
            float dx4 = y4;
            float dy4 = -delta_*y4 - alpha_*x4 - beta_*x4*x4*x4 + gamma_*c4;
            x_   += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
            y_   += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
            phi_ += dt_*omega_;
            if (phi_ > 6.28318f) phi_ -= 6.28318f;  // keep phi in [0, 2π)
            if (diverged(x_) || diverged(y_)) init();
        }
        float getX() const override { return x_; }
        float getY() const override { return y_; }
    private:
        float x_ = 1.0f, y_ = 0.0f, phi_ = 0.0f;
        float gamma_ = 0.4f, omega_ = 1.2f, dt_ = 0.05f;
        static constexpr float alpha_ = -1.0f;  // double-well: negative linear term
        static constexpr float beta_  =  1.0f;  // positive cubic term
        static constexpr float delta_ =  0.3f;  // damping
    };

    // ─── ChaosCoupledRossler ──────────────────────────────────────────────────────
    // Two Rössler systems with symmetric x-coupling.
    // dx1 = -y1 - z1 + k(x2-x1),   dy1 = x1 + a·y1,   dz1 = b + z1(x1-c)
    // dx2 = -y2 - z2 + k(x1-x2),   dy2 = x2 + a·y2,   dz2 = b + z2(x2-c)
    // CHAOS = c (bifurcation, 2–8, shared), CHAR = k (coupling, 0.0–0.5)
    // At low k: two detuned oscillators beating. At high k: synchronise.
    // Oscillators start at different ICs to ensure phase diversity.
    // Audio: x1→L, x2→R — true stereo output.
    class ChaosCoupledRossler : public ChaosBase {
    public:
        ChaosCoupledRossler() {
            name       = "CPLROSSLER";
            chaosLabel = "c"; charLabel  = "k";
            chaosMin   = 2.0f;   chaosMax = 8.0f;
            simRateMin = 88.2f;  simRateMax = 4410.0f;   // was dt 0.002-0.1 per sample at 44.1k
            dtBase     = 0.1f;
            maxStepsPerSecond = 32.0f * kRefSampleRate;   // ~178 cyc/step - two coupled systems, 32 steps/sample at 44.1k
            divergeBound  = 200.0f;   // outputs reach ~16
            charMin    = 0.0f;   charMax  = 0.5f;
            modScale   = 1.0f;
            modMin = 1.0f;    modMax = 13.0f;   // escapes below c~0.5, stable to ~13.75
            gainL      = 0.146f; gainR    = 0.146f;   // level-matched, see ChaosBase
            xMin       = -13.0f; xRange   = 26.0f;
            yMin       = -11.0f; yRange   = 22.0f;
            cvScaleX   = 0.45f;  cvScaleY = 0.45f;
        }
        void init() override {
            x1_=0.1f; y1_=0.0f; z1_=0.0f;
            x2_=0.5f; y2_=0.2f; z2_=0.0f;  // offset IC for phase diversity
        }
        void setParams(float chaos, float rate, float charV) override {
            c_ = chaos; dt_ = rate; k_ = charV;
        }
        void stepSample() override {
            // Derivatives — both oscillators coupled via x
            auto deriv = [this](float x1, float y1, float z1,
                                float x2, float y2, float z2,
                                float& dx, float& dy, float& dz) {
                dx = -y1 - z1 + k_*(x2 - x1);
                dy =  x1 + a_*y1;
                dz =  b_ + z1*(x1 - c_);
                (void)y2; (void)z2;
            };
            float dx1a, dy1a, dz1a, dx2a, dy2a, dz2a;
            deriv(x1_,y1_,z1_, x2_,y2_,z2_, dx1a,dy1a,dz1a);
            deriv(x2_,y2_,z2_, x1_,y1_,z1_, dx2a,dy2a,dz2a);

            float x1b=x1_+0.5f*dt_*dx1a, y1b=y1_+0.5f*dt_*dy1a, z1b=z1_+0.5f*dt_*dz1a;
            float x2b=x2_+0.5f*dt_*dx2a, y2b=y2_+0.5f*dt_*dy2a, z2b=z2_+0.5f*dt_*dz2a;
            float dx1b, dy1b, dz1b, dx2b, dy2b, dz2b;
            deriv(x1b,y1b,z1b, x2b,y2b,z2b, dx1b,dy1b,dz1b);
            deriv(x2b,y2b,z2b, x1b,y1b,z1b, dx2b,dy2b,dz2b);

            float x1c=x1_+0.5f*dt_*dx1b, y1c=y1_+0.5f*dt_*dy1b, z1c=z1_+0.5f*dt_*dz1b;
            float x2c=x2_+0.5f*dt_*dx2b, y2c=y2_+0.5f*dt_*dy2b, z2c=z2_+0.5f*dt_*dz2b;
            float dx1c, dy1c, dz1c, dx2c, dy2c, dz2c;
            deriv(x1c,y1c,z1c, x2c,y2c,z2c, dx1c,dy1c,dz1c);
            deriv(x2c,y2c,z2c, x1c,y1c,z1c, dx2c,dy2c,dz2c);

            float x1d=x1_+dt_*dx1c, y1d=y1_+dt_*dy1c, z1d=z1_+dt_*dz1c;
            float x2d=x2_+dt_*dx2c, y2d=y2_+dt_*dy2c, z2d=z2_+dt_*dz2c;
            float dx1d, dy1d, dz1d, dx2d, dy2d, dz2d;
            deriv(x1d,y1d,z1d, x2d,y2d,z2d, dx1d,dy1d,dz1d);
            deriv(x2d,y2d,z2d, x1d,y1d,z1d, dx2d,dy2d,dz2d);

            x1_ += dt_/6.0f*(dx1a + 2*dx1b + 2*dx1c + dx1d);
            y1_ += dt_/6.0f*(dy1a + 2*dy1b + 2*dy1c + dy1d);
            z1_ += dt_/6.0f*(dz1a + 2*dz1b + 2*dz1c + dz1d);
            x2_ += dt_/6.0f*(dx2a + 2*dx2b + 2*dx2c + dx2d);
            y2_ += dt_/6.0f*(dy2a + 2*dy2b + 2*dy2c + dy2d);
            z2_ += dt_/6.0f*(dz2a + 2*dz2b + 2*dz2c + dz2d);
            if (diverged(x1_) || diverged(y1_) || diverged(z1_) ||
                diverged(x2_) || diverged(y2_) || diverged(z2_)) init();
        }
        float getX() const override { return x1_; }
        float getY() const override { return x2_; }
    private:
        float x1_=0.1f, y1_=0.0f, z1_=0.0f;
        float x2_=0.5f, y2_=0.2f, z2_=0.0f;
        float c_=5.7f, k_=0.05f, dt_=0.05f;
        static constexpr float a_ = 0.2f;
        static constexpr float b_ = 0.2f;
    };

}  // namespace chaos_core
