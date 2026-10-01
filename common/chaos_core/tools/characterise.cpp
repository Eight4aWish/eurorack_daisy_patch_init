// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// chaos_core characterisation harness — host build, no hardware required.
//
// Every ChaosBase subclass carries a block of constructor metadata that can
// only be arrived at by measurement: the parameter ranges that stay bounded,
// the divergence guard's headroom, the pre-tanh gains that level-match one
// algorithm against another, and the plot window. Measuring those by hand on
// the bench is what the Chua floor table and the level-matching sweep in
// common/chaos_core/README.md cost; doing it once per algorithm across a growing
// suite is the bulk of the work in adding algorithms at all.
//
// This tool does that sweep on a host compiler and prints the numbers, so the
// bench is left with the part that actually needs ears.
//
// Methodology follows README.md so the output is comparable with the values
// already in Attractors.h:
//   - 5 x 5 x 3 sweep of (chaos, char, rate) across each algorithm's own
//     pot range, at dtBase-relative rates.
//   - Each combination is warmed (transient discarded) and then measured.
//   - gain = atanh(0.90) / MEDIAN peak |state| over the sweep. The median,
//     not the maximum: calibrating on the max drags everything down, and for
//     Chua the max is its divergence spike, which is not a thing anyone plays.
//
// Guard trips are detected by watching for the exact init() state reappearing
// mid-run — stepSample() re-seeds through init() on divergence, and init()
// assigns constants, so the equality is exact rather than approximate.
//
// Build (no cmake needed):
//   g++ -O2 -I common/chaos_core/include
//       common/chaos_core/tools/characterise.cpp common/chaos_core/src/Registry.cpp
//       -o /tmp/characterise && /tmp/characterise
//   (one line; the continuations are split here only for readability)
//
// Options:
//   --verbose   per-combination divergence map for each algorithm
//   --samples N samples measured per combination (default 200000)

#include "models.h"   // the shipping six, plus candidates with -DCHAOS_CANDIDATES

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace chaos_core;

namespace {

constexpr int   kChaosSteps = 5;
constexpr int   kCharSteps  = 5;
constexpr int   kRateSteps  = 3;
constexpr float kTanhTarget = 0.90f;   // README: gain lands normal settings here

struct Combination {
    float chaos = 0.0f, charV = 0.0f, dt = 0.0f;
    float peakX = 0.0f, peakY = 0.0f;
    float minX  = 0.0f, maxX  = 0.0f;
    float minY  = 0.0f, maxY  = 0.0f;
    bool  tripped = false;
};

struct Result {
    const char*              name = "?";
    double                   nsPerStep = 0.0;
    std::vector<Combination> combos;
    int                      trips = 0;
    float medianPeakX = 0.0f, medianPeakY = 0.0f;
    float maxPeakX    = 0.0f, maxPeakY    = 0.0f;
    float obsMinX = 0.0f, obsMaxX = 0.0f;
    float obsMinY = 0.0f, obsMaxY = 0.0f;
};

float lerp(float a, float b, int i, int n) {
    return (n <= 1) ? a : a + (b - a) * (float)i / (float)(n - 1);
}

float median(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    const size_t m = v.size() / 2;
    return (v.size() % 2) ? v[m] : 0.5f * (v[m - 1] + v[m]);
}

// One (chaos, char, rate) point: warm the transient off, then measure.
Combination measure(ChaosBase* a, float chaos, float charV, float dt, long samples) {
    Combination c;
    c.chaos = chaos; c.charV = charV; c.dt = dt;

    a->init();
    const float x0 = a->getX(), y0 = a->getY();   // exact re-seed signature
    a->setParams(chaos, dt, charV);

    for (long i = 0; i < samples / 10; i++) a->stepSample();   // discard transient

    bool  first = true;
    for (long i = 0; i < samples; i++) {
        a->stepSample();
        const float x = a->getX(), y = a->getY();
        if (x == x0 && y == y0) c.tripped = true;
        if (!std::isfinite(x) || !std::isfinite(y)) { c.tripped = true; continue; }
        const float ax = std::fabs(x), ay = std::fabs(y);
        if (ax > c.peakX) c.peakX = ax;
        if (ay > c.peakY) c.peakY = ay;
        if (first) { c.minX = c.maxX = x; c.minY = c.maxY = y; first = false; }
        else {
            c.minX = std::min(c.minX, x); c.maxX = std::max(c.maxX, x);
            c.minY = std::min(c.minY, y); c.maxY = std::max(c.maxY, y);
        }
    }
    return c;
}

double timePerStep(ChaosBase* a) {
    a->init();
    a->setParams(a->chaosMin + 0.5f * (a->chaosMax - a->chaosMin), a->dtBase,
                 a->charMin  + 0.5f * (a->charMax  - a->charMin));
    const long warm = 200000, n = 4000000;
    for (long i = 0; i < warm; i++) a->stepSample();
    const auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < n; i++) a->stepSample();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / (double)n;
}

Result characterise(ChaosBase* a, long samples) {
    Result r;
    r.name      = a->name;
    r.nsPerStep = timePerStep(a);

    std::vector<float> peaksX, peaksY;
    bool first = true;

    for (int ci = 0; ci < kChaosSteps; ci++) {
        const float chaos = lerp(a->chaosMin, a->chaosMax, ci, kChaosSteps);
        for (int hi = 0; hi < kCharSteps; hi++) {
            const float charV = lerp(a->charMin, a->charMax, hi, kCharSteps);
            for (int ri = 0; ri < kRateSteps; ri++) {
                const float simRate = lerp(a->simRateMin, a->simRateMax, ri, kRateSteps);
                const float dt = a->scheduleFor(simRate, kRefSampleRate).stepDt;
                Combination c = measure(a, chaos, charV, dt, samples);
                if (c.tripped) r.trips++;
                // A tripped combination's extents describe a runaway, not a
                // voice — exclude it from gain and window fitting, but keep it
                // in the list so --verbose can show where the edges are.
                if (!c.tripped) {
                    peaksX.push_back(c.peakX);
                    peaksY.push_back(c.peakY);
                    r.maxPeakX = std::max(r.maxPeakX, c.peakX);
                    r.maxPeakY = std::max(r.maxPeakY, c.peakY);
                    if (first) {
                        r.obsMinX = c.minX; r.obsMaxX = c.maxX;
                        r.obsMinY = c.minY; r.obsMaxY = c.maxY;
                        first = false;
                    } else {
                        r.obsMinX = std::min(r.obsMinX, c.minX);
                        r.obsMaxX = std::max(r.obsMaxX, c.maxX);
                        r.obsMinY = std::min(r.obsMinY, c.minY);
                        r.obsMaxY = std::max(r.obsMaxY, c.maxY);
                    }
                }
                r.combos.push_back(c);
            }
        }
    }

    r.medianPeakX = median(peaksX);
    r.medianPeakY = median(peaksY);
    return r;
}

void printVerbose(const Result& r, ChaosBase* a) {
    std::printf("\n  %s — divergence map (. bounded, X guard tripped)\n", r.name);
    std::printf("  %-8s", "chaos\\ch");
    for (int hi = 0; hi < kCharSteps; hi++)
        std::printf(" %7.3f", lerp(a->charMin, a->charMax, hi, kCharSteps));
    std::printf("\n");
    size_t k = 0;
    for (int ci = 0; ci < kChaosSteps; ci++) {
        std::printf("  %8.3f", lerp(a->chaosMin, a->chaosMax, ci, kChaosSteps));
        for (int hi = 0; hi < kCharSteps; hi++) {
            bool any = false;
            for (int ri = 0; ri < kRateSteps; ri++) any |= r.combos[k++].tripped;
            std::printf("       %c", any ? 'X' : '.');
        }
        std::printf("\n");
    }
}

// atanh without <cmath>'s domain surprises at the edges.
float gainFor(float medianPeak) {
    if (medianPeak <= 0.0f) return 0.0f;
    return std::atanh(kTanhTarget) / medianPeak;
}

// Gains are measured quantities: a difference from the declared value is drift.
void printGain(const char* label, float current, float measured) {
    const float drift = (current > 0.0f) ? std::fabs(measured - current) / current : 1.0f;
    std::printf("    %-10s declared %9.4f   measured %9.4f   %s\n",
                label, current, measured,
                (drift <= 0.05f) ? "ok" : "** drifted **");
}

// The plot window is a framing decision, not a measurement — a window tighter
// than the trajectory crops deliberately, and constrain() flattens the
// excursions against the edge of the display. So report how much of the
// observed motion the declared window actually contains, and leave the choice
// where it belongs.
void printWindow(const char* axis, float declMin, float declRange,
                 float obsMin, float obsMax) {
    const float declMax = declMin + declRange;
    const float obsSpan = obsMax - obsMin;
    const float lo      = std::max(declMin, obsMin);
    const float hi      = std::min(declMax, obsMax);
    const float covered = (obsSpan > 0.0f) ? std::max(0.0f, hi - lo) / obsSpan : 1.0f;
    std::printf("    %-10s window [%8.2f,%8.2f]   observed [%8.2f,%8.2f]   %3.0f%% shown%s\n",
                axis, declMin, declMax, obsMin, obsMax, covered * 100.0f,
                (covered < 0.95f) ? "  (clipping)" : "");
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose  = false;
    long samples  = 200000;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--verbose"))            verbose = true;
        else if (!std::strcmp(argv[i], "--samples") && i + 1 < argc) samples = std::atol(argv[++i]);
    }

    std::printf("chaos_core characterisation — %d algorithms, %dx%dx%d sweep, "
                "%ld samples per point\n\n", nModels(), kChaosSteps, kCharSteps,
                kRateSteps, samples);

    std::printf("%-16s %9s %6s %7s %12s %12s\n",
                "ALGO", "ns/step", "rel", "trips", "median|x|", "median|y|");
    std::printf("%s\n", "----------------------------------------------------------------------");

    std::vector<Result> results;
    double base = 0.0;
    for (int i = 0; i < nModels(); i++) {
        Result r = characterise(model(i), samples);
        if (i == 0) base = r.nsPerStep;
        std::printf("%-16s %9.2f %6.2f %4d/%-3zu %12.4f %12.4f\n",
                    r.name, r.nsPerStep, r.nsPerStep / base,
                    r.trips, r.combos.size(), r.medianPeakX, r.medianPeakY);
        results.push_back(std::move(r));
    }

    std::printf("\nConstructor values — measured against what Attractors.h currently declares\n");
    for (int i = 0; i < nModels(); i++) {
        ChaosBase*    a = model(i);
        const Result& r = results[i];
        std::printf("\n  %s\n", r.name);
        printGain("gainL", a->gainL, gainFor(r.medianPeakX));
        printGain("gainR", a->gainR, gainFor(r.medianPeakY));
        printWindow("plot x", a->xMin, a->xRange, r.obsMinX, r.obsMaxX);
        printWindow("plot y", a->yMin, a->yRange, r.obsMinY, r.obsMaxY);
        const float headroom = (std::max(r.maxPeakX, r.maxPeakY) > 0.0f)
                             ? a->divergeBound / std::max(r.maxPeakX, r.maxPeakY) : 0.0f;
        std::printf("    %-10s declared %9.4f   peak |state| %7.4f   %.0fx headroom\n",
                    "divBound", a->divergeBound, std::max(r.maxPeakX, r.maxPeakY), headroom);
        if (r.trips)
            std::printf("    %d of %zu combinations tripped the guard — --verbose maps them\n",
                        r.trips, r.combos.size());
        if (verbose) printVerbose(r, a);
    }

    // ── Schedule checks ──────────────────────────────────────────────────────
    // Two properties the per-second rate fields exist to provide, asserted rather
    // than assumed: that the refactor is an identity at the rate everything was
    // originally measured at, and that a pitch request survives a change of
    // sample rate. The second is the whole point — a range tuned on 44.1 kHz
    // hardware has to still mean the same pitch at 96 kHz.
    {
        int identityFails = 0, invarianceFails = 0;
        for (int i = 0; i < nModels(); i++) {
            ChaosBase* a = model(i);
            // The pre-refactor per-sample equivalents, reconstructed.
            const float legacyRateMin = a->simRateMin / kRefSampleRate;
            const float legacyRateMax = a->simRateMax / kRefSampleRate;
            const float legacyOversMax = a->maxStepsPerSecond / kRefSampleRate;

            for (int p = 0; p <= 20; p++) {
                for (int oct = -5; oct <= 6; oct++) {
                    const float pot   = (float)p / 20.0f;
                    const float voct  = std::pow(2.0f, (float)oct);

                    // Legacy: pot spans dt directly, V/Oct multiplies, clamp, split.
                    float desired = (legacyRateMin + pot * (legacyRateMax - legacyRateMin)) * voct;
                    const float hi = a->dtBase * legacyOversMax;
                    if (desired < 1.0e-5f) desired = 1.0e-5f; else if (desired > hi) desired = hi;
                    const float legacyDt = desired;

                    // Current: pot spans simulated time per second, Voice schedules it.
                    const float simRate = (a->simRateMin + pot * (a->simRateMax - a->simRateMin)) * voct;
                    const StepSchedule s44 = a->scheduleFor(simRate, kRefSampleRate);
                    const float nowDt = s44.stepDt * s44.stepsPerSample;

                    if (std::fabs(nowDt - legacyDt) > 1.0e-6f * std::fabs(legacyDt) + 1.0e-9f)
                        identityFails++;

                    // Pitch = dt x sampleRate, and must not move with sampleRate
                    // wherever the request sits inside the clamps at both rates.
                    for (float fs : {48000.0f, 96000.0f}) {
                        const StepSchedule sN = a->scheduleFor(simRate, fs);
                        const float pitch44 = nowDt * kRefSampleRate;
                        const float pitchN  = sN.stepDt * sN.stepsPerSample * fs;
                        const bool clamped  = (pitch44 < simRate * 0.999f) || (pitchN < simRate * 0.999f);
                        if (!clamped && std::fabs(pitchN - pitch44) > 1.0e-4f * simRate)
                            invarianceFails++;
                    }
                }
            }
        }
        std::printf("\nSchedule checks\n");
        std::printf("  identity at %.0f Hz vs the pre-refactor per-sample formula: %s\n",
                    kRefSampleRate, identityFails ? "** FAIL **" : "pass");
        std::printf("  pitch invariant across 44.1 / 48 / 96 kHz:                  %s\n",
                    invarianceFails ? "** FAIL **" : "pass");
        if (identityFails)   std::printf("  %d identity mismatches\n", identityFails);
        if (invarianceFails) std::printf("  %d invariance mismatches\n", invarianceFails);
    }

    std::printf("\nNotes\n"
                "  - ns/step is host x86, not Cortex-M7: out-of-order execution flatters\n"
                "    algorithms with instruction-level parallelism (two coupled systems\n"
                "    especially), and glibc's transcendentals are not newlib's. Use `rel`\n"
                "    to rank arithmetic cost; set maxStepsPerSecond against the on-device\n"
                "    CPU readout at full oversampling, never against this.\n"
                "  - Guard trips are not automatically faults. Chua loses its bounded\n"
                "    attractor inside its own pot range on purpose (see README.md); the\n"
                "    stuttering re-seed past that floor is the character, not a bug.\n");
    return 0;
}
