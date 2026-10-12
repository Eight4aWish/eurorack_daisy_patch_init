// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
// Portions adapted from Mutable Instruments Beads (beads.cc, ui.cc, settings.cc,
// cv_reader.h): Copyright 2017-2021 Emilie Gillet, under the same licence.
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
// Wish on Daisy patch.Init() -- Wish, the granular texture synthesizer based on Mutable
// Instruments Beads, on a stock Electrosmith Daisy patch.init().
//
// The DSP is Beads' own, compiled from the submodule exactly as daisy_beads/
// (Wish for the Alchemy Lab) builds it, with the same two shadowed headers, the
// same pot laws (daisy_beads/src/pot_laws.h) and the same load governor. Same
// chip, same clock: the Patch SM is an STM32H750 at 480 MHz. This file is the
// panel: four knobs, one button, one toggle, one LED.
//
// Three layers of four knobs. The toggle picks PLAY or TONE; holding B7 gives
// RANDOM, Beads' four attenuverters as randomisers, each on its parameter's knob
// where it can be. The four PLAY knobs are the four with jacks.
//
//              KNOB 1 (CV_1)   KNOB 2 (CV_2)   KNOB 3 (CV_3)   KNOB 4 (CV_4)
//   PLAY       TIME            SIZE            DENSITY         PITCH          toggle down
//   TONE       SHAPE           DRY/WET         FEEDBACK        REVERB         toggle up
//   RANDOM     TIME random     SIZE random     SHAPE random    PITCH random   B7 held
//
// A knob takes a parameter over only once it reaches the parameter's value
// (catch), so changing layer never makes anything jump. A randomiser is Beads'
// attenuverter with nothing patched: noon none, clockwise a smooth random walk,
// anticlockwise a fresh random value per grain. CV is added at full depth, as
// on Wish.
//
//   B7 tap              FREEZE on/off. GATE IN 2 holds it too. LED lit while frozen.
//   B7 held 2 s, without turning anything: SETTINGS. KNOB 1 QUALITY (Beads' four
//                       media, digital anticlockwise to dirty tape clockwise),
//                       KNOB 2 INPUT (mono, stereo, synth), KNOB 3 GATE OUT (off,
//                       on). Only a knob that is turned changes its setting; the LED
//                       blinks the new value's number. Tap B7 to leave.
//   B7 at power-on      V/OCT calibration, as Joy Lite: patch 1 V, press; 3 V, press.
//
//   CV_5 V/OCT          CV_6 TIME CV        CV_7 SIZE CV        CV_8 DENSITY CV
//   GATE IN 1 SEED      GATE IN 2 FREEZE    GATE OUT 1 Beads' grain gates, when on
//   IN L / IN R         OUT L / OUT R       audio. IN R follows IN L when unpatched.
//
// There is no SEED button: the internal clock runs, as Beads boots, and DENSITY
// at noon is no grains. Triggers on GATE IN 1 take over as on Wish: SEED counts
// as patched while they arrive and for a while after, DENSITY divides or thins
// them, and the internal clock comes back once they stop.
//
// GATE OUT is Beads' own gate output, which takes its right channel: with it on,
// both audio outs carry Beads' mono mix and GATE OUT 1 carries the gates.
//
// Settings, the knob values of the two layers not under the hand, and the V/OCT
// calibration are saved to QSPI a few seconds after the last change.

#include <string.h>
#include <algorithm>
#include <cmath>

#include "daisy_patch_sm.h"
#include "util/CpuLoadMeter.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/gate_flags.h"

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "beads/gain_control.h"

#include "pot_laws.h"
#include "voct_cal.h"

using namespace daisy;
using namespace patch_sm;

using beads::FloatFrame;
using beads::GainControl;
using beads::ModulatedParameter;
using beads::Parameters;
using beads::Processor;
using beads::Synth;
using stmlib::GateFlags;
using wish::CvFilter;
using wish::PotFilter;

/* ── Rates ──────────────────────────────────────────────────────────────── */
// Beads runs its controls once per codec block at 4 kHz, whatever the sample
// rate: 12 samples at 48 kHz, 8 at 32 kHz. Its filter coefficients and the
// SEED delay below assume that rate, so the block size follows the codec.
static constexpr int    kBlockRate    = 4000;
static constexpr size_t kMaxBlockSize = 24;   // Beads' kMaxCodecBlockSize

// CV scaling, from Beads' default calibration over its +-5 V inputs, as Wish.
// GetAdcValue() reads a CV jack as -1..+1 for -5..+5 V.
static constexpr float kVoltsPerAdc      = 5.0f;
static constexpr float kTimeSizePerVolt  = 0.32f;
static constexpr float kDensityPerVolt   = 0.2f;

// V/OCT, CV_5: two-point calibration shared with Joy (common/voct_cal.h).
// Joy Lite's defaults until a calibration is run.
static constexpr float kVoctCenterDefault = 0.007074f;
static constexpr float kVoctSlopeDefault  = 60.73f;
static joy::VoctCal    g_voct_cal         = {kVoctCenterDefault, kVoctSlopeDefault};

// SEED counts as patched for this long after a trigger, or four of its own
// periods if longer (Beads' divider resets after the same four), capped.
static constexpr uint32_t kExtClockMinMs = 4000;
static constexpr uint32_t kExtClockMaxMs = 30000;

/* ── Buffers ────────────────────────────────────────────────────────────── */
// As Wish: the 768 KB of delay lines in SDRAM; the reverb, read and written
// every sample, on chip in D2's `.heap` section, which libDaisy's SRAM linker
// script places at 0x30008000, past the 32 KB the MPU makes uncached for DMA,
// and before `end`, where newlib's malloc starts. Neither is zeroed at boot.
static uint8_t  s_delay[3][256 * 1024] __attribute__((section(".sdram_bss")));
static uint16_t s_reverb[32768]        __attribute__((section(".heap")));

/* ── Hardware and engine ────────────────────────────────────────────────── */
static DaisyPatchSM hw;
static Switch       b7;       // momentary
static Switch       toggle;   // B8
static CpuLoadMeter cpu;

static Processor   processor;
static Synth       synth;
static GainControl gain_control;

/* ── Parameters and layers ──────────────────────────────────────────────── */
enum Param : uint8_t
{
    kTime, kSize, kDensity, kPitch,              // PLAY
    kShape, kDryWet, kFeedback, kReverb,         // TONE
    kTimeRnd, kSizeRnd, kShapeRnd, kPitchRnd,    // RANDOM
    kNumParams
};
enum Layer : uint8_t { kLayerPlay = 0, kLayerTone = 1, kLayerRandom = 2, kLayerSettings = 3 };
static constexpr uint8_t kNumKnobs = 4;

static constexpr uint8_t kLayerParams[3][kNumKnobs] = {
    {kTime, kSize, kDensity, kPitch},
    {kShape, kDryWet, kFeedback, kReverb},
    {kTimeRnd, kSizeRnd, kShapeRnd, kPitchRnd},
};

// First-boot values, 0..1 as a knob would set them. The randomisers start at
// none; FEEDBACK off and a little REVERB, as Wish's layer.
static constexpr float kParamDefaults[kNumParams] = {
    0.5f, 0.5f, 0.75f, 0.5f,
    0.5f, 0.5f, 0.0f, 0.2f,
    0.5f, 0.5f, 0.5f, 0.5f,
};

// Written by the audio callback (the knob under the hand), read by both.
static volatile float s_param[kNumParams];

// The layer the knobs drive, set by the main loop from the toggle and B7.
static volatile uint8_t s_layer = kLayerPlay;

// Catch: after a layer change a knob drives its parameter only once it reaches
// the parameter's value, or crosses it.
static uint8_t s_catchLayer = 0xFF;
static bool    s_caught[kNumKnobs];
static float   s_prevKnob[kNumKnobs];
static float   s_layerStartKnob[kNumKnobs];
static constexpr float kCatchWindow = 0.02f;
static constexpr float kTurnedBy    = 0.03f;   // a knob moved on purpose

static volatile float s_knob[kNumKnobs];       // raw 0..1, for SETTINGS
static volatile bool  s_turnedInLayer = false; // a knob moved since the layer began

/* ── Settings ───────────────────────────────────────────────────────────── */
enum : uint8_t { kInputMono = 0, kInputStereo = 1, kInputSynth = 2, kNumInputs = 3 };

static volatile uint8_t s_media   = Processor::MEDIA_DIGITAL;   // Beads' factory default
static volatile uint8_t s_input   = kInputMono;
static volatile bool    s_gateOut = false;

// The configuration the codec and processor are running, which the settings
// may be ahead of (a change waits while frozen, as on Beads).
static uint8_t  s_runMedia = 0xFF;
static uint8_t  s_runInput = 0xFF;
static uint32_t s_runRate  = 0;

// Saved to QSPI. PersistentStorage compares with != to decide whether to write.
struct WishSettings
{
    uint32_t magic;
    uint8_t  media, input, gate_out, pad;
    float    param[kNumParams];
    float    voct_center, voct_slope;

    bool operator!=(const WishSettings& o) const { return memcmp(this, &o, sizeof *this) != 0; }
};
static constexpr uint32_t kSettingsMagic      = 0x57495350u;   // "WISP"
// The 64 KB below Joy and Joy Lite's (0x7F0000), so a unit that runs both keeps
// Joy's model and V/OCT calibration through a visit from Wish, and the reverse.
static constexpr uint32_t kSettingsQspiOffset = 0x7E0000;
static PersistentStorage<WishSettings> storage(hw.qspi);

static bool     s_dirty      = false;
static uint32_t s_dirtySince = 0;
static constexpr uint32_t kSaveAfterMs = 3000;

static void MarkDirty()
{
    s_dirty      = true;
    s_dirtySince = System::GetNow();
}

/* ── Control <-> audio ──────────────────────────────────────────────────── */
static volatile bool     s_freezeButton = false;
static volatile bool     s_frozen       = false;   // button or GATE IN 2, last block
static volatile uint32_t s_seedCount    = 0;       // SEED triggers seen, for the LED
static volatile bool     s_extClock     = false;   // SEED counts as patched
static volatile int      s_transition   = 0;       // blocks of silence left

/* ── Pot and CV conditioning (daisy_beads/src/pot_laws.h) ───────────────── */
static PotFilter f_time, f_size, f_shape, f_density, f_pitch, f_dryWet;
static PotFilter f_timeRand, f_sizeRand, f_shapeRand, f_pitchRand, f_feedback, f_reverb;
static CvFilter  c_time, c_size, c_density, c_pitch;

/* ── SEED: delayed, and external-clock detection ────────────────────────── */
// Beads delays SEED by 2.5 ms, ten 4 kHz blocks, so a sequencer's pitch CV has
// settled by the time its trigger starts the grain.
static constexpr uint8_t kSeedDelayBlocks = 10;
static bool      s_seedDelay[16] = {};
static uint8_t   s_seedDelayPos  = 0;
static bool      s_seedWasHigh   = false;
static GateFlags s_previousSeed  = stmlib::GATE_FLAG_LOW;

static uint32_t s_sinceEdge  = 0xFFFFFFFFu / 2u;
static uint32_t s_extTimeout = kExtClockMinMs * (kBlockRate / 1000);

static void TrackExtClock(bool rising)
{
    constexpr uint32_t kMin = kExtClockMinMs * (kBlockRate / 1000);
    constexpr uint32_t kMax = kExtClockMaxMs * (kBlockRate / 1000);
    if (rising)
    {
        // A period is only known when the previous edge was recent enough to
        // count; the first trigger after a pause gets the minimum.
        const uint32_t period = s_extClock ? s_sinceEdge : 0u;
        uint32_t t = 4u * period;
        s_extTimeout = t < kMin ? kMin : (t > kMax ? kMax : t);
        s_sinceEdge  = 0;
        s_extClock   = true;
    }
    else if (s_sinceEdge < 0xFFFFFFFFu / 2u)
    {
        s_sinceEdge++;
    }
    if (s_extClock && s_sinceEdge > s_extTimeout) s_extClock = false;
}

/* ── Load governor, as Wish ─────────────────────────────────────────────── */
// Beads can ask for more than a block at its heaviest (dense grains, PITCH
// random, Digital stereo: measured on the Alchemy Lab, the same chip at the same
// clock). The callback times itself and, past 80%, pulls DENSITY toward noon:
// fewer new grains. It eases off once the load is back under 60%.
static float          s_ticksPerSample = 0.0f;
static volatile float s_govScale       = 1.0f;

static float GovernLoad(float scale, float used)
{
    if (used > 0.80f)      scale *= 0.95f;
    else if (used < 0.60f) scale += 0.0005f;
    return scale < 0.0f ? 0.0f : (scale > 1.0f ? 1.0f : scale);
}

static inline void ThrottleDensity(ModulatedParameter& d, float scale)
{
    d.base = 0.5f + (d.base - 0.5f) * scale;
    d.cv  *= scale;
}

static void EndBlock(uint32_t t0, size_t size)
{
    const float used = static_cast<float>(System::GetTick() - t0)
                       / (static_cast<float>(size) * s_ticksPerSample);
    s_govScale = GovernLoad(s_govScale, used);
    cpu.OnBlockEnd();
}

/* ── Knobs: catch and layers (audio callback, 4 kHz) ────────────────────── */
static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float Clamp11(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

static void ReadKnobs()
{
    float k[kNumKnobs];
    for (uint8_t i = 0; i < kNumKnobs; i++)
    {
        k[i]      = Clamp01(hw.GetAdcValue(CV_1 + i));
        s_knob[i] = k[i];
    }

    const uint8_t layer = s_layer;
    if (layer != s_catchLayer)
    {
        s_catchLayer    = layer;
        s_turnedInLayer = false;
        for (uint8_t i = 0; i < kNumKnobs; i++)
        {
            s_caught[i]          = false;
            s_prevKnob[i]        = k[i];
            s_layerStartKnob[i]  = k[i];
        }
    }

    for (uint8_t i = 0; i < kNumKnobs; i++)
        if (fabsf(k[i] - s_layerStartKnob[i]) > kTurnedBy) s_turnedInLayer = true;

    if (layer == kLayerSettings) return;   // the main loop reads s_knob

    for (uint8_t i = 0; i < kNumKnobs; i++)
    {
        const uint8_t p      = kLayerParams[layer][i];
        const float   target = s_param[p];
        if (!s_caught[i]
            && (fabsf(k[i] - target) < kCatchWindow
                || (s_prevKnob[i] - target) * (k[i] - target) <= 0.0f))
            s_caught[i] = true;
        if (s_caught[i]) s_param[p] = k[i];
        s_prevKnob[i] = k[i];
    }
}

/* ── Audio ──────────────────────────────────────────────────────────────── */
static inline int32_t ToCodec(float x)
{
    CONSTRAIN(x, -1.0f, 0.999999f);
    return static_cast<int32_t>(x * 2147483648.0f);
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    const uint32_t t0 = System::GetTick();
    cpu.OnBlockStart();
    if (size > kMaxBlockSize) size = kMaxBlockSize;

    hw.ProcessAllControls();
    ReadKnobs();

    const bool synthMode = s_runInput == kInputSynth;
    const bool mono      = s_runInput == kInputMono;

    // ── Gates: FREEZE (B7 or GATE IN 2), SEED (GATE IN 1, delayed) ──
    const bool frozen = s_freezeButton || hw.gate_in_2.State();
    s_frozen = frozen;
    bool freeze[kMaxBlockSize];
    for (size_t i = 0; i < size; ++i) freeze[i] = frozen;

    const bool seedHigh = hw.gate_in_1.State();
    TrackExtClock(seedHigh && !s_seedWasHigh);
    if (seedHigh && !s_seedWasHigh) s_seedCount = s_seedCount + 1;
    s_seedWasHigh = seedHigh;
    s_seedDelay[s_seedDelayPos] = seedHigh;
    const bool seedDelayed =
        s_seedDelay[(s_seedDelayPos + 16u - kSeedDelayBlocks) & 15u];
    s_seedDelayPos = (s_seedDelayPos + 1u) & 15u;

    GateFlags        seed[kMaxBlockSize];
    const GateFlags* seedInput = nullptr;
    if (s_extClock)
    {
        for (size_t i = 0; i < size; ++i)
            s_previousSeed = seed[i] = stmlib::ExtractGateFlags(s_previousSeed, seedDelayed);
        seedInput = seed;
    }
    else
    {
        s_previousSeed = stmlib::GATE_FLAG_LOW;
    }

    // ── Input, through Beads' AGC ──
    beads::Codec::Frame codecIn[kMaxBlockSize];
    for (size_t i = 0; i < size; ++i)
    {
        codecIn[i].l = ToCodec(in[0][i]);
        codecIn[i].r = ToCodec(in[1][i]);
    }
    FloatFrame floatIn[kMaxBlockSize];
    FloatFrame floatOut[kMaxBlockSize];
    gain_control.Process(s_transition == 0,
                         0,                   // auto gain
                         synthMode,           // left unpatched
                         synthMode || mono,   // right unpatched: L feeds both
                         codecIn, floatIn, size);

    // ── Controls ──
    Parameters p;
    p.time.base         = f_time.Process(s_param[kTime])
                          + c_time.Process(Clamp11(hw.GetAdcValue(CV_6)) * kVoltsPerAdc * kTimeSizePerVolt, 0.5f);
    p.time.attenuverter = f_timeRand.Process(s_param[kTimeRnd]);
    p.time.has_cv       = false;
    p.time.cv           = 0.0f;

    p.size.base         = f_size.Process(s_param[kSize])
                          + c_size.Process(Clamp11(hw.GetAdcValue(CV_7)) * kVoltsPerAdc * kTimeSizePerVolt, 0.5f);
    p.size.attenuverter = f_sizeRand.Process(s_param[kSizeRnd]);
    p.size.has_cv       = false;
    p.size.cv           = 0.0f;

    p.shape.base         = f_shape.Process(s_param[kShape]);
    p.shape.attenuverter = f_shapeRand.Process(s_param[kShapeRnd]);
    p.shape.has_cv       = false;
    p.shape.cv           = 0.0f;

    float pitchCv = joy::VoctToSemitones(g_voct_cal, Clamp11(hw.GetAdcValue(CV_5)));
    CONSTRAIN(pitchCv, -60.0f, 60.0f);
    p.pitch.base         = f_pitch.Process(s_param[kPitch]) + c_pitch.Process(pitchCv, 1.0f);
    p.pitch.attenuverter = f_pitchRand.Process(s_param[kPitchRnd]);
    p.pitch.has_cv       = false;
    p.pitch.cv           = 0.0f;

    p.monophonic_mode = false;

    ModulatedParameter d;
    float densityCv = Clamp11(hw.GetAdcValue(CV_8)) * kVoltsPerAdc * kDensityPerVolt;
    CONSTRAIN(densityCv, -1.0f, 1.0f);
    d.base = f_density.Process(s_param[kDensity]);
    d.cv   = c_density.Process(densityCv, 1.0f);
    ThrottleDensity(d, s_govScale);

    Processor::MixParameters m;
    m.feedback      = f_feedback.Process(s_param[kFeedback]) * 1.005f;
    m.dry_wet       = f_dryWet.Process(s_param[kDryWet]) * 1.005f;
    m.reverb_amount = f_reverb.Process(s_param[kReverb]) * 1.005f;
    CONSTRAIN(m.feedback, 0.0f, 1.0f);
    CONSTRAIN(m.dry_wet, 0.0f, 1.0f);
    CONSTRAIN(m.reverb_amount, 0.0f, 1.0f);
    const bool gateOut = s_gateOut;
    m.output_gate = gateOut;

    // ── After a codec restart: silence while the AGC settles (Beads' 1/4 s) ──
    if (s_transition > 0)
    {
        s_transition = s_transition - 1;
        for (size_t i = 0; i < size; ++i) out[0][i] = out[1][i] = 0.0f;
        hw.gate_out_1.Write(false);
        EndBlock(t0, size);
        return;
    }

    // ── Beads. The internal clock always runs (latched), as Beads boots. ──
    if (synthMode)
        synth.Render(beads::SEED_BUTTON_STATE_LOCKED, seedInput, freeze,
                     Synth::Resolution(s_runMedia), d, p, m, floatOut, size);
    else
        processor.Process(beads::SEED_BUTTON_STATE_LOCKED, seedInput, freeze, d, p, m,
                          floatIn, floatOut, size);

    // With GATE OUT on, Beads mixes to mono on L and puts its gates on R; the
    // gates go to GATE OUT 1 and the mix to both audio outs.
    bool gate = false;
    for (size_t i = 0; i < size; ++i)
    {
        float l = floatOut[i].l, r = floatOut[i].r;
        CONSTRAIN(l, -1.0f, 0.999999f);
        if (gateOut)
        {
            gate = gate || r < -0.4f;
            r    = l;
        }
        CONSTRAIN(r, -1.0f, 0.999999f);
        out[0][i] = l;
        out[1][i] = r;
    }
    hw.gate_out_1.Write(gate);

    EndBlock(t0, size);
}

/* ── Codec and processor configuration (main loop only) ─────────────────── */
// Beads' MonitorStateChange + ConfigureAndStartCodec: stop the codec, set the
// processor to the media and channel count, restart at the media's sample rate,
// and blank the output for a quarter second while the AGC catches up.
static void Configure(uint8_t media, uint8_t input)
{
    const bool synthMode   = input == kInputSynth;
    const bool wasSynth    = s_runInput == kInputSynth;
    const bool monoChanged = (input == kInputMono) != (s_runInput == kInputMono);

    if (s_runRate) hw.StopAudio();

    if (!synthMode) processor.Configure(Processor::Media(media), input == kInputMono);
    const uint32_t rate = synthMode ? 48000u : static_cast<uint32_t>(processor.sample_rate());

    gain_control.Init(
        static_cast<float>(rate),
        media == Processor::MEDIA_DIRTY_TAPE
            ? GainControl::SOFT_CLIPPING_MODE_ON
            : (media == Processor::MEDIA_CLEAN_TAPE ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                                                    : GainControl::SOFT_CLIPPING_MODE_OFF),
        s_runRate == 0 || synthMode || wasSynth || monoChanged);

    s_runMedia   = media;
    s_runInput   = input;
    s_runRate    = rate;
    s_transition = kBlockRate / 4;

    hw.SetAudioBlockSize(rate / kBlockRate);
    hw.SetAudioSampleRate(rate == 32000u ? SaiHandle::Config::SampleRate::SAI_32KHZ
                                         : SaiHandle::Config::SampleRate::SAI_48KHZ);
    cpu.Init(static_cast<float>(rate), static_cast<int>(rate / kBlockRate));
    s_ticksPerSample = static_cast<float>(System::GetTickFreq()) / static_cast<float>(rate);
    hw.StartAudio(AudioCallback);
}

/* ── LED: CV_OUT_2 drives the panel LED, 0..5 V ─────────────────────────── */
// 4 V is Joy Lite's "on". The LED hangs off the DAC through a resistor, so it
// stays dark below its forward voltage; the dim level sits above that.
static constexpr float kLedOn  = 4.0f;
static constexpr float kLedDim = 2.4f;

static void SetLed(float volts) { hw.WriteCvOut(CV_OUT_2, volts); }

// A number blinked out: n flashes, then back to the normal display.
static int      s_blinksLeft = 0;
static bool     s_blinkOn    = false;
static uint32_t s_blinkNext  = 0;

static void BlinkNumber(int n)
{
    s_blinksLeft = n;
    s_blinkOn    = false;
    s_blinkNext  = System::GetNow() + 150u;   // a gap first, so the count reads
}

static void RenderLed(uint32_t now, bool settings)
{
    if (s_blinksLeft > 0)
    {
        if (static_cast<int32_t>(now - s_blinkNext) >= 0)
        {
            if (!s_blinkOn) { s_blinkOn = true;  s_blinkNext = now + 140u; }
            else            { s_blinkOn = false; s_blinkNext = now + 220u; s_blinksLeft--; }
        }
        SetLed(s_blinkOn ? kLedOn : 0.0f);
        return;
    }

    if (settings)
    {
        // Breathing, 2 s: SETTINGS is open.
        int tri = static_cast<int>((now & 2047u) >> 2);
        tri     = tri >= 256 ? 511 - tri : tri;
        SetLed(kLedDim + (kLedOn - kLedDim) * static_cast<float>(tri) / 255.0f);
        return;
    }

    // Lit while frozen; otherwise a flash on each SEED trigger.
    static uint32_t seen = 0, flashUntil = 0;
    if (s_seedCount != seen) { seen = s_seedCount; flashUntil = now + 30u; }
    if (s_frozen)
        SetLed(kLedOn);
    else
        SetLed(static_cast<int32_t>(flashUntil - now) > 0 ? kLedOn : 0.0f);
}

/* ── V/OCT calibration (B7 at power-on), as Joy Lite ────────────────────── */
static void WaitForFreshPress(bool blink)
{
    uint32_t t  = System::GetNow();
    bool     on = true;
    SetLed(kLedOn);
    auto drive = [&]() {
        if (!blink) return;
        const uint32_t now = System::GetNow();
        if (now - t > 150u) { t = now; on = !on; SetLed(on ? kLedOn : 0.0f); }
    };
    do { hw.ProcessAllControls(); b7.Debounce(); drive(); System::Delay(1); } while (b7.Pressed());
    do { hw.ProcessAllControls(); b7.Debounce(); drive(); System::Delay(1); } while (!b7.Pressed());
}

static float CaptureVoct()
{
    constexpr int kN  = 200;
    float         acc = 0.0f;
    for (int i = 0; i < kN; i++)
    {
        hw.ProcessAllControls();
        acc += Clamp11(hw.GetAdcValue(CV_5));
        System::Delay(1);
    }
    return acc / static_cast<float>(kN);
}

static void RunCalibration()
{
    WaitForFreshPress(false);   // LED solid: patch 1 V, press
    const float adc1 = CaptureVoct();
    WaitForFreshPress(true);    // LED blinking: patch 3 V, press
    const float adc3 = CaptureVoct();
    g_voct_cal = joy::ComputeVoctCal(adc1, adc3, g_voct_cal);
    for (int i = 0; i < 10; i++)   // done: a flurry
    {
        SetLed((i & 1) ? kLedOn : 0.0f);
        System::Delay(60);
    }
    SetLed(0.0f);
    do { b7.Debounce(); System::Delay(1); } while (b7.Pressed());   // let go before playing
}

/* ── Saved state ────────────────────────────────────────────────────────── */
static WishSettings Snapshot()
{
    WishSettings s;
    memset(&s, 0, sizeof s);
    s.magic    = kSettingsMagic;
    s.media    = s_media;
    s.input    = s_input;
    s.gate_out = s_gateOut ? 1 : 0;
    for (uint8_t i = 0; i < kNumParams; i++) s.param[i] = s_param[i];
    s.voct_center = g_voct_cal.center;
    s.voct_slope  = g_voct_cal.slope;
    return s;
}

static void LoadSettings()
{
    WishSettings defaults = Snapshot();   // the compiled-in values, set before this
    storage.Init(defaults, kSettingsQspiOffset);
    const WishSettings& s = storage.GetSettings();
    if (s.magic != kSettingsMagic || s.media > 3 || s.input >= kNumInputs || s.gate_out > 1)
    {
        storage.RestoreDefaults();
        return;
    }
    s_media   = s.media;
    s_input   = s.input;
    s_gateOut = s.gate_out != 0;
    for (uint8_t i = 0; i < kNumParams; i++)
        if (s.param[i] >= 0.0f && s.param[i] <= 1.0f) s_param[i] = s.param[i];
    g_voct_cal = {s.voct_center, s.voct_slope};
}

// Worth a QSPI write: a setting changed, or a knob value moved past ADC noise.
static bool Differs(const WishSettings& a, const WishSettings& b)
{
    if (a.media != b.media || a.input != b.input || a.gate_out != b.gate_out
        || a.voct_center != b.voct_center || a.voct_slope != b.voct_slope)
        return true;
    for (uint8_t i = 0; i < kNumParams; i++)
        if (fabsf(a.param[i] - b.param[i]) > 0.02f) return true;
    return false;
}

static void SaveSettings(const WishSettings& s)
{
    storage.GetSettings() = s;
    storage.Save();
}

/* ── Main loop: B7, the toggle, SETTINGS, the LED, saving (1 ms) ────────── */
static constexpr uint32_t kSettingsHoldMs = 2000;

int main()
{
    hw.Init();
    b7.Init(hw.B7, 1000.0f, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);
    // B8 reads "pressed" with the toggle DOWN (Mirth checked it on a stock unit,
    // 2026-10-03; this app's first flash agreed), so pressed is PLAY.
    toggle.Init(hw.B8, 1000.0f, Switch::TYPE_TOGGLE, Switch::POLARITY_NORMAL);
    hw.StartDac();
    SetLed(0.0f);

    memset(s_delay, 0, sizeof s_delay);
    memset(s_reverb, 0, sizeof s_reverb);
    uint8_t* buffers[3] = {s_delay[0], s_delay[1], s_delay[2]};
    processor.Init(buffers, s_reverb);
    synth.Init(s_reverb);

    for (uint8_t i = 0; i < kNumParams; i++) s_param[i] = kParamDefaults[i];
    LoadSettings();

    // Let the controls and switches settle, then read where everything is.
    hw.StartAdc();
    for (int i = 0; i < 40; i++)
    {
        hw.ProcessAllControls();
        b7.Debounce();
        toggle.Debounce();
        System::Delay(1);
    }

    if (b7.Pressed())
    {
        RunCalibration();
        MarkDirty();
    }

    // The layer under the hand takes the knobs as they are; the others keep
    // their saved values until each knob catches them.
    const uint8_t startLayer = toggle.Pressed() ? kLayerPlay : kLayerTone;
    s_layer = startLayer;
    for (uint8_t i = 0; i < kNumKnobs; i++)
        s_param[kLayerParams[startLayer][i]] = Clamp01(hw.GetAdcValue(CV_1 + i));

    // Pot filters start where the parameters are, so nothing glides in at boot.
    f_time.Init(PotFilter::kNormal, 0.001f, s_param[kTime]);
    f_size.Init(PotFilter::kNormal, 0.0f, s_param[kSize]);
    f_shape.Init(PotFilter::kNormal, 0.0f, s_param[kShape]);
    f_density.Init(PotFilter::kNormal, 0.0f, s_param[kDensity]);
    f_pitch.Init(PotFilter::kDetune, 0.0f, s_param[kPitch]);
    f_dryWet.Init(PotFilter::kNormal, 0.0f, s_param[kDryWet]);
    f_timeRand.Init(PotFilter::kAttenuverter, 0.0f, s_param[kTimeRnd]);
    f_sizeRand.Init(PotFilter::kAttenuverter, 0.0f, s_param[kSizeRnd]);
    f_shapeRand.Init(PotFilter::kAttenuverter, 0.0f, s_param[kShapeRnd]);
    f_feedback.Init(PotFilter::kNormal, 0.0f, s_param[kFeedback]);
    f_pitchRand.Init(PotFilter::kAttenuverter, 0.0f, s_param[kPitchRnd]);
    f_reverb.Init(PotFilter::kNormal, 0.0f, s_param[kReverb]);

    hw.gate_out_1.Write(false);
    Configure(s_media, s_input);

    // B7 state.
    bool     b7Was        = false;
    uint32_t b7DownMs     = 0;
    bool     inSettings   = false;
    bool     settingsByThisPress = false;

    // SETTINGS: a knob changes its setting only once it has been turned.
    float settingsRef[3]    = {};
    bool  settingsActive[3] = {};

    WishSettings lastSaved = Snapshot();

    for (;;)
    {
        const uint32_t now = System::GetNow();
        b7.Debounce();
        toggle.Debounce();

        const bool    b7Now     = b7.Pressed();
        const uint8_t baseLayer = toggle.Pressed() ? kLayerPlay : kLayerTone;

        if (b7Now && !b7Was)
        {
            b7DownMs            = now;
            settingsByThisPress = false;
            if (!inSettings) s_layer = kLayerRandom;
        }

        // Held 2 s with nothing turned: open SETTINGS.
        if (b7Now && !inSettings && !s_turnedInLayer && now - b7DownMs >= kSettingsHoldMs)
        {
            inSettings          = true;
            settingsByThisPress = true;
            s_layer             = kLayerSettings;
            for (uint8_t i = 0; i < 3; i++)
            {
                settingsRef[i]    = s_knob[i];
                settingsActive[i] = false;
            }
        }

        if (!b7Now && b7Was)
        {
            if (inSettings)
            {
                // The press that opened SETTINGS leaves it open; the next tap closes it.
                if (!settingsByThisPress) inSettings = false;
            }
            else if (!s_turnedInLayer)
            {
                s_freezeButton = !s_freezeButton;   // a tap
            }
            else
            {
                MarkDirty();   // the RANDOM layer was used
            }
        }
        b7Was = b7Now;

        if (!inSettings && !b7Now)
        {
            if (s_layer != baseLayer) MarkDirty();   // a layer's values are worth keeping
            s_layer = baseLayer;
        }

        if (inSettings)
        {
            for (uint8_t i = 0; i < 3; i++)
            {
                const float k = s_knob[i];
                if (!settingsActive[i] && fabsf(k - settingsRef[i]) > 0.05f) settingsActive[i] = true;
                if (!settingsActive[i]) continue;
                if (i == 0)
                {
                    const uint8_t media = static_cast<uint8_t>(3 - std::min(3, static_cast<int>(k * 4.0f)));
                    if (media != s_media) { s_media = media; BlinkNumber(4 - media); MarkDirty(); }
                }
                else if (i == 1)
                {
                    const uint8_t input = static_cast<uint8_t>(std::min(2, static_cast<int>(k * 3.0f)));
                    if (input != s_input) { s_input = input; BlinkNumber(input + 1); MarkDirty(); }
                }
                else
                {
                    const bool g = k >= 0.5f;
                    if (g != s_gateOut) { s_gateOut = g; BlinkNumber(g ? 2 : 1); MarkDirty(); }
                }
            }
        }

        // A new media or input waits while frozen, as on Beads: the buffer would
        // be reinterpreted, and the frozen sound is the point.
        if (!s_frozen && s_transition == 0 && (s_media != s_runMedia || s_input != s_runInput))
            Configure(s_media, s_input);

        RenderLed(now, inSettings);

        // The Patch SM's own LED, behind the panel: lit while the governor thins grains.
        hw.SetLed(s_govScale < 0.999f);

        if (s_dirty && now - s_dirtySince >= kSaveAfterMs)
        {
            s_dirty = false;
            const WishSettings s = Snapshot();
            if (Differs(s, lastSaved))
            {
                SaveSettings(s);
                lastSaved = s;
            }
        }

        System::Delay(1);
    }
}
