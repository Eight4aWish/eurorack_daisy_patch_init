// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// chaos_core period map — where is an algorithm PITCHED? Host build, no hardware.
//
// characterise.cpp answers "is this parameter combination bounded, and how loud".
// This answers the question that decides whether a range is musical: at each
// point on the CHAOS x CHAR plane, is the attractor on a periodic orbit (a pitch)
// or a chaotic one (noise)?
//
// Method: collect successive local maxima of the Y output -- the classic return
// map -- and count how many distinct values they settle onto, to a 0.5% tolerance.
// A period-n orbit gives n clusters; chaos gives many. This matters more than it
// sounds: a spread or variance test only finds period-1, and every interesting
// window is a period-doubling cascade, so period-2 and period-4 would read as
// noise. Counting clusters finds the whole cascade.
//
// A '!' means no local maxima were found at all: the trajectory settled to a
// fixed point. That is silence, not chaos, and not a guard trip -- Lorenz does it
// below the chaos onset near rho = 24.74, and at sigma below ~3.5.
//
// This is how Lorenz's range was set. The old 24-32 sat entirely inside the
// chaotic region, so CHAOS never crossed a bifurcation and there was no pitched
// setting anywhere on the knob -- alone among the six.
//
// Build:
//   g++ -O2 -std=c++17 -I common/chaos_core/include
//       common/chaos_core/tools/periodmap.cpp common/chaos_core/src/Registry.cpp
//       -o /tmp/periodmap && /tmp/periodmap 2
//   (one line; split here for readability)
//
// Args: <algo index> [chaosMin chaosMax charMin charMax]
//   With no range given it maps the algorithm's own declared range -- so it shows
//   what the panel can actually reach. Pass a range to explore beyond it.

#include "models.h"   // the shipping six, plus candidates with -DCHAOS_CANDIDATES

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace chaos_core;

namespace {

constexpr int  kRows = 21, kCols = 11;
constexpr long kWarm = 300000, kRun = 2500000;
constexpr size_t kMaxima = 200, kTail = 60;

// -1 = fixed point (silent) or non-finite; otherwise the period order.
int periodOrder(ChaosBase* a, float chaos, float charV) {
    a->init();
    a->setParams(chaos, a->dtBase, charV);
    for (long i = 0; i < kWarm; i++) {
        a->stepSample();
        if (!std::isfinite(a->getY())) return -1;
    }
    std::vector<float> mx;
    float p2 = 0.0f, p1 = 0.0f;
    bool have = false;
    for (long i = 0; i < kRun && mx.size() < kMaxima; i++) {
        a->stepSample();
        const float v = a->getY();
        if (!std::isfinite(v)) return -1;
        if (have && p1 > p2 && p1 > v) mx.push_back(p1);
        p2 = p1; p1 = v; have = true;
    }
    if (mx.size() < kTail) return -1;

    std::vector<float> tail(mx.end() - kTail, mx.end());
    float mean = 0.0f;
    for (float f : tail) mean += f;
    mean /= (float)tail.size();
    const float tol = 0.005f * std::fabs(mean);

    std::vector<float> clusters;
    for (float f : tail) {
        bool hit = false;
        for (float c : clusters) if (std::fabs(f - c) <= tol) { hit = true; break; }
        if (!hit) clusters.push_back(f);
    }
    return (int)clusters.size();
}

float lerp(float a, float b, int i, int n) {
    return (n <= 1) ? a : a + (b - a) * (float)i / (float)(n - 1);
}

}  // namespace

int main(int argc, char** argv) {
    const int idx = (argc > 1) ? std::atoi(argv[1]) : 0;
    if (idx < 0 || idx >= nModels()) {
        std::printf("usage: periodmap <0..%d> [chaosMin chaosMax charMin charMax]\n", nModels() - 1);
        for (int i = 0; i < nModels(); i++) std::printf("  %d  %s\n", i, model(i)->name);
        return 1;
    }
    ChaosBase* a = model(idx);
    const float cLo = (argc > 2) ? (float)std::atof(argv[2]) : a->chaosMin;
    const float cHi = (argc > 3) ? (float)std::atof(argv[3]) : a->chaosMax;
    const float hLo = (argc > 4) ? (float)std::atof(argv[4]) : a->charMin;
    const float hHi = (argc > 5) ? (float)std::atof(argv[5]) : a->charMax;

    std::printf("%s — period order at dtBase = %g\n", a->name, a->dtBase);
    std::printf("digit = period-n (PITCHED), '.' = chaotic, '!' = fixed point (silent)\n");
    std::printf("CHAOS (%s) %g..%g down, CHAR (%s) %g..%g across%s\n\n",
                a->chaosLabel, cLo, cHi, a->charLabel, hLo, hHi,
                (argc > 2) ? "  [range overridden]" : "  [declared range]");

    std::printf("  %7s ", "chaos");
    for (int j = 0; j < kCols; j++) std::printf("%5.2f", lerp(hLo, hHi, j, kCols));
    std::printf("\n");

    int pitched = 0, silent = 0;
    for (int i = 0; i < kRows; i++) {
        const float chaos = lerp(cLo, cHi, i, kRows);
        std::printf("  %7.2f ", chaos);
        for (int j = 0; j < kCols; j++) {
            const int n = periodOrder(a, chaos, lerp(hLo, hHi, j, kCols));
            if (n < 0)       { std::printf("    !"); silent++; }
            else if (n <= 8) { std::printf("    %d", n); pitched++; }
            else               std::printf("    .");
        }
        std::printf("\n");
    }
    std::printf("\n  %d of %d points pitched, %d silent (fixed point)\n",
                pitched, kRows * kCols, silent);
    return 0;
}
