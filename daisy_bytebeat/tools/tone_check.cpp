// Tone macro checks: clean bypass at noon, the two halves doing what they claim,
// and — the one that matters — that a 24x drive into a wavefolder followed by a
// resonant SVF stays finite and bounded across the whole knob sweep.
#include "lofi_tone.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace multiosc;

static const float SR = 48000.f;

// A deliberately hostile source, but bounded to +/-1 exactly as the engine's
// output is — otherwise the bypass path just reports the input's own peak and
// the stability number means nothing. The DC offset mimics formula 85, which
// sits at a mean of -0.5.
static float src(int n, float dc) {
    float t = (float)n;
    float v = 0.6f * sinf(2.f * 3.14159265f * 220.f * t / SR)
            + 0.4f * sinf(2.f * 3.14159265f * 3700.f * t / SR);
    if (((n * 2654435761u) >> 28) & 1) v += 0.3f;   // cheap deterministic hash
    v += dc;
    return v > 1.f ? 1.f : (v < -1.f ? -1.f : v);
}

int main() {
    printf("Clean bypass at noon (must be bit-identical):\n");
    {
        LofiTone tn; tn.Init(SR); tn.SetMacro(0.5f);
        LofiTone::State st; bool same = true;
        for (int n = 0; n < 4096; n++) { float in = src(n, -0.5f); if (tn.Process(in, st) != in) same = false; }
        printf("  IsClean=%d  output identical: %s\n", (int)tn.IsClean(), same ? "yes" : "NO (bug)");
    }

    printf("\nBrightness across the sweep (mean |x[n]-x[n-1]|, higher = brighter):\n");
    for (float k : {0.00f, 0.20f, 0.40f, 0.50f, 0.60f, 0.80f, 1.00f}) {
        LofiTone tn; tn.Init(SR); tn.SetMacro(k);
        LofiTone::State st;
        double d = 0, rms = 0; float prev = 0;
        for (int n = 0; n < 48000; n++) {
            float o = tn.IsClean() ? src(n, -0.5f) : tn.Process(src(n, -0.5f), st);
            if (n) d += std::fabs(o - prev);
            prev = o; rms += (double)o * o;
        }
        printf("  knob %.2f  %-5s  d=%.5f  rms=%.4f\n", k,
               tn.IsClean() ? "clean" : (k < 0.5f ? "CCW" : "CW"),
               d / 47999, std::sqrt(rms / 48000));
    }

    printf("\nStability sweep: 101 knob positions x 1 s of hostile full-scale input\n");
    int    bad = 0;
    double worst = 0;
    float  worst_k = 0;
    for (int i = 0; i <= 100; i++) {
        const float k = (float)i / 100.f;
        LofiTone tn; tn.Init(SR); tn.SetMacro(k);
        LofiTone::State st;
        for (int n = 0; n < 48000; n++) {
            const float o = tn.Process(src(n, -0.5f), st);
            if (!std::isfinite(o)) { bad++; break; }
            if (std::fabs(o) > worst) { worst = std::fabs(o); worst_k = k; }
        }
    }
    printf("  non-finite outputs: %d (must be 0)\n", bad);
    printf("  peak |out| = %.4f at knob %.2f (input is bounded to 1.0)\n", worst, worst_k);

    // Knob jumps mid-stream must not click or blow up: the SVF and S&H carry
    // state across a SetMacro, which is what happens when you sweep the pot.
    printf("\nLive knob sweep with carried state:\n");
    {
        LofiTone tn; tn.Init(SR); LofiTone::State st;
        double peak = 0; int nonfinite = 0;
        for (int n = 0; n < 480000; n++) {                  // 10 s
            if (n % 48 == 0) tn.SetMacro((float)((n / 48) % 1000) / 999.f);
            const float o = tn.Process(src(n, -0.5f), st);
            if (!std::isfinite(o)) nonfinite++;
            if (std::fabs(o) > peak) peak = std::fabs(o);
        }
        printf("  non-finite: %d   peak |out| = %.4f\n", nonfinite, peak);
    }
    return 0;
}
