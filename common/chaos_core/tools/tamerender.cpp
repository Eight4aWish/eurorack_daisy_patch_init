// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// chaos_core TAME renders — audition files, so the middle of the TAME knob can be
// judged by ear before any hardware. Host build.
//
// Writes 48 kHz 16-bit stereo WAVs (L = X, R = Y, as the panel's J9/J10) through
// chaos_core::Voice::setPitch(), with the control rate at 1 kHz as on hardware:
//
//   01-05  TAME swept 0 -> 1 over ten seconds at A2 (110 Hz), one per model
//   06     Lorenz's bench-found plucked-string spot (rho 104.5, sigma 6.69)
//          at five TAME settings
//   07-08  an A-major arpeggio at four TAME settings, on Rossler and Lorenz --
//          does the voice follow the notes?
//
// Build and run:
//   g++ -O2 -std=c++17 -I common/chaos_core/include
//       common/chaos_core/tools/tamerender.cpp common/chaos_core/src/Registry.cpp
//       -o /tmp/tamerender && /tmp/tamerender <output directory>
//   (one line; split here for readability)

#include "chaos_core/Registry.h"
#include "chaos_core/Voice.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace chaos_core;

namespace {

constexpr float kSr = 48000.0f;
constexpr int   kBlock = 48;
constexpr float kLevel = 0.7f;

void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    const uint32_t n = (uint32_t)L.size(), bytes = n * 4, sr = (uint32_t)kSr;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(sr); u32(sr * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    const uint32_t fade = (uint32_t)(0.01f * kSr);
    for (uint32_t i = 0; i < n; i++) {
        float g = kLevel;
        if (i < fade) g *= (float)i / fade;
        if (n - 1 - i < fade) g *= (float)(n - 1 - i) / fade;
        for (float v : {L[i], R[i]}) {
            float s = v * g * 32767.0f;
            s = s > 32767.0f ? 32767.0f : (s < -32768.0f ? -32768.0f : s);
            u16((uint16_t)(int16_t)std::lrintf(s));
        }
    }
    std::fclose(f);
    std::printf("  %s  (%.1f s)\n", path.c_str(), n / kSr);
}

// A control-rate program: at time t (seconds), what to play.
struct Ctl { float chaos, charV, hz, tame; };

template <typename F>
void render(const std::string& path, ChaosBase* a, float seconds, F program) {
    Voice v;
    v.setSampleRate(kSr);
    v.setAlgo(a);
    const int n = (int)(seconds * kSr);
    std::vector<float> L(n + kBlock), R(n + kBlock);
    for (int i = 0; i < n; i += kBlock) {
        const Ctl c = program(i / kSr);
        v.setPitch(c.chaos, c.charV, c.hz, c.tame);
        v.render(&L[i], &R[i], kBlock);
    }
    L.resize(n); R.resize(n);
    writeWav(path, L, R);
}


}  // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : ".";
    std::printf("Writing to %s\n", dir.c_str());

    // 01-05: TAME sweeps. One second held at each end, ten seconds of ramp.
    struct Sweep { const char* file; int algo; float chaos, charV; };
    const Sweep sweeps[] = {
        {"01_rossler_tame_sweep.wav",         0, 5.6f,  0.23f},
        {"02_coupled_rossler_tame_sweep.wav", 5, 5.6f,  0.25f},
        {"03_lorenz_tame_sweep.wav",          2, 28.0f, 10.0f},   // canonical
        {"04_chua_tame_sweep.wav",            3, 9.8f,  14.0f},
        {"05_duffing_tame_sweep.wav",         4, 0.52f, 1.1f},
    };
    for (const Sweep& s : sweeps) {
        render(dir + "/" + s.file, algos[s.algo], 12.0f, [&](float t) {
            float tame = (t - 1.0f) / 10.0f;
            tame = tame < 0.0f ? 0.0f : (tame > 1.0f ? 1.0f : tame);
            return Ctl{s.chaos, s.charV, 110.0f, tame};
        });
    }

    // 06: the Lorenz sweet spot, 2.5 s at each of five TAME settings.
    {
        const float tames[] = {0.0f, 0.3f, 0.55f, 0.7f, 1.0f};
        render(dir + "/06_lorenz_sweetspot_rho104.5.wav", algos[2], 12.5f, [&](float t) {
            int k = (int)(t / 2.5f); if (k > 4) k = 4;
            return Ctl{104.5f, 6.69f, 110.0f, tames[k]};
        });
    }

    // 07-08: A-major arpeggio, A2 C#3 E3 A3 twice at each of four TAME settings.
    struct Arp { const char* file; int algo; float chaos, charV; float tames[4]; };
    const Arp arps[] = {
        {"07_rossler_arpeggio.wav", 0, 5.6f,  0.23f, {0.0f, 0.35f, 0.75f, 1.0f}},
        {"08_lorenz_arpeggio.wav",  2, 28.0f, 10.0f, {0.0f, 0.5f,  0.7f,  1.0f}},
    };
    const float notes[] = {110.0f, 138.59f, 164.81f, 220.0f};
    for (const Arp& p : arps) {
        render(dir + "/" + p.file, algos[p.algo], 12.8f, [&](float t) {
            const int step = (int)(t / 0.4f);
            int sec = step / 8; if (sec > 3) sec = 3;
            return Ctl{p.chaos, p.charV, notes[step % 4], p.tames[sec]};
        });
    }
    return 0;
}
