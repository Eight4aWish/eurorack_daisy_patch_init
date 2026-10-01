// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// chaos_core auditions — one WAV per model, so a bank's six slots can be chosen
// by ear before any of them goes near the module. Renders bank 2 and the
// candidates in reserve (bank 2 was chosen from these nine on 2026-10-01). Host build.
//
// Each file is 48 kHz 16-bit stereo (L = X, R = Y, as J9/J10), rendered through
// Voice::setPitch() with TAME's mode on Auto and the envelope off (drone), in four
// sections separated by half a second of silence:
//
//   1  CHAOS swept end to end over 10 s, CHAR at the model's spot, TAME 0
//   2  CHAR swept end to end over 10 s, CHAOS at the spot, TAME 0
//   3  TAME swept 0 -> 1 over 10 s at the spot (1 s held at each end)
//   4  A2 C#3 E3 A3 arpeggio, twice through at TAME 0, then twice at TAME 0.7
//
// Sections 1-3 are at A2 (110 Hz). The "spot" is each model's best-known chaotic
// setting from the literature, checked against periodmap.
//
// Build and run:
//   g++ -O2 -std=c++17 -DCHAOS_CANDIDATES -I common/chaos_core/include
//       -I common/chaos_core/tools common/chaos_core/tools/audition.cpp
//       common/chaos_core/src/Registry.cpp common/chaos_core/src/Candidates.cpp
//       -o /tmp/audition && /tmp/audition <output directory>
//   (one line; split here for readability)

#include "models.h"
#include "chaos_core/Voice.h"

#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#ifndef CHAOS_CANDIDATES
#error "audition renders the reserve candidates too: build with -DCHAOS_CANDIDATES and src/Candidates.cpp"
#endif

using namespace chaos_core;

namespace {

constexpr float kSr = 48000.0f;
constexpr int   kBlock = 48;     // 1 kHz control rate, as on hardware
constexpr float kLevel = 0.7f;
constexpr float kGap = 0.5f;

struct Ctl { float chaos, charV, hz, tame; };

// Render `seconds` of a control program into L/R, appended, through one Voice
// that persists across sections (so the attractor carries on, as on the panel).
template <typename F>
void play(Voice& v, std::vector<float>& L, std::vector<float>& R, float seconds, F program) {
    const int n = (int)(seconds * kSr);
    const size_t at = L.size();
    L.resize(at + n + kBlock); R.resize(at + n + kBlock);
    for (int i = 0; i < n; i += kBlock) {
        const Ctl c = program(i / kSr);
        v.setPitch(c.chaos, c.charV, c.hz, c.tame);
        v.render(&L[at + i], &R[at + i], kBlock);
    }
    L.resize(at + n); R.resize(at + n);
    // 10 ms fades at both ends of the section, so the gaps don't click.
    const int fade = (int)(0.01f * kSr);
    for (int i = 0; i < fade && i < n; i++) {
        const float g = (float)i / fade;
        L[at + i] *= g; R[at + i] *= g;
        L[at + n - 1 - i] *= g; R[at + n - 1 - i] *= g;
    }
}

void gap(std::vector<float>& L, std::vector<float>& R) {
    L.resize(L.size() + (size_t)(kGap * kSr), 0.0f);
    R.resize(R.size() + (size_t)(kGap * kSr), 0.0f);
}

void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    const uint32_t n = (uint32_t)L.size(), bytes = n * 4, sr = (uint32_t)kSr;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(sr); u32(sr * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (uint32_t i = 0; i < n; i++)
        for (float v : {L[i], R[i]}) {
            float s = v * kLevel * 32767.0f;
            s = s > 32767.0f ? 32767.0f : (s < -32768.0f ? -32768.0f : s);
            u16((uint16_t)(int16_t)std::lrintf(s));
        }
    std::fclose(f);
    std::printf("  %s  (%.1f s)\n", path.c_str(), n / kSr);
}

float lerp(float a, float b, float t) { return a + (b - a) * (t < 0 ? 0 : (t > 1 ? 1 : t)); }

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : ".";
    std::printf("Writing to %s\n", dir.c_str());

    // Each model's spot: its best-known chaotic setting. Indices are models.h's:
    // bank 2 is 6-11, the candidates in reserve 12-14.
    struct Spot { int model; float chaos, charV; const char* why; };
    const Spot spots[] = {
        {6,  1.15f,  0.667f, "Baker & Gollub: g 1.15, drive 2/3"},
        {7,  0.8f,   28.0f,  "the Lu system itself: alpha 0.8, r 28"},
        {8,  30.0f,  6.0f,   "R 30, T 6: inside the chaotic band"},
        {9,  0.1f,   0.88f,  "forced near resonance: F 0.1, drive 0.88"},
        {10, 5.0f,   2.0f,   "loop gain 5, Q 2: chaotic"},
        {11, 3.25f,  0.006f, "chaotic bursting: I 3.25, r 0.006"},
        {12, 2.0f,   5.0f,   "Rikitake's classic: mu 2, a 5"},
        {13, 0.81f,  0.375f, "Shimizu & Morioka: lambda 0.81, alpha 0.375"},
        {14, 0.44f,  1.1f,   "Genesio & Tesi: a 0.44, b 1.1"},
    };
    const float notes[] = {110.0f, 138.59f, 164.81f, 220.0f};

    for (const Spot& s : spots) {
        ChaosBase* a = model(s.model);
        Voice v;
        v.setSampleRate(kSr);
        v.setAlgo(a);
        std::vector<float> L, R;

        // 1: CHAOS end to end.
        play(v, L, R, 10.0f, [&](float t) {
            return Ctl{lerp(a->chaosMin, a->chaosMax, t / 10.0f), s.charV, 110.0f, 0.0f};
        });
        gap(L, R);
        // 2: CHAR end to end.
        play(v, L, R, 10.0f, [&](float t) {
            return Ctl{s.chaos, lerp(a->charMin, a->charMax, t / 10.0f), 110.0f, 0.0f};
        });
        gap(L, R);
        // 3: TAME 0 -> 1, a second held at each end.
        play(v, L, R, 12.0f, [&](float t) {
            return Ctl{s.chaos, s.charV, 110.0f, lerp(0.0f, 1.0f, (t - 1.0f) / 10.0f)};
        });
        gap(L, R);
        // 4: the arpeggio, free then at TAME 0.7.
        play(v, L, R, 6.4f, [&](float t) {
            const int step = (int)(t / 0.4f);
            return Ctl{s.chaos, s.charV, notes[step % 4], step < 8 ? 0.0f : 0.7f};
        });

        char file[96];
        int k = std::snprintf(file, sizeof file, "%02d_", s.model + 1);
        for (const char* c = a->name; *c && k < 90; c++) file[k++] = (*c == ' ') ? '_' : (char)std::tolower(*c);
        std::snprintf(file + k, sizeof file - k, ".wav");
        writeWav(dir + "/" + file, L, R);
        std::printf("      spot: %s\n", s.why);
    }
    return 0;
}
