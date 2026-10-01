#pragma once
// OdeModel: one RK4 stepper shared by the models in Bank2.h and Candidates.h.
// Each model supplies only its derivative, its outputs and how it escapes; the
// TAME drive is added to the first state variable's derivative, as in every
// model in Attractors.h, where RK4 is written out by hand per system.

#include <math.h>
#include "chaos_core/ChaosBase.h"

namespace chaos_core {

    // ─── OdeModel: RK4 over N state variables ─────────────────────────────────
    // Derived supplies:
    //   void deriv(const float* s, float drive, float* ds) const;
    //   bool escaped() const;          // after a step: re-seed?
    //   void initState(float* s) const;
    // and may override wrap() to keep angle variables in range.
    template <int N, class Derived>
    class OdeModel : public ChaosBase {
    public:
        static_assert(N <= ChaosBase::kMaxState, "state too large for TAME's snapshot");
        void init() override { self().initState(s_); }
        int  saveState(float* s) const override { for (int i = 0; i < N; i++) s[i] = s_[i]; return N; }
        void loadState(const float* s) override { for (int i = 0; i < N; i++) s_[i] = s[i]; }
        void setParams(float chaos, float rate, float charV) override {
            chaos_ = chaos; dt_ = rate; char_ = charV;
        }
        void stepSample() override {
            float k1[N], k2[N], k3[N], k4[N], t[N];
            const Derived& d = self();
            d.deriv(s_, tameD0, k1);
            for (int i = 0; i < N; i++) t[i] = s_[i] + 0.5f * dt_ * k1[i];
            d.deriv(t, tameDH, k2);
            for (int i = 0; i < N; i++) t[i] = s_[i] + 0.5f * dt_ * k2[i];
            d.deriv(t, tameDH, k3);
            for (int i = 0; i < N; i++) t[i] = s_[i] + dt_ * k3[i];
            d.deriv(t, tameD1, k4);
            for (int i = 0; i < N; i++)
                s_[i] += dt_ / 6.0f * (k1[i] + 2.0f * k2[i] + 2.0f * k3[i] + k4[i]);
            self().wrap();
            if (self().escaped()) reseed();
        }
    protected:
        void wrap() {}
        bool anyDiverged(int n) const {
            for (int i = 0; i < n; i++) if (diverged(s_[i])) return true;
            return false;
        }
        // Keep an angle in [-pi, pi), and blend one along the short way round.
        static float wrapPi(float a) {
            if (a >= 3.14159265f)  a -= 6.28318531f;
            if (a < -3.14159265f)  a += 6.28318531f;
            return a;
        }
        static float shortWay(float from, float to) { return wrapPi(to - from); }

        float s_[N] = {};
        float chaos_ = 0.0f, char_ = 0.0f, dt_ = 0.01f;
    private:
        Derived&       self()       { return static_cast<Derived&>(*this); }
        const Derived& self() const { return static_cast<const Derived&>(*this); }
    };

}  // namespace chaos_core
