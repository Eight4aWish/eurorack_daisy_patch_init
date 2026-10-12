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
// Wish on the Mac: Beads' DSP as daisy_beads builds it (src/upstream shadows
// included), driven the way src/wish.cpp drives it, at each media's codec rate
// in 4 kHz blocks. Every case must stay finite and inside full scale, and make
// sound where it should; each also writes a WAV to listen to.
//
//   make -C daisy_beads host-test            build and run, WAVs to build/host/
//
// Nothing here touches the Alchemy Lab: it checks the DSP integration (the
// 48 kHz dirty-tape path upstream never shipped, the CV and pot scaling, the
// SEED clock path, synth mode, gate output), not the hardware layer.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "beads/gain_control.h"
#include "stmlib/utils/gate_flags.h"

#include "pot_laws.h"

using namespace beads;

static uint8_t  g_delay[3][256 * 1024];
static uint16_t g_reverb[32768];

static Processor   processor;
static Synth       synth;
static GainControl gain_control;

static constexpr int kBlockRate = 4000;
static int           g_failures = 0;

static void Check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

/* ── WAV out (16-bit stereo) ───────────────────────────────────────────── */
static void WriteWav(const std::string& path, const std::vector<float>& lr, int rate)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t frames = static_cast<uint32_t>(lr.size() / 2);
    const uint32_t bytes  = frames * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2);
    u32(static_cast<uint32_t>(rate)); u32(static_cast<uint32_t>(rate) * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (float s : lr)
    {
        const float   c = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
        const int16_t v = static_cast<int16_t>(c * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

/* ── One render ────────────────────────────────────────────────────────── */
struct Case
{
    std::string name;
    int   media      = Processor::MEDIA_DIGITAL;
    bool  mono       = true;
    bool  synth_mode = false;
    float seconds    = 6.0f;

    // Knobs, 0..1, as the pots and layer would give them. TIME is how far back
    // in the buffer grains read (up to 32 s in dirty-tape mono), so keep it near
    // the write head or a short render hears only the empty buffer.
    float time = 0.05f, size = 0.6f, shape = 0.5f, density = 0.75f, pitch = 0.5f;
    float time_rnd = 0.5f, size_rnd = 0.5f, shape_rnd = 0.5f, pitch_rnd = 0.5f;
    float feedback = 0.3f, dry_wet = 1.0f, reverb = 0.3f;
    float pitch_volts = 0.0f;   // J3

    bool  seed_locked = true;
    float seed_hz     = 0.0f;   // J4 triggers; 0 = none
    float freeze_from = -1.0f;  // seconds; FREEZE held from here on
    float input_until = 1e9f;   // seconds; input silent after this
    bool  gate_out    = false;
};

struct Result
{
    float peak = 0.0f, rms_tail = 0.0f;   // peak before the output clamp
    bool  finite = true, gate_values_ok = true;
};

static Result Render(const Case& c, const std::string& out_dir)
{
    std::memset(g_delay, 0, sizeof g_delay);
    std::memset(g_reverb, 0, sizeof g_reverb);
    uint8_t* buffers[3] = {g_delay[0], g_delay[1], g_delay[2]};
    processor.Init(buffers, g_reverb);
    synth.Init(g_reverb);

    if (!c.synth_mode) processor.Configure(Processor::Media(c.media), c.mono);
    const int    rate  = c.synth_mode ? 48000 : static_cast<int>(processor.sample_rate());
    const size_t block = static_cast<size_t>(rate / kBlockRate);
    gain_control.Init(static_cast<float>(rate),
                      c.media == Processor::MEDIA_DIRTY_TAPE
                          ? GainControl::SOFT_CLIPPING_MODE_ON
                          : (c.media == Processor::MEDIA_CLEAN_TAPE
                                 ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                                 : GainControl::SOFT_CLIPPING_MODE_OFF),
                      true);

    wish::PotFilter f_time, f_size, f_shape, f_density, f_pitch, f_dry_wet;
    wish::PotFilter f_time_rnd, f_size_rnd, f_shape_rnd, f_pitch_rnd, f_feedback, f_reverb;
    f_time.Init(wish::PotFilter::kNormal, 0.001f, c.time);
    f_size.Init(wish::PotFilter::kNormal, 0.0f, c.size);
    f_shape.Init(wish::PotFilter::kNormal, 0.0f, c.shape);
    f_density.Init(wish::PotFilter::kNormal, 0.0f, c.density);
    f_pitch.Init(wish::PotFilter::kDetune, 0.0f, c.pitch);
    f_dry_wet.Init(wish::PotFilter::kNormal, 0.0f, c.dry_wet);
    f_time_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, c.time_rnd);
    f_size_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, c.size_rnd);
    f_shape_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, c.shape_rnd);
    f_pitch_rnd.Init(wish::PotFilter::kAttenuverter, 0.0f, c.pitch_rnd);
    f_feedback.Init(wish::PotFilter::kNormal, 0.0f, c.feedback);
    f_reverb.Init(wish::PotFilter::kNormal, 0.0f, c.reverb);

    const size_t total  = static_cast<size_t>(c.seconds * rate);
    const size_t tail0  = total / 3;   // measure after the buffer has filled
    std::vector<float> wav;
    wav.reserve(total * 2);

    Result r;
    double sum_sq = 0.0;
    size_t n_tail = 0;
    float  phase = 0.0f, seed_phase = 0.0f;
    stmlib::GateFlags previous_seed = stmlib::GATE_FLAG_LOW;

    for (size_t t = 0; t < total; t += block)
    {
        const float now = static_cast<float>(t) / rate;

        // Input: a 110 Hz saw at -12 dBFS, pulsed at 2 Hz, then silence.
        Codec::Frame in[24];
        for (size_t i = 0; i < block; ++i)
        {
            float s = 0.0f;
            if (now < c.input_until)
            {
                phase += 110.0f / rate;
                if (phase >= 1.0f) phase -= 1.0f;
                const float env = std::fmod(now * 2.0f, 1.0f) < 0.5f ? 1.0f : 0.2f;
                s = 0.25f * (2.0f * phase - 1.0f) * env;
            }
            in[i].l = static_cast<int32_t>(s * 2147483647.0f);
            in[i].r = static_cast<int32_t>(-0.7f * s * 2147483647.0f);
        }
        FloatFrame fin[24], fout[24];
        gain_control.Process(true, 0, c.synth_mode, c.synth_mode || c.mono, in, fin, block);

        bool freeze[24];
        for (size_t i = 0; i < block; ++i) freeze[i] = c.freeze_from >= 0.0f && now >= c.freeze_from;

        stmlib::GateFlags        seed[24];
        const stmlib::GateFlags* seed_input = nullptr;
        if (c.seed_hz > 0.0f)
        {
            seed_phase += c.seed_hz * block / rate;
            const bool high = std::fmod(seed_phase, 1.0f) < 0.1f;
            for (size_t i = 0; i < block; ++i)
                previous_seed = seed[i] = stmlib::ExtractGateFlags(previous_seed, high);
            seed_input = seed;
        }

        Parameters p;
        p.time  = {f_time.Process(c.time), f_time_rnd.Process(c.time_rnd), 0.0f, false};
        p.size  = {f_size.Process(c.size), f_size_rnd.Process(c.size_rnd), 0.0f, false};
        p.shape = {f_shape.Process(c.shape), f_shape_rnd.Process(c.shape_rnd), 0.0f, false};
        p.pitch = {f_pitch.Process(c.pitch) + c.pitch_volts * 12.0f,
                   f_pitch_rnd.Process(c.pitch_rnd), 0.0f, false};
        p.monophonic_mode = false;

        ModulatedParameter d;
        d.base = f_density.Process(c.density);
        d.cv   = 0.0f;

        Processor::MixParameters m;
        m.feedback      = std::min(f_feedback.Process(c.feedback) * 1.005f, 1.0f);
        m.dry_wet       = std::min(f_dry_wet.Process(c.dry_wet) * 1.005f, 1.0f);
        m.reverb_amount = std::min(f_reverb.Process(c.reverb) * 1.005f, 1.0f);
        m.output_gate   = c.gate_out;

        const SeedButtonState seed_button =
            c.seed_locked ? SEED_BUTTON_STATE_LOCKED : SEED_BUTTON_STATE_OFF;

        if (c.synth_mode)
            synth.Render(seed_button, seed_input, freeze, Synth::Resolution(c.media),
                         d, p, m, fout, block);
        else
            processor.Process(seed_button, seed_input, freeze, d, p, m, fin, fout, block);

        for (size_t i = 0; i < block; ++i)
        {
            if (!std::isfinite(fout[i].l) || !std::isfinite(fout[i].r)) r.finite = false;
            r.peak = std::max(r.peak, std::max(std::fabs(fout[i].l), std::fabs(fout[i].r)));
            // As wish.cpp (and Beads' beads.cc) before the codec.
            const float l  = std::min(std::max(fout[i].l, -1.0f), 0.999999f);
            const float rr = std::min(std::max(fout[i].r, -1.0f), 0.999999f);
            if (c.gate_out && !(rr == 0.0f || rr == -0.8f)) r.gate_values_ok = false;
            if (t + i >= tail0)
            {
                sum_sq += 0.5 * (double(l) * l + (c.gate_out ? 0.0 : double(rr) * rr));
                n_tail++;
            }
            wav.push_back(l);
            wav.push_back(rr);
        }
    }
    r.rms_tail = n_tail ? static_cast<float>(std::sqrt(sum_sq / n_tail)) : 0.0f;
    WriteWav(out_dir + "/" + c.name + ".wav", wav, rate);
    return r;
}

static void Expect(const Case& c, const std::string& out_dir, float min_rms = 1e-3f)
{
    const Result r = Render(c, out_dir);
    char line[160];
    std::snprintf(line, sizeof line, "%-28s peak %.3f  tail rms %.4f", c.name.c_str(),
                  r.peak, r.rms_tail);
    std::printf("%s\n", line);
    Check(r.finite, "finite");
    // Beads' output limiter is soft: a few percent over full scale before the
    // clamp is its normal behaviour; much more would mean it is not working.
    Check(r.peak <= 1.25f, "limiter holds (peak before clamp under 1.25)");
    Check(r.rms_tail >= min_rms, "makes sound");
    if (c.gate_out) Check(r.gate_values_ok, "R carries gates (0 or -0.8) only");
}

static void PotLaws()
{
    std::printf("pot laws\n");
    wish::PotFilter d, a;
    d.Init(wish::PotFilter::kDetune, 0.0f, 0.5f);
    a.Init(wish::PotFilter::kAttenuverter, 0.0f, 0.5f);
    Check(std::fabs(d.Apply(0.0f) + 24.0f) < 1e-3f, "PITCH fully left is -24 semitones");
    Check(std::fabs(d.Apply(1.0f) - 24.0f) < 1e-3f, "PITCH fully right is +24 semitones");
    Check(std::fabs(d.Apply(0.5f)) < 1e-3f, "PITCH centre is 0");
    Check(std::fabs(a.Apply(0.0f) + 1.0f) < 1e-3f, "randomiser fully left is -1");
    Check(std::fabs(a.Apply(1.0f) - 1.0f) < 1e-3f, "randomiser fully right is +1");
    Check(a.Apply(0.52f) == 0.0f && a.Apply(0.48f) == 0.0f, "randomiser dead zone at centre");
}

int main(int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    PotLaws();

    static const char* kMedia[4] = {"dirty_tape", "clean_tape", "vintage_digital", "digital"};
    for (int media = 0; media < 4; media++)
        for (int mono = 1; mono >= 0; mono--)
        {
            Case c;
            c.name  = std::string(kMedia[media]) + (mono ? "_mono" : "_stereo");
            c.media = media;
            c.mono  = mono != 0;
            Expect(c, out);
        }

    {
        Case c; c.name = "delay_mode_dirty_tape"; c.media = Processor::MEDIA_DIRTY_TAPE;
        c.size = 1.0f; c.time = 0.02f; c.feedback = 0.6f;
        Expect(c, out);
    }
    {
        Case c; c.name = "freeze_after_input_stops"; c.freeze_from = 1.5f; c.input_until = 2.0f;
        Expect(c, out);
    }
    {
        Case c; c.name = "randomisers_at_extremes"; c.time_rnd = 1.0f; c.size_rnd = 0.0f;
        c.shape_rnd = 1.0f; c.pitch_rnd = 0.0f; c.pitch_volts = 2.0f;
        Expect(c, out);
    }
    {
        Case c; c.name = "seed_clock_divided"; c.seed_hz = 8.0f; c.density = 0.2f;
        Expect(c, out);
    }
    {
        Case c; c.name = "seed_manual_unlatched"; c.seed_locked = false; c.seed_hz = 3.0f;
        c.density = 0.5f;
        Expect(c, out);
    }
    {
        Case c; c.name = "gate_output"; c.gate_out = true;
        Expect(c, out);
    }
    for (int media = 0; media < 4; media++)
    {
        Case c; c.name = std::string("synth_") + kMedia[media]; c.synth_mode = true;
        c.media = media; c.input_until = 0.0f;
        Expect(c, out);
    }

    std::printf("%s: %d failure%s\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
