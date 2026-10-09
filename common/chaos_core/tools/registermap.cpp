// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// chaos_core register map — which octave does a driven model actually sound in?
// Host build, no hardware.
//
// The driven models (PITCH_FORCED) are tuned by their drive, not by pitchmap's
// zero crossings: naturalFreq() returns the drive frequency, or a fraction of it.
// Whether that is where the ear hears the note depends on which periodic window
// each point of the pot range sits in. This plays every forced model through
// Voice::setPitch() at TAME 0 over a 9 x 7 CHAOS x CHAR grid and prints X's
// period, by McLeod's method over 0.4-7 note periods, as a multiple of the note's
// period, with the NSDF clarity in brackets. 1.00 is in tune; 2, 3, 4... are
// subharmonic windows, intervals below the note; a fraction is chaos.
//
// 2026-10-09: Duffing and the pendulum read 1 in 39 of 63 cells, so their drive is
// their register. The Brusselator read 2 in 50 of 63 while tuned to its drive, so
// its naturalFreq() was halved; it now reads 1 in 46, and 2 (its period-4 windows,
// an octave below) in 11.
//
// Build:
//   g++ -O2 -std=c++17 -I common/chaos_core/include -I common/chaos_core/tools
//       common/chaos_core/tools/registermap.cpp common/chaos_core/src/Registry.cpp
//       -o /tmp/registermap && /tmp/registermap
//   (one line; split here for readability)

#include "models.h"
#include "chaos_core/Voice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace chaos_core;
constexpr float kSr = 48000.0f; constexpr int kBlock = 48, kWin = 16384;
struct Est { float hz, clarity; };
Est mpm(const float* x, int W, float tLo, float tHi) {
    std::vector<double> sq(W + 1, 0.0); double mean = 0;
    for (int i = 0; i < W; i++) mean += x[i]; mean /= W;
    std::vector<float> y(W);
    for (int i = 0; i < W; i++) { y[i] = x[i] - (float)mean; sq[i + 1] = sq[i] + (double)y[i] * y[i]; }
    const int lo = std::max(2, (int)tLo), hi = std::min(W / 2, (int)tHi + 1);
    std::vector<float> n(hi + 2, 0.0f);
    for (int t = lo - 1; t <= hi + 1 && t < W; t++) {
        double r = 0; for (int i = 0; i + t < W; i++) r += (double)y[i] * y[i + t];
        const double m = (sq[W - t] - sq[0]) + (sq[W] - sq[t]);
        n[t] = (m > 0) ? (float)(2.0 * r / m) : 0.0f;
    }
    float best = 0; for (int t = lo; t <= hi; t++) if (n[t] > n[t-1] && n[t] >= n[t+1] && n[t] > best) best = n[t];
    if (best <= 0) return {0, 0};
    for (int t = lo; t <= hi; t++) if (n[t] > n[t-1] && n[t] >= n[t+1] && n[t] >= 0.9f * best) {
        const float a = n[t-1], b = n[t], c = n[t+1], den = a - 2*b + c, d = den != 0 ? 0.5f*(a-c)/den : 0;
        return {kSr / ((float)t + d), b};
    }
    return {0, 0};
}
int main() {
    const float hz = 110.0f, T = kSr / hz;
    for (int ai = 0; ai < nModels(); ai++) {
        ChaosBase* a = model(ai);
        if (a->pitchClass != PITCH_FORCED) continue;
        std::printf("\n%s  X period / note period (clarity)   rows CHAOS %.3g..%.3g, cols CHAR %.3g..%.3g\n",
                    a->name, a->chaosMin, a->chaosMax, a->charMin, a->charMax);
        std::vector<float> all;
        for (int r = 0; r < 9; r++) {
            const float chaos = a->chaosMin + (a->chaosMax - a->chaosMin) * r / 8.0f;
            std::printf("%7.3f ", chaos);
            for (int c = 0; c < 7; c++) {
                const float ch = a->charMin + (a->charMax - a->charMin) * c / 6.0f;
                Voice v; v.setSampleRate(kSr); v.setAlgo(a);
                const int warm = (int)(0.5f * kSr);
                std::vector<float> L(warm + kWin + kBlock), R(L.size());
                for (int i = 0; i < warm + kWin; i += kBlock) { v.setPitch(chaos, ch, hz, 0.0f); v.render(&L[i], &R[i], kBlock); }
                const Est e = mpm(&L[warm], kWin, 0.4f * T, 7.0f * T);
                const float ratio = e.hz > 0 ? hz / e.hz : 0.0f;
                if (e.hz > 0) all.push_back(ratio);
                std::printf(" %5.2f(%.2f)", ratio, e.clarity);
            }
            std::printf("\n");
        }
        std::sort(all.begin(), all.end());
        int near[8] = {0};
        for (float x : all) { int k = (int)std::lround(x); if (k >= 0 && k < 8 && std::fabs(x - k) < 0.06f) near[k]++; }
        std::printf("  median %.2f; within 0.06 of k=1..6:", all[all.size()/2]);
        for (int k = 1; k < 7; k++) std::printf(" %d:%d", k, near[k]);
        std::printf("  of %zu\n", all.size());
    }
}
