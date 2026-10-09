// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// chaos_core TAME test — does the voice play the note it is asked for? Host build.
//
// Renders chaos_core::Voice through setPitch() at 48 kHz and measures the pitch
// of the L output with the McLeod pitch method (normalised square difference):
//
//   zc       1200 log2(rate / target), rate from the interpolated upward zero
//            crossings of L. The lock measure: a phase-locked voice reads 0
//            exactly, however chaotic its waveform.
//   mpm      the same from McLeod's method, the median over four windows. The
//            measure where X may cross zero more than once a cycle (Lorenz's X
//            can switch lobes twice in one). It reads a period-2 waveform --
//            alternating big and small loops -- as the octave below, which is
//            strictly its period but not what zc hears.
//   clarity  the NSDF peak height, 0..1. 1 is exactly periodic; ~0.8 is a clear
//            pitch with roughness; below ~0.6 the pitch is weak or gone.
//
// Each cell is the median |zc| and |mpm| and the mean clarity over three CHAOS
// settings (30%, 60%, 90% of the pot) at mid CHAR. McLeod's period search is
// limited to +-1.3 octaves of the target.
//
// Pass/fail (exit status 1 on any failure). Since 2026-10-09 TAME is a push and
// nothing more, so it promises no lock: how close each model gets is a finding
// and an ear decision, reported, not judged. What is checked:
//   - the reference is a cosine (cos2pi against libm);
//   - a model whose push ceiling is zero (tameDriveMax, Lorenz and Chua) plays
//     bit-identically at every TAME: the knob must leave it free, not nearly so.
//
// Build:
//   g++ -O2 -std=c++17 -I common/chaos_core/include
//       common/chaos_core/tools/tametest.cpp common/chaos_core/src/Registry.cpp
//       -o /tmp/tametest && /tmp/tametest
//   (one line; split here for readability)
//
// Args: [algo index]   (default: every algorithm)

#include "models.h"   // the shipping six, plus candidates with -DCHAOS_CANDIDATES
#include "chaos_core/Voice.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace chaos_core;

namespace {

constexpr float kSr = 48000.0f;
constexpr int   kBlock = 48;                 // 1 kHz control rate, as on hardware
constexpr int   kWin = 8192;
constexpr float kPitches[] = {55.0f, 110.0f, 220.0f, 440.0f, 880.0f};
constexpr float kTames[]   = {0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f};
constexpr float kChaosN[]  = {0.3f, 0.6f, 0.9f};

struct Est { float hz, clarity; };

// McLeod: NSDF over lags [tLo, tHi]; the first key maximum within 90% of the
// highest, refined by a parabola.
Est mpm(const float* x, int W, float tLo, float tHi) {
    std::vector<double> sq(W + 1, 0.0);
    double mean = 0.0;
    for (int i = 0; i < W; i++) mean += x[i];
    mean /= W;
    std::vector<float> y(W);
    for (int i = 0; i < W; i++) { y[i] = x[i] - (float)mean; sq[i + 1] = sq[i] + (double)y[i] * y[i]; }
    const int lo = std::max(2, (int)tLo), hi = std::min(W / 2, (int)tHi + 1);
    std::vector<float> n(hi + 2, 0.0f);
    for (int t = lo - 1; t <= hi + 1 && t < W; t++) {
        double r = 0.0;
        for (int i = 0; i + t < W; i++) r += (double)y[i] * y[i + t];
        const double m = (sq[W - t] - sq[0]) + (sq[W] - sq[t]);
        n[t] = (m > 0.0) ? (float)(2.0 * r / m) : 0.0f;
    }
    float best = 0.0f;
    for (int t = lo; t <= hi; t++)
        if (n[t] > n[t - 1] && n[t] >= n[t + 1] && n[t] > best) best = n[t];
    if (best <= 0.0f) return {0.0f, 0.0f};
    for (int t = lo; t <= hi; t++) {
        if (n[t] > n[t - 1] && n[t] >= n[t + 1] && n[t] >= 0.9f * best) {
            const float a = n[t - 1], b = n[t], c = n[t + 1];
            const float den = a - 2.0f * b + c;
            const float d = (den != 0.0f) ? 0.5f * (a - c) / den : 0.0f;
            return {kSr / ((float)t + d), b};
        }
    }
    return {0.0f, 0.0f};
}

struct Result { float cents, clarity, zcCents; };

Result play(ChaosBase* a, float chaos, float charV, float hz, float tame,
            std::vector<float>* out = nullptr) {
    Voice v;
    v.setSampleRate(kSr);
    v.setAlgo(a);
    const int warm = (int)(0.4f * kSr), len = 4 * kWin;
    std::vector<float> L(warm + len + kBlock), R(warm + len + kBlock);   // last block overhangs
    for (int i = 0; i < warm + len; i += kBlock) {
        v.setPitch(chaos, charV, hz, tame);
        v.render(&L[i], &R[i], kBlock);
    }
    if (out) out->assign(L.begin() + warm, L.begin() + warm + len);
    const float T = kSr / hz;
    std::vector<float> h, c;
    for (int w = 0; w < 4; w++) {
        const Est e = mpm(&L[warm + w * kWin], kWin, 0.4f * T, 2.5f * T);
        h.push_back(e.hz > 0.0f ? e.hz : hz * 0.4f);   // no pitch: score as far off
        c.push_back(e.clarity);
    }
    std::sort(h.begin(), h.end());
    const float med = 0.5f * (h[1] + h[2]);
    float cl = 0.0f;
    for (float x : c) cl += x;
    double t0 = -1.0, t1 = -1.0;
    int nc = 0;
    for (int i = warm + 1; i < warm + len; i++)
        if (L[i - 1] < 0.0f && L[i] >= 0.0f) {
            const double t = i - 1 + (double)L[i - 1] / (double)(L[i - 1] - L[i]);
            if (t0 < 0.0) t0 = t;
            t1 = t; nc++;
        }
    const float zcHz = (nc > 1) ? (float)((nc - 1) * kSr / (t1 - t0)) : 0.0f;
    return {1200.0f * std::log2(med / hz), cl / 4.0f,
            zcHz > 0.0f ? 1200.0f * std::log2(zcHz / hz) : 9999.0f};
}

}  // namespace

int main(int argc, char** argv) {
    int only = -1;
    if (argc > 1) only = std::atoi(argv[1]);

    // The reference cosine has to be a cosine.
    float cosErr = 0.0f;
    for (int i = 0; i < 100000; i++) {
        const float p = i / 100000.0f;
        cosErr = std::max(cosErr, std::fabs(cos2pi(p) - (float)std::cos(6.283185307 * p)));
    }
    std::printf("cos2pi max error %.2e\n", cosErr);
    int fails = (cosErr > 1e-4f) ? 1 : 0;

    for (int ai = 0; ai < nModels(); ai++) {
        if (only >= 0 && ai != only) continue;
        ChaosBase* a = model(ai);
        // This tests TAME, not the chip. maxStepsPerSecond is a per-board CPU
        // limit, measured by Secret's `make BENCH=1`. On the Alchemy Lab it puts
        // the top of some models below this test's 880 Hz (Hindmarsh-Rose ~200 Hz
        // at mid settings), where the note plateaus by design. Lifted here so a
        // failure means TAME, not the cap. Host only; restored after.
        const float cap = a->maxStepsPerSecond;
        a->maxStepsPerSecond = 1.0e9f;
        struct Restore { ChaosBase* a; float cap; ~Restore() { a->maxStepsPerSecond = cap; } } restore{a, cap};
        static const char* kClass[] = {"coherent", "forced", "incoherent"};
        std::printf("\n%s  [%s, push %.1f%%]  |zc| |mpm| cents, clarity\n  TAME ", a->name,
                    kClass[a->pitchClass], 100.0f * a->tameDriveMax);
        for (float hz : kPitches) std::printf("  %9.0f Hz  ", hz);
        std::printf("\n");
        const float charV = 0.5f * (a->charMin + a->charMax);
        if (!(a->tameDriveMax > 0.0f)) {
            // No push: TAME must change nothing at all.
            const float chaos = 0.5f * (a->chaosMin + a->chaosMax);
            std::vector<float> free, full;
            play(a, chaos, charV, 220.0f, 0.0f, &free);
            play(a, chaos, charV, 220.0f, 1.0f, &full);
            const bool same = (free == full);
            if (!same) fails++;
            std::printf("  TAME 1 against TAME 0: %s\n", same ? "bit-identical" : "DIFFERS!");
        }
        for (float tame : kTames) {
            std::printf("  %4.2f", tame);
            for (float hz : kPitches) {
                std::vector<float> cents, zcs;
                float clar = 0.0f;
                for (float cn : kChaosN) {
                    const float chaos = a->chaosMin + (a->chaosMax - a->chaosMin) * cn;
                    const Result r = play(a, chaos, charV, hz, tame);
                    cents.push_back(std::fabs(r.cents));
                    zcs.push_back(std::fabs(r.zcCents));
                    clar += r.clarity;
                }
                std::sort(cents.begin(), cents.end());
                std::sort(zcs.begin(), zcs.end());
                const float c = cents[1];
                clar /= 3.0f;
                std::printf("  %4.0f %4.0f %4.2f ", std::min(zcs[1], 9999.0f), c, clar);
            }
            std::printf("\n");
        }
    }
    std::printf("\n%s (%d failure%s)\n", fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
