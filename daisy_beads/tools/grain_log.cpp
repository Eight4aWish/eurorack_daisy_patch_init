// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
// The render loop follows Mutable Instruments Beads' beads.cc: Copyright
// 2017-2021 Emilie Gillet, under the same licence.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// Wish's grain logger: Beads' real DSP, as daisy_beads builds it, run through
// scripted scenarios on the Mac, logging every grain it starts. It feeds the
// granular explainer in the Eight4aWish video (eight4awish-video,
// slides/wish-animations.md), so the animation shows Beads' own grains.
//
//   make -C daisy_beads grain-log      JSON + WAV per scenario, to build/grainlog/
//
// The grains come from a hook in a host-only copy of Beads' delay_grain.h
// (tools/grainlog/), which is never on the firmware's include path. The knobs go
// through Wish's real pot laws (src/pot_laws.h). Digital quality, mono: the
// delay line runs at the codec's 48 kHz, so delays and durations convert to
// seconds directly, and the buffer holds 8 s.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "beads/dsp/processor.h"
#include "beads/gain_control.h"
#include "stmlib/utils/gate_flags.h"

#include "pot_laws.h"

using namespace beads;

/* ── The hook ──────────────────────────────────────────────────────────── */
namespace beads {
void (*g_grain_start_hook)(const GrainStartEvent& e) = nullptr;
}

struct LoggedGrain
{
    double t;
    GrainStartEvent e;
};
static std::vector<LoggedGrain> g_grains;
static double                   g_now = 0.0;

static void OnGrainStart(const GrainStartEvent& e) { g_grains.push_back({g_now, e}); }

/* ── Beads ─────────────────────────────────────────────────────────────── */
static uint8_t  g_delay[3][256 * 1024];
static uint16_t g_reverb[32768];
static Processor   processor;
static GainControl gain_control;

static constexpr int    kRate         = 48000;    // Digital
static constexpr int    kBlockRate    = 4000;     // Beads' control rate
static constexpr size_t kBlock        = kRate / kBlockRate;
static constexpr float  kBufferSecs   = 768.0f * 1000.0f * 8.0f / 32.0f * 2.0f / kRate;  // mono: 8 s
static constexpr int    kFps          = 30;       // knob and freeze timeline
static constexpr int    kEnvRate      = 200;      // input envelope, per second of recording

/* ── Scenarios ─────────────────────────────────────────────────────────── */
// Knob positions 0..1, as a hand would leave them; Wish's pot laws do the rest.
// The knob moves are timed to the video's voiceover, so the animation plays them
// in real time: WishGranular.tsx's BEATS say which take second each scenario
// second lands on.
struct Knobs
{
    float time = 0.1f, size = 0.5f, shape = 0.5f, density = 0.5f, pitch = 0.5f;
    float time_rnd = 0.5f, size_rnd = 0.5f, shape_rnd = 0.5f, pitch_rnd = 0.5f;
    float voct = 0.0f;      // semitones from V/oct
    bool  freeze = false;
    bool  input = true;     // the phrase keeps playing in
};

struct Scenario
{
    std::string name;       // file name
    std::string beat;       // which animation beat it feeds
    float       seconds;
    std::function<Knobs(float)> knobs;
    std::vector<float> triggers;   // SEED trigger times; empty = internal clock only
};

static float Ramp(float t, float t0, float t1, float a, float b)
{
    if (t <= t0) return a;
    if (t >= t1) return b;
    return a + (b - a) * (t - t0) / (t1 - t0);
}

static std::vector<float> Every(float from, float to, float step)
{
    std::vector<float> v;
    for (float t = from; t < to; t += step) v.push_back(t);
    return v;
}

// PITCH knob position for a step of Beads' detune law (pot_laws.h kDetune):
// 0, 1, 3, 4, 7, 12, 24 semitones are steps 0..6, sign by side of noon. The
// middle of each step, clear of the crossfades.
static float PitchKnob(int step, int sign)
{
    return 0.5f + sign * (static_cast<float>(step) + 0.25f) / 12.0f;
}

static std::vector<Scenario> Scenarios()
{
    std::vector<Scenario> s;

    // 1. The buffer filling, no grains (DENSITY at noon).
    s.push_back({"buffer", "1 the buffer", 16.5f,
                 [](float) { Knobs k; k.density = 0.5f; return k; }, {}});

    // 2. One grain, from one trigger, every trigger accepted (DENSITY full).
    s.push_back({"one_grain", "2 one grain", 5.0f,
                 [](float) { Knobs k; k.density = 1.0f; k.time = 0.15f; k.size = 0.8f; return k; },
                 {3.0f}});

    // 2b. TIME, then SIZE, then SHAPE swept. SIZE starts below a third, where
    //     Beads plays grains backwards (delay_granular_player.h).
    s.push_back({"time_size_shape", "2 one grain: TIME, SIZE, SHAPE", 16.5f,
                 [](float t) {
                     Knobs k;
                     k.density = 1.0f;
                     // One knob moves at a time; the others rest mid-way.
                     k.time  = t < 3.95f ? Ramp(t, 1.6f, 3.9f, 0.02f, 0.4f) : 0.15f;
                     // SIZE steps, set just before each trigger: backwards and long,
                     // shortest at a third, then forwards and growing (Beads' SIZE law).
                     static const float kSizes[8] = {0.03f, 0.12f, 0.22f, 0.33f, 0.45f, 0.6f, 0.72f, 0.85f};
                     const int si = std::max(0, std::min(7, static_cast<int>((t - 3.95f) / 0.64f)));
                     k.size  = (t >= 3.95f && t < 9.14f) ? kSizes[si] : 0.55f;
                     k.shape = t >= 9.14f ? Ramp(t, 9.3f, 16.0f, 0.0f, 1.0f) : 0.5f;
                     return k;
                 },
                 [] {
                     std::vector<float> v = Every(1.75f, 3.95f, 0.5f);           // TIME
                     for (int i = 0; i < 8; ++i) v.push_back(4.05f + 0.64f * i);  // one per SIZE step
                     for (float t : Every(9.25f, 16.5f, 0.5f)) v.push_back(t);    // SHAPE
                     return v;
                 }()});

    // 3. PITCH through the detune steps: up 1, 3, 4, 7, 12, 24, then down.
    s.push_back({"pitch", "3 pitch", 12.0f,
                 [](float t) {
                     Knobs k;
                     k.density = 1.0f;
                     k.time = 0.1f; k.size = 0.45f;
                     const int i = static_cast<int>((t - 2.0f) / 0.6f);
                     if (t < 2.0f)       k.pitch = 0.5f;
                     else if (i < 7)     k.pitch = PitchKnob(i, +1);
                     else                k.pitch = PitchKnob(i - 7, -1);
                     return k;
                 },
                 Every(2.15f, 12.0f, 0.3f)});

    // 4. DENSITY: noon, anticlockwise (regular, faster), then clockwise
    //    (random, denser). Internal clock.
    s.push_back({"density", "4 density", 12.0f,
                 [](float t) {
                     Knobs k;
                     k.time = 0.1f; k.size = 0.45f;
                     if (t < 6.0f)       k.density = 0.5f;
                     else if (t < 8.0f)  k.density = 0.35f;
                     else if (t < 9.9f)  k.density = 0.2f;
                     else if (t < 10.9f) k.density = 0.75f;
                     else                k.density = 0.9f;
                     return k;
                 },
                 {}});

    // 5. The randomisers: none, then clockwise (the same degree of randomness
    //    on every grain), then anticlockwise (a random degree per grain), on TIME
    //    and PITCH.
    s.push_back({"randomisers", "5 randomisers", 15.0f,
                 [](float t) {
                     Knobs k;
                     k.density = 0.25f;   // regular, so the scatter is the randomiser's
                     k.time = 0.12f; k.size = 0.4f;
                     if (t >= 7.08f && t < 10.7f) { k.time_rnd = 0.9f;  k.pitch_rnd = 0.85f; }
                     if (t >= 10.7f)             { k.time_rnd = 0.1f;  k.pitch_rnd = 0.15f; }
                     return k;
                 },
                 {}});

    // 6. FREEZE: grains read the buffer as it records, then it is held.
    s.push_back({"freeze", "6 freeze", 12.0f,
                 [](float t) {
                     Knobs k;
                     k.density = 0.72f; k.time = 0.2f; k.size = 0.5f;
                     k.freeze = t >= 6.0f;
                     return k;
                 },
                 {}});

    // 7. SEED from a sequencer: capture a phrase, freeze, then each trigger plays
    //    one grain at the pitch on V/oct.
    s.push_back({"seed", "7 seed", 16.0f,
                 [](float t) {
                     Knobs k;
                     k.density = 1.0f; k.time = 0.25f; k.size = 0.55f;
                     k.freeze = t >= 4.0f;
                     k.input  = t < 4.0f;
                     static const float kNotes[8] = {0, 3, 7, 12, 10, 7, 3, -2};
                     // V/oct changes 20 ms before each trigger, as a sequencer's would.
                     const int i = static_cast<int>(std::floor((t - 5.0f + 0.02f) / 0.5f));
                     k.voct = (t >= 4.98f) ? kNotes[((i % 8) + 8) % 8] : 0.0f;
                     return k;
                 },
                 Every(5.0f, 16.0f, 0.5f)});

    return s;
}

/* ── The input: a short phrase, plucked, easy to see in a buffer ───────── */
static float Phrase(double t)
{
    static const float kSemis[8] = {0, 3, 7, 10, 12, 7, 3, 5};
    const double note = std::floor(t * 4.0);
    const double age  = t - note / 4.0;
    const float  f    = 220.0f * std::pow(2.0f, kSemis[static_cast<int>(note) % 8] / 12.0f);
    const double ph   = std::fmod(t * f, 1.0);
    const float  saw  = static_cast<float>(2.0 * ph - 1.0);
    return 0.4f * saw * static_cast<float>(std::exp(-age / 0.12));
}

/* ── Output ────────────────────────────────────────────────────────────── */
static void WriteWav(const std::string& path, const std::vector<float>& lr)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t frames = static_cast<uint32_t>(lr.size() / 2);
    const uint32_t bytes  = frames * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2);
    u32(kRate); u32(kRate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (float s : lr)
    {
        const float   c = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
        const int16_t v = static_cast<int16_t>(c * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

static void Run(const Scenario& sc, const std::string& dir)
{
    std::memset(g_delay, 0, sizeof g_delay);
    std::memset(g_reverb, 0, sizeof g_reverb);
    uint8_t* buffers[3] = {g_delay[0], g_delay[1], g_delay[2]};
    processor.Init(buffers, g_reverb);
    processor.Configure(Processor::MEDIA_DIGITAL, true);
    gain_control.Init(static_cast<float>(kRate), GainControl::SOFT_CLIPPING_MODE_OFF, true);
    g_grains.clear();

    const Knobs k0 = sc.knobs(0.0f);
    wish::PotFilter f_time, f_size, f_shape, f_density, f_pitch;
    wish::PotFilter f_time_rnd, f_size_rnd, f_shape_rnd, f_pitch_rnd;
    f_time.Init(wish::PotFilter::kNormal, 0.001f, k0.time);
    f_size.Init(wish::PotFilter::kNormal, 0.0f, k0.size);
    f_shape.Init(wish::PotFilter::kNormal, 0.0f, k0.shape);
    f_density.Init(wish::PotFilter::kNormal, 0.0f, k0.density);
    f_pitch.Init(wish::PotFilter::kDetune, 0.0f, k0.pitch);
    f_time_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, k0.time_rnd);
    f_size_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, k0.size_rnd);
    f_shape_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, k0.shape_rnd);
    f_pitch_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, k0.pitch_rnd);

    const size_t total = static_cast<size_t>(sc.seconds * kRate);
    std::vector<float> wav;
    wav.reserve(total * 2);

    // Timelines: knobs and freeze at kFps; the input envelope per second of
    // *recording* (nothing is written while frozen), so the animation can draw
    // the buffer from it; the recorded time at each frame places the write head.
    std::vector<Knobs> frames;
    std::vector<double> recorded_at_frame;
    std::vector<float> env;
    double recorded = 0.0;
    float  env_peak = 0.0f;
    size_t env_count = 0;
    const size_t env_every = kRate / kEnvRate;
    stmlib::GateFlags previous_seed = stmlib::GATE_FLAG_LOW;
    size_t next_trigger = 0;
    double trigger_until = -1.0;
    size_t next_frame = 0;

    for (size_t t = 0; t < total; t += kBlock)
    {
        const double now = static_cast<double>(t) / kRate;
        g_now = now;
        const Knobs k = sc.knobs(static_cast<float>(now));

        while (next_frame * kRate / kFps <= t)
        {
            frames.push_back(k);
            recorded_at_frame.push_back(recorded);
            next_frame++;
        }

        Codec::Frame in[kBlock];
        for (size_t i = 0; i < kBlock; ++i)
        {
            const float s = k.input ? Phrase(now + static_cast<double>(i) / kRate) : 0.0f;
            in[i].l = in[i].r = static_cast<int32_t>(s * 2147483647.0f);
            if (!k.freeze)
            {
                env_peak = std::max(env_peak, std::fabs(s));
                if (++env_count == env_every)
                {
                    env.push_back(env_peak);
                    env_peak = 0.0f;
                    env_count = 0;
                }
            }
        }
        if (!k.freeze) recorded += static_cast<double>(kBlock) / kRate;

        FloatFrame fin[kBlock], fout[kBlock];
        gain_control.Process(true, 0, false, true, in, fin, kBlock);

        bool freeze[kBlock];
        for (size_t i = 0; i < kBlock; ++i) freeze[i] = k.freeze;

        // SEED: a 5 ms trigger at each scripted time.
        stmlib::GateFlags        seed[kBlock];
        const stmlib::GateFlags* seed_input = nullptr;
        if (!sc.triggers.empty())
        {
            if (next_trigger < sc.triggers.size() && now >= sc.triggers[next_trigger])
            {
                trigger_until = now + 0.005;
                next_trigger++;
            }
            const bool high = now < trigger_until;
            for (size_t i = 0; i < kBlock; ++i)
                previous_seed = seed[i] = stmlib::ExtractGateFlags(previous_seed, high);
            seed_input = seed;
        }

        Parameters p;
        p.time  = {f_time.Process(k.time), f_time_rnd.Process(k.time_rnd), 0.0f, false};
        p.size  = {f_size.Process(k.size), f_size_rnd.Process(k.size_rnd), 0.0f, false};
        p.shape = {f_shape.Process(k.shape), f_shape_rnd.Process(k.shape_rnd), 0.0f, false};
        p.pitch = {f_pitch.Process(k.pitch) + k.voct, f_pitch_rnd.Process(k.pitch_rnd), 0.0f, false};
        p.monophonic_mode = false;

        ModulatedParameter d;
        d.base = f_density.Process(k.density);
        d.cv   = 0.0f;

        Processor::MixParameters m;
        m.feedback = 0.0f; m.dry_wet = 1.0f; m.reverb_amount = 0.0f; m.output_gate = false;

        processor.Process(SEED_BUTTON_STATE_LOCKED, seed_input, freeze, d, p, m, fin, fout, kBlock);
        for (size_t i = 0; i < kBlock; ++i)
        {
            wav.push_back(fout[i].l);
            wav.push_back(fout[i].r);
        }
    }

    WriteWav(dir + "/" + sc.name + ".wav", wav);

    FILE* f = std::fopen((dir + "/" + sc.name + ".json").c_str(), "w");
    if (!f) return;
    std::fprintf(f, "{\n  \"name\": \"%s\",\n  \"beat\": \"%s\",\n", sc.name.c_str(), sc.beat.c_str());
    std::fprintf(f, "  \"seconds\": %.3f,\n  \"bufferSeconds\": %.3f,\n  \"fps\": %d,\n  \"envRate\": %d,\n",
                 sc.seconds, kBufferSecs, kFps, kEnvRate);
    std::fprintf(f, "  \"triggers\": [");
    for (size_t i = 0; i < sc.triggers.size(); ++i)
        std::fprintf(f, "%s%.4f", i ? ", " : "", sc.triggers[i]);
    std::fprintf(f, "],\n  \"grains\": [\n");
    for (size_t i = 0; i < g_grains.size(); ++i)
    {
        const LoggedGrain& g = g_grains[i];
        std::fprintf(f,
                     "    {\"t\": %.4f, \"delay\": %.4f, \"duration\": %.4f, \"speed\": %.4f, "
                     "\"semitones\": %.2f, \"reverse\": %s, \"shape\": %.3f, \"gain\": %.3f, \"pan\": %.3f, "
                     "\"frozen\": %s}%s\n",
                     g.t, g.e.delay / kRate, g.e.duration / kRate, g.e.speed,
                     12.0f * std::log2(std::max(std::fabs(g.e.speed), 1e-6f)),
                     g.e.speed < 0.0f ? "true" : "false", g.e.shape, g.e.gain, g.e.pan,
                     g.e.frozen ? "true" : "false", i + 1 < g_grains.size() ? "," : "");
    }
    std::fprintf(f, "  ],\n  \"frames\": [\n");
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const Knobs& k = frames[i];
        std::fprintf(f,
                     "    {\"time\": %.3f, \"size\": %.3f, \"shape\": %.3f, \"density\": %.3f, \"pitch\": %.3f, "
                     "\"timeRnd\": %.3f, \"sizeRnd\": %.3f, \"shapeRnd\": %.3f, \"pitchRnd\": %.3f, "
                     "\"voct\": %.1f, \"freeze\": %s, \"input\": %s, \"recorded\": %.4f}%s\n",
                     k.time, k.size, k.shape, k.density, k.pitch, k.time_rnd, k.size_rnd, k.shape_rnd,
                     k.pitch_rnd, k.voct, k.freeze ? "true" : "false", k.input ? "true" : "false",
                     recorded_at_frame[i], i + 1 < frames.size() ? "," : "");
    }
    std::fprintf(f, "  ],\n  \"inputEnvelope\": [");
    for (size_t i = 0; i < env.size(); ++i) std::fprintf(f, "%s%.3f", i ? "," : "", env[i]);
    std::fprintf(f, "]\n}\n");
    std::fclose(f);

    std::printf("%-16s %5.1f s  %4zu grains  %4zu frames  %5zu envelope points\n", sc.name.c_str(),
                sc.seconds, g_grains.size(), frames.size(), env.size());
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "build/grainlog";
    beads::g_grain_start_hook = OnGrainStart;
    for (const Scenario& sc : Scenarios()) Run(sc, dir);
    return 0;
}
