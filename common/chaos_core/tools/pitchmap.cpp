// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// chaos_core pitch map — how steady is an algorithm's pitch? Host build, no hardware.
//
// periodmap.cpp asks whether a point is periodic. This asks the question TAME needs
// answered (docs/SECRET.md, section 2): what is the attractor's natural rotation
// frequency at each point on the CHAOS x CHAR plane, and how much does it jitter?
//
//   f_nat   upward zero crossings of X (DC removed) per unit of SIMULATED time.
//           Pitch in Hz is f_nat x simRate, so simRate = f_target / f_nat is the
//           scale-compensation table for coherent systems.
//   jitter  coefficient of variation of the crossing intervals. ~0 is periodic;
//           Rossler-type spirals stay near 0.1-0.2 even when fully chaotic (a pitch
//           with roughness); ~0.3 and above has no usable pitch.
//
// First measurement at mid CHAR (2026-09-30): Rossler f_nat 0.175-0.179 across the
// whole CHAOS range (+-1.2%), Van der Pol periodic but drifting 1.35 octaves with mu,
// Duffing locked to its drive (omega/2pi, or /3 and /5 in subharmonic windows),
// Lorenz and Chua erratic outside their periodic windows.
//
// Build:
//   g++ -O2 -std=c++17 -I common/chaos_core/include
//       common/chaos_core/tools/pitchmap.cpp common/chaos_core/src/Registry.cpp
//       -o /tmp/pitchmap && /tmp/pitchmap 0
//   (one line; split here for readability)
//
// Args: <algo index> [chaosMin chaosMax charMin charMax]
//   Output: two grids, f_nat then jitter. '!' = no oscillation (fixed point or
//   non-finite). Integrates at dtBase/2, so the figures are the ODE's, not RK4's.

#include "chaos_core/Registry.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace chaos_core;

namespace {

constexpr int  kRows = 13, kCols = 7;
constexpr long kWarm = 100000, kRun = 400000;
constexpr size_t kMinCrossings = 10;

struct Pitch { bool ok; float fNat, jitter; };

Pitch measure(ChaosBase* a, float chaos, float charV) {
    const float dt = a->dtBase * 0.5f;
    a->init();
    a->setParams(chaos, dt, charV);
    double dc = 0.0;
    for (long i = 0; i < kWarm; i++) {
        a->stepSample();
        const float x = a->getX();
        if (!std::isfinite(x)) return {false, 0, 0};
        dc += x;
    }
    dc /= (double)kWarm;

    std::vector<double> iv;
    double t = 0.0, lastT = -1.0;
    float prev = a->getX() - (float)dc;
    for (long i = 0; i < kRun; i++) {
        a->stepSample();
        t += dt;
        const float x = a->getX() - (float)dc;
        if (!std::isfinite(x)) return {false, 0, 0};
        if (prev < 0.0f && x >= 0.0f) {
            if (lastT >= 0.0) iv.push_back(t - lastT);
            lastT = t;
        }
        prev = x;
    }
    if (iv.size() < kMinCrossings) return {false, 0, 0};

    double m = 0.0;
    for (double v : iv) m += v;
    m /= (double)iv.size();
    double s = 0.0;
    for (double v : iv) s += (v - m) * (v - m);
    s = std::sqrt(s / (double)iv.size());
    return {true, (float)(1.0 / m), (float)(s / m)};
}

float lerp(float a, float b, int i, int n) {
    return (n <= 1) ? a : a + (b - a) * (float)i / (float)(n - 1);
}

}  // namespace

int main(int argc, char** argv) {
    const int idx = (argc > 1) ? std::atoi(argv[1]) : 0;
    if (idx < 0 || idx >= N_ALGOS) {
        std::printf("usage: pitchmap <0..%d> [chaosMin chaosMax charMin charMax]\n", N_ALGOS - 1);
        for (int i = 0; i < N_ALGOS; i++) std::printf("  %d  %s\n", i, algos[i]->name);
        return 1;
    }
    ChaosBase* a = algos[idx];
    const float cLo = (argc > 2) ? (float)std::atof(argv[2]) : a->chaosMin;
    const float cHi = (argc > 3) ? (float)std::atof(argv[3]) : a->chaosMax;
    const float hLo = (argc > 4) ? (float)std::atof(argv[4]) : a->charMin;
    const float hHi = (argc > 5) ? (float)std::atof(argv[5]) : a->charMax;

    static Pitch grid[kRows][kCols];
    for (int i = 0; i < kRows; i++)
        for (int j = 0; j < kCols; j++)
            grid[i][j] = measure(a, lerp(cLo, cHi, i, kRows), lerp(hLo, hHi, j, kCols));

    std::printf("%s — natural frequency per unit simulated time, and jitter\n", a->name);
    std::printf("CHAOS (%s) %g..%g down, CHAR (%s) %g..%g across%s\n",
                a->chaosLabel, cLo, cHi, a->charLabel, hLo, hHi,
                (argc > 2) ? "  [range overridden]" : "  [declared range]");

    for (int pass = 0; pass < 2; pass++) {
        std::printf("\n%s\n  %7s ", pass == 0 ? "f_nat" : "jitter (CV of period)", "chaos");
        for (int j = 0; j < kCols; j++) std::printf("%8.3g", lerp(hLo, hHi, j, kCols));
        std::printf("\n");
        for (int i = 0; i < kRows; i++) {
            std::printf("  %7.3g ", lerp(cLo, cHi, i, kRows));
            for (int j = 0; j < kCols; j++) {
                const Pitch& p = grid[i][j];
                if (!p.ok) std::printf("%8s", "!");
                else       std::printf("%8.4f", pass == 0 ? p.fNat : p.jitter);
            }
            std::printf("\n");
        }
    }

    float lo = 1e30f, hi = 0.0f;
    int coherent = 0, total = 0;
    for (int i = 0; i < kRows; i++)
        for (int j = 0; j < kCols; j++) {
            const Pitch& p = grid[i][j];
            if (!p.ok) continue;
            total++;
            if (p.fNat < lo) lo = p.fNat;
            if (p.fNat > hi) hi = p.fNat;
            if (p.jitter < 0.25f) coherent++;
        }
    if (total > 0)
        std::printf("\n  f_nat spans %.2f octaves; %d of %d oscillating points have jitter < 0.25\n",
                    std::log2(hi / lo), coherent, total);
    return 0;
}
