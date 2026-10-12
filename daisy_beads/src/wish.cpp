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
// Wish -- a granular texture synthesizer for the Hermetic Modular Alchemy Lab
// V2, based on Mutable Instruments Beads.
//
// The DSP is Beads' own (pichenettes/eurorack, beads/, MIT, Emilie Gillet),
// compiled from the submodule with one flag changed (daisy_beads/Makefile). This
// file is what Beads' drivers, UI, CV reader and settings store were: the panel,
// the jacks, the audio callback, the codec's sample rate, and saved state. The
// pot laws, CV scaling, gestures and LED colours follow Beads' own code
// (beads.cc, ui.cc, cv_reader.h, settings.cc) wherever the panel allows.
//
//   P1 TIME            P2 SIZE
//   P3 SHAPE           P4 DENSITY
//   P5 PITCH           P6 DRY/WET
//
// Hold B3 for the second layer: Beads' four attenuverters as randomisers, plus
// two of its small knobs. Each randomiser sits on its parameter's own pot.
//
//   P1 TIME random     P2 SIZE random
//   P3 SHAPE random    P4 FEEDBACK
//   P5 PITCH random    P6 REVERB
//
// A randomiser is Beads' attenuverter with nothing patched: centre is none,
// clockwise a smooth random walk, anticlockwise a fresh random value per grain.
// CV is always added at full depth (attenuate off-module), so the randomisers
// stay randomisers whether a jack is patched or not, and V/OCT tracks without
// a knob at full. What is lost is Beads' patched-and-anticlockwise case, CV
// setting the amount of randomness.
//
//   B1        FREEZE, toggles on press (J5 holds it too)
//   B2        SEED: press for a grain, hold to keep seeding; hold 4 s, or hold
//             B2 and press B1, to latch the internal clock (the boot state)
//   B3        tap: QUALITY, Beads' four media, in Beads' order and colours
//             hold: the layer above
//             hold 1.5 s and let go without turning anything: restart the AGC
//   B3 + B1   input: Mono (J1 only) / Stereo / Synth (no input), on B1's LEDs
//   B3 + B2   gate output: J10 carries Beads' grain gates instead of audio R
//
//   J1 IN L    J2 IN R      (audio)
//   J3 V/OCT   J4 SEED      the pair a sequencer drives
//   J5 FREEZE  J6 TIME CV   J7 SIZE CV   J8 DENSITY CV
//   J9 OUT L   J10 OUT R    (audio)
//
// Beads tells patched jacks from unpatched ones with a normalisation probe; the
// Alchemy Lab has none, so two things become explicit. The audio input mode is
// the B3 + B1 setting. SEED counts as patched while triggers arrive and for a
// while after (kExtClockMinMs, or four of its own periods): DENSITY then
// divides or thins the incoming triggers as on Beads, and the internal clock
// comes back once they stop.
//
// Left for later: manual input gain (AGC only), the frozen-buffer backup to
// flash, SHAPE CV and Beads' ASSIGN CV (no jacks left).
//
// Boot gestures belong to the board: hold B3 at power-on for DFU, B1 + B2 for
// the factory CV calibration.

#include <string.h>
#include <algorithm>
#include <type_traits>

#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"
#include "util/scopedirqblocker.h"

#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/host_link/diagnostics.h"
#include "alchemy/host_link/host.h"
#include "alchemy/surface/control_loop.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/pager.h"
#include "alchemy/surface/presets.h"
#include "alchemy/surface/serializable.h"
#include "alchemy/surface/virtual_knob.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/gate_flags.h"

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "beads/gain_control.h"

#include "pot_laws.h"

using alchemy::AlchemyLab;
using alchemy::AlchemyLabV2;
using alchemy::Bipolar;
using alchemy::ControlLoop;
using alchemy::LedPanel;
using alchemy::Level;
using alchemy::Page;
using alchemy::Pager;
using alchemy::Presets;
using alchemy::Serializable;
using alchemy::VirtualKnob;
using alchemy::kButtonB1;
using alchemy::kButtonB2;
using alchemy::kButtonB3;
using alchemy::kNumPots;
using alchemy::kPotBottomLeft;
using alchemy::kPotBottomRight;
using alchemy::kPotMiddleLeft;
using alchemy::kPotMiddleRight;
using alchemy::kPotTopLeft;
using alchemy::kPotTopRight;
namespace hostlink = alchemy::hostlink;

using beads::FloatFrame;
using beads::GainControl;
using beads::ModulatedParameter;
using beads::Parameters;
using beads::Processor;
using beads::SeedButtonState;
using beads::Synth;
using stmlib::GateFlags;

static_assert(std::is_same_v<AlchemyLab, AlchemyLabV2>,
              "Wish needs the Alchemy Lab V2 (-DALCHEMY_BOARD_V2)");

/* ── Rates ──────────────────────────────────────────────────────────────── */
// Beads runs its controls once per codec block at 4 kHz, whatever the sample
// rate: 12 samples at 48 kHz, 8 at 32 kHz. Its filter coefficients and the
// SEED delay below assume that rate, so the block size follows the codec.
static constexpr int    kBlockRate    = 4000;
static constexpr size_t kMaxBlockSize = 24;   // Beads' kMaxCodecBlockSize

/* ── Jacks: indices into hw.cv_jacks[], which start at J3 ───────────────── */
static constexpr uint8_t kJackPitch   = 0;   // J3 V/OCT
static constexpr uint8_t kJackSeed    = 1;   // J4 SEED trigger
static constexpr uint8_t kJackFreeze  = 2;   // J5 FREEZE gate
static constexpr uint8_t kJackTime    = 3;   // J6 TIME CV
static constexpr uint8_t kJackSize    = 4;   // J7 SIZE CV
static constexpr uint8_t kJackDensity = 5;   // J8 DENSITY CV

// Gate thresholds, volts, with hysteresis (as Secret).
static constexpr float kGateOn  = 1.2f;
static constexpr float kGateOff = 0.6f;

// CV scaling, from Beads' default calibration (settings.cc) over its +-5 V
// inputs: TIME/SIZE/SHAPE +-1.6, DENSITY +-1, PITCH +-60 semitones.
static constexpr float kTimeSizePerVolt = 0.32f;
static constexpr float kDensityPerVolt  = 0.2f;
static constexpr float kSemitonesPerVolt = 12.0f;

// SEED counts as patched for this long after a trigger, or four of its own
// periods if longer (Beads' divider resets after the same four), capped.
static constexpr uint32_t kExtClockMinMs = 4000;
static constexpr uint32_t kExtClockMaxMs = 30000;

/* ── Buffers ────────────────────────────────────────────────────────────── */
// Beads' own sizes. On the H743 all of it sat in on-chip SRAM. Here BOOT_SRAM
// fills AXI with code and DTCM holds the rest of the state, so the 768 KB of
// delay lines go to SDRAM.
//
// The reverb, read and written every sample, stays on chip, in D2. The SDK's
// linker script gives D2 only the LED DMA buffer and `.heap`, so it goes in
// `.heap`: newlib's _sbrk grows from `end`, which the script puts after that
// section, so malloc can never reach it. Neither region is zeroed at boot;
// main() clears both.
static uint8_t  s_delay[3][256 * 1024] __attribute__((section(".sdram_bss")));
static uint16_t s_reverb[32768]        __attribute__((section(".heap")));

/* ── Knobs ──────────────────────────────────────────────────────────────── */
enum : uint8_t { kPageMain = 0, kPageLayer = 1, kNumPages = 2 };

static constexpr LedPanel::Rgb kDim        = {0x18, 0x18, 0x18};
static constexpr LedPanel::Rgb kRandWalk   = {0xB0, 0x60, 0xFF};   // clockwise
static constexpr LedPanel::Rgb kRandGrain  = {0xFF, 0x40, 0x90};   // anticlockwise
static constexpr LedPanel::Rgb kLayerColor = {0xB0, 0x60, 0xFF};

static VirtualKnob timeKnob = VirtualKnob(kPotTopLeft, "Time")
    .Ident("time").Ring(Level({0x40, 0xA0, 0xFF}));
static VirtualKnob sizeKnob = VirtualKnob(kPotTopRight, "Size")
    .Ident("size").Ring(Level({0x40, 0xFF, 0x90}));
static VirtualKnob shapeKnob = VirtualKnob(kPotMiddleLeft, "Shape")
    .Ident("shape").Ring(Level({0xFF, 0x60, 0xC0}));
// Noon is no grains; anticlockwise regular, clockwise random (Beads' DENSITY).
static VirtualKnob density = VirtualKnob(kPotMiddleRight, "Density")
    .Ident("density").Ring(Bipolar({0xFF, 0x90, 0x20}, {0x20, 0xD0, 0xFF}, kDim));
static VirtualKnob pitchKnob = VirtualKnob(kPotBottomLeft, "Pitch")
    .Ident("pitch").Ring(Bipolar({0xFF, 0xE0, 0x40}, {0xFF, 0x50, 0x20}, kDim));
static VirtualKnob dryWet = VirtualKnob(kPotBottomRight, "Dry/Wet")
    .Ident("drywet").Ring(Level({0xC0, 0xC0, 0xC0}));

static VirtualKnob timeRand = VirtualKnob(kPotTopLeft, "Time random")
    .Ident("time.rnd").Ring(Bipolar(kRandWalk, kRandGrain, kDim));
static VirtualKnob sizeRand = VirtualKnob(kPotTopRight, "Size random")
    .Ident("size.rnd").Ring(Bipolar(kRandWalk, kRandGrain, kDim));
static VirtualKnob shapeRand = VirtualKnob(kPotMiddleLeft, "Shape random")
    .Ident("shape.rnd").Ring(Bipolar(kRandWalk, kRandGrain, kDim));
static VirtualKnob feedback = VirtualKnob(kPotMiddleRight, "Feedback")
    .Ident("feedback").Ring(Level({0xFF, 0x80, 0x00}));
static VirtualKnob pitchRand = VirtualKnob(kPotBottomLeft, "Pitch random")
    .Ident("pitch.rnd").Ring(Bipolar(kRandWalk, kRandGrain, kDim));
static VirtualKnob reverb = VirtualKnob(kPotBottomRight, "Reverb")
    .Ident("reverb").Ring(Level({0x60, 0x80, 0xFF}));

static Page s_mainPage = Page(kPageMain).Name("Main")
    .Knobs(timeKnob, sizeKnob, shapeKnob, density, pitchKnob, dryWet);
static Page s_layerPage = Page(kPageLayer).Name("Random + FX")
    .Knobs(timeRand, sizeRand, shapeRand, feedback, pitchRand, reverb);

// First-boot values for the layer, by pot. The randomisers start at none.
static constexpr float kLayerDefaults[kNumPots] = {0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.2f};

/* ── Hardware, surfaces and engine ──────────────────────────────────────── */
static AlchemyLab          hw;
static ControlLoop         loop(hw);
static Pager               pager = Pager(kNumPages, kNumPots)
                                       .Shift(hw.buttons[kButtonB3], kPageLayer);
static Presets             presets(hw.seed.qspi);
static daisy::CpuLoadMeter cpu;

// A jack's calibrated voltage. make NOJ3J6=1 is for David's board until its
// replacement arrives (2026-10-12): J3 sits at +12 V with nothing patched, which
// pinned every grain five octaves up, and J6 read no gate under Secret. Both
// then read 0 V: no V/OCT and no TIME CV, everything else as normal.
static inline float JackVolts(uint8_t jack)
{
#ifdef WISH_NOJ3J6
    if (jack == kJackPitch || jack == kJackTime) return 0.0f;
#endif
    return hw.cv_jacks[jack].Volts();
}

static Processor   processor;
static Synth       synth;
static GainControl gain_control;

/* ── Settings ───────────────────────────────────────────────────────────── */
enum : uint8_t { kInputMono = 0, kInputStereo = 1, kInputSynth = 2, kNumInputs = 3 };
static const char* const kInputNames[kNumInputs] = {"Mono", "Stereo", "Synth"};
static const char* const kMediaNames[4] = {"Dirty tape", "Clean tape", "Vintage digital", "Digital"};

// Beads' media palette (ui.cc), the QUALITY LED's colours.
static constexpr LedPanel::Rgb kMediaColors[4] = {
    {128, 0, 48}, {255, 144, 0}, {0, 255, 192}, {192, 192, 208}};
static constexpr LedPanel::Rgb kInputColors[kNumInputs] = {
    {0xFF, 0xFF, 0xFF}, {0x40, 0x80, 0xFF}, {0xFF, 0x40, 0xFF}};

// The saved state: what Beads kept in its settings sector, plus the layer's
// knob values, which on Beads were physical pots. Presets slot 0, saved a few
// seconds after the last change and loaded at boot.
struct WishState : public Serializable
{
    uint8_t media    = Processor::MEDIA_DIGITAL;   // Beads' factory default
    uint8_t input    = kInputMono;
    uint8_t gate_out = 0;
    float   layer[kNumPots];

    WishState() { memcpy(layer, kLayerDefaults, sizeof layer); }

    static constexpr size_t sizeKnob = 3 + sizeof(float) * kNumPots;

    size_t SerializedSize() const override { return sizeKnob; }

    void Serialize(uint8_t* out) const override
    {
        out[0] = media;
        out[1] = input;
        out[2] = gate_out;
        for (uint8_t i = 0; i < kNumPots; i++)
        {
            const float v = pager.Stored(kPageLayer, i);
            memcpy(out + 3 + i * sizeof(float), &v, sizeof(float));
        }
    }

    bool Deserialize(const uint8_t* in) override
    {
        if (in[0] > 3 || in[1] >= kNumInputs || in[2] > 1) return false;
        media    = in[0];
        input    = in[1];
        gate_out = in[2];
        for (uint8_t i = 0; i < kNumPots; i++)
        {
            float v;
            memcpy(&v, in + 3 + i * sizeof(float), sizeof(float));
            layer[i] = (v >= 0.0f && v <= 1.0f) ? v : kLayerDefaults[i];
        }
        return true;
    }

    uint32_t SchemaHash() const override { return 0x57495331u; }   // "WIS1"
};
static WishState s_state;

static bool     s_dirty       = false;
static uint32_t s_dirtySince  = 0;
static constexpr uint32_t kSaveAfterMs = 3000;

static void MarkDirty()
{
    s_dirty      = true;
    s_dirtySince = daisy::System::GetNow();
}

/* ── Control <-> audio ──────────────────────────────────────────────────── */
// Written by the control loop, read by the audio callback, or the reverse.
// Single aligned words; nothing here needs more than that.
static volatile bool     s_freezeButton = false;
static volatile bool     s_seedPressed  = false;
static volatile bool     s_seedLocked   = true;    // Beads boots latched
static volatile bool     s_gateOut      = false;
static volatile bool     s_agcRestart   = false;

static volatile bool     s_frozen       = false;   // button or J5, last block
static volatile uint32_t s_seedCount    = 0;       // SEED high blocks, for the LED
static volatile bool     s_extClock     = false;   // SEED counts as patched
static volatile int      s_transition   = 0;       // blocks of silence left
static volatile float    s_overwrite    = 1.0f;    // Beads' overwrite reserve
static volatile uint8_t  s_vu           = 0;
static volatile bool     s_agcAdjusting = false;

// The configuration the codec and processor are running, which the settings
// above may be ahead of (a change waits while frozen, as on Beads).
static uint8_t  s_runMedia  = 0xFF;
static uint8_t  s_runInput  = 0xFF;
static uint32_t s_runRate   = 0;

/* ── Pot and CV conditioning (pot_laws.h) ──────────────────────────────── */
using wish::CvFilter;
using wish::PotFilter;

static PotFilter f_time, f_size, f_shape, f_density, f_pitch, f_dryWet;
static PotFilter f_timeRand, f_sizeRand, f_shapeRand, f_pitchRand, f_feedback, f_reverb;
static CvFilter  c_time, c_size, c_density, c_pitch;

/* ── Gates ──────────────────────────────────────────────────────────────── */
struct GateIn
{
    bool high = false;
    bool Process(float volts)
    {
        if (!high && volts > kGateOn)       high = true;
        else if (high && volts < kGateOff)  high = false;
        return high;
    }
};
static GateIn g_seed, g_freeze;

// Beads delays SEED by 2.5 ms, ten 4 kHz blocks, so a sequencer's pitch CV has
// settled by the time its trigger starts the grain.
static constexpr uint8_t kSeedDelayBlocks = 10;
static bool     s_seedDelay[16]  = {};
static uint8_t  s_seedDelayPos   = 0;
static GateFlags s_previousSeed  = stmlib::GATE_FLAG_LOW;

// External-clock detection on SEED, in blocks.
static uint32_t s_sinceEdge     = 0xFFFFFFFFu / 2u;
static uint32_t s_extTimeout    = kExtClockMinMs * (kBlockRate / 1000);

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

/* ── Audio ──────────────────────────────────────────────────────────────── */
static inline int32_t ToCodec(float x)
{
    CONSTRAIN(x, -1.0f, 0.999999f);
    return static_cast<int32_t>(x * 2147483648.0f);
}

/* ── Load governor ──────────────────────────────────────────────────────── */
// Measured with make BENCH=1 on 2026-10-12: Beads alone takes up to ~98% of a
// block with dense grains and PITCH random at full, before this callback's own
// work. A block that runs long starves the control loop: LEDs, buttons and USB
// stop until the grains die away (David's first session hit it). So, as Secret
// does, the callback times itself and, past 80%, pulls DENSITY toward noon:
// fewer new grains. It eases off once the load is back under 60%. Grains
// already playing finish as they are; only their successors thin out.
static float          s_ticksPerSample = 0.0f;   // timer ticks per sample, set by Configure()
static volatile float s_govScale       = 1.0f;   // 1 = not throttling
static volatile float s_peakLoad       = 0.0f;   // worst block since the last heartbeat

static float GovernLoad(float scale, float used)
{
    if (used > 0.80f)      scale *= 0.95f;     // halves in ~14 blocks, 3.5 ms
    else if (used < 0.60f) scale += 0.0005f;   // back to 1 in half a second
    return scale < 0.0f ? 0.0f : (scale > 1.0f ? 1.0f : scale);
}

static inline void ThrottleDensity(ModulatedParameter& d, float scale)
{
    d.base = 0.5f + (d.base - 0.5f) * scale;
    d.cv  *= scale;
}

static void EndBlock(uint32_t t0, size_t size)
{
    const float used = static_cast<float>(daisy::System::GetTick() - t0)
                       / (static_cast<float>(size) * s_ticksPerSample);
    if (used > s_peakLoad) s_peakLoad = used;
    s_govScale = GovernLoad(s_govScale, used);
    cpu.OnBlockEnd();
}

static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{
    const uint32_t t0 = daisy::System::GetTick();
    cpu.OnBlockStart();
    if (size > kMaxBlockSize) size = kMaxBlockSize;

    const bool synthMode = s_runInput == kInputSynth;
    const bool mono      = s_runInput == kInputMono;

    // ── Gates: FREEZE (button or J5), SEED (J4, delayed) ──
    const bool freezeGate = g_freeze.Process(JackVolts(kJackFreeze));
    const bool frozen     = s_freezeButton || freezeGate;
    s_frozen = frozen;
    bool freeze[kMaxBlockSize];
    for (size_t i = 0; i < size; ++i) freeze[i] = frozen;

    const bool wasHigh  = g_seed.high;
    const bool seedHigh = g_seed.Process(JackVolts(kJackSeed));
    TrackExtClock(seedHigh && !wasHigh);
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
        if (seedDelayed) s_seedCount = s_seedCount + 1;
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
    if (s_agcRestart)
    {
        s_agcRestart = false;
        gain_control.Restart();
    }
    FloatFrame floatIn[kMaxBlockSize];
    FloatFrame floatOut[kMaxBlockSize];
    gain_control.Process(s_transition == 0,
                         0,                   // auto gain
                         synthMode,           // left unpatched
                         synthMode || mono,   // right unpatched: L feeds both
                         codecIn, floatIn, size);
    s_vu           = gain_control.vu_meter();
    s_agcAdjusting = gain_control.adjusting();

    // ── Controls ──
    Parameters p;
    p.time.base         = f_time.Process(timeKnob.Norm())
                          + c_time.Process(JackVolts(kJackTime) * kTimeSizePerVolt, 0.5f);
    p.time.attenuverter = f_timeRand.Process(timeRand.Norm());
    p.time.has_cv       = false;
    p.time.cv           = 0.0f;

    p.size.base         = f_size.Process(sizeKnob.Norm())
                          + c_size.Process(JackVolts(kJackSize) * kTimeSizePerVolt, 0.5f);
    p.size.attenuverter = f_sizeRand.Process(sizeRand.Norm());
    p.size.has_cv       = false;
    p.size.cv           = 0.0f;

    p.shape.base         = f_shape.Process(shapeKnob.Norm());
    p.shape.attenuverter = f_shapeRand.Process(shapeRand.Norm());
    p.shape.has_cv       = false;
    p.shape.cv           = 0.0f;

    float pitchCv = JackVolts(kJackPitch) * kSemitonesPerVolt;
    CONSTRAIN(pitchCv, -60.0f, 60.0f);
    p.pitch.base         = f_pitch.Process(pitchKnob.Norm()) + c_pitch.Process(pitchCv, 1.0f);
    p.pitch.attenuverter = f_pitchRand.Process(pitchRand.Norm());
    p.pitch.has_cv       = false;
    p.pitch.cv           = 0.0f;

    p.monophonic_mode = false;

    ModulatedParameter d;
    float densityCv = JackVolts(kJackDensity) * kDensityPerVolt;
    CONSTRAIN(densityCv, -1.0f, 1.0f);
    d.base = f_density.Process(density.Norm());
    d.cv   = c_density.Process(densityCv, 1.0f);
    ThrottleDensity(d, s_govScale);

    Processor::MixParameters m;
    m.feedback      = f_feedback.Process(feedback.Norm()) * 1.005f;
    m.dry_wet       = f_dryWet.Process(dryWet.Norm()) * 1.005f;
    m.reverb_amount = f_reverb.Process(reverb.Norm()) * 1.005f;
    CONSTRAIN(m.feedback, 0.0f, 1.0f);
    CONSTRAIN(m.dry_wet, 0.0f, 1.0f);
    CONSTRAIN(m.reverb_amount, 0.0f, 1.0f);
    m.output_gate = s_gateOut;

    const SeedButtonState seedButton =
        s_seedLocked ? beads::SEED_BUTTON_STATE_LOCKED
                     : (s_seedPressed ? beads::SEED_BUTTON_STATE_ON
                                      : beads::SEED_BUTTON_STATE_OFF);

    // ── After a codec restart: silence while the AGC settles (Beads' 1/4 s) ──
    if (s_transition > 0)
    {
        s_transition = s_transition - 1;
        for (size_t i = 0; i < size; ++i) out[0][i] = out[1][i] = 0.0f;
        EndBlock(t0, size);
        return;
    }

    // ── Beads ──
    if (synthMode)
    {
        synth.Render(seedButton, seedInput, freeze, Synth::Resolution(s_runMedia),
                     d, p, m, floatOut, size);
        s_overwrite = 1.0f;
    }
    else
    {
        processor.Process(seedButton, seedInput, freeze, d, p, m,
                          floatIn, floatOut, size);
        s_overwrite = processor.overwrite_reserve();
    }

    for (size_t i = 0; i < size; ++i)
    {
        float l = floatOut[i].l, r = floatOut[i].r;
        CONSTRAIN(l, -1.0f, 0.999999f);
        CONSTRAIN(r, -1.0f, 0.999999f);
        out[0][i] = l;
        out[1][i] = r;
    }

    EndBlock(t0, size);
}

/* ── Codec and processor configuration (control loop only) ──────────────── */
// Beads' MonitorStateChange + ConfigureAndStartCodec: stop the codec, set the
// processor to the media and channel count, restart at the media's sample rate,
// and blank the output for a quarter second while the AGC catches up.
static void Configure(uint8_t media, uint8_t input)
{
    const bool synthMode   = input == kInputSynth;
    const bool wasSynth    = s_runInput == kInputSynth;
    const bool monoChanged = (input == kInputMono) != (s_runInput == kInputMono);

    if (s_runRate) hw.seed.StopAudio();

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

    hw.seed.SetAudioSampleRate(rate == 32000u ? daisy::SaiHandle::Config::SampleRate::SAI_32KHZ
                                              : daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(rate / kBlockRate);
    cpu.Init(static_cast<float>(rate), static_cast<int>(rate / kBlockRate));
    s_ticksPerSample = static_cast<float>(daisy::System::GetTickFreq()) / static_cast<float>(rate);
    hw.StartAudio(AudioCallback);
}

/* ── HostLink: reflashing and diagnostics over the front USB-C ──────────── */
//   node deps/alchemy-sdk/tools/hostlink-cli/hostlink.mjs -p /dev/cu.usbmodem<serial> watch
static hostlink::Host        host("wish", "Wish", "0.1.0-dev", WISH_GIT_HASH);
static hostlink::Diagnostics debug;

static hostlink::Gauge<float>    g_cpuAvg("cpu.avg", "CPU average");
static hostlink::Gauge<float>    g_cpuMax("cpu.max", "CPU peak since the last configuration");
static hostlink::Gauge<uint32_t> g_rate  ("rate",    "Codec sample rate");
static hostlink::Gauge<uint32_t> g_media ("media",   "Quality (0 dirty tape .. 3 digital)");
static hostlink::Gauge<uint32_t> g_input ("input",   "Input (0 mono, 1 stereo, 2 synth)");
static hostlink::Gauge<bool>     g_frozen("frozen",  "Frozen");
static hostlink::Gauge<bool>     g_ext   ("ext",     "SEED clocked externally");
static hostlink::Gauge<bool>     g_locked("locked",  "SEED latched");
static hostlink::Gauge<float>    g_gov   ("gov",     "Load governor on DENSITY (1 = not throttling)");
static hostlink::Gauge<bool>     g_cal   ("cal",     "Board CV calibration loaded");

/* ── 1 ms poll: buttons ─────────────────────────────────────────────────── */
static bool     s_b1 = false, s_b2 = false, s_b3 = false;
static uint32_t s_b2DownMs = 0, s_b3DownMs = 0;
static bool     s_b2Done = false;    // this B2 press has been used up
static bool     s_b3Chord = false;   // this B3 press made a chord
static uint32_t s_b3ChordMs = 0;     // for the LEDs: show the setting a while

static void Poll(uint32_t t_ms)
{
    const bool b1 = hw.buttons[kButtonB1].Pressed();
    const bool b2 = hw.buttons[kButtonB2].Pressed();
    const bool b3 = hw.buttons[kButtonB3].Pressed();
    bool b1Down = b1 && !s_b1;
    bool b2Down = b2 && !s_b2;
    const bool b2Up = !b2 && s_b2;
    const bool b3Down = b3 && !s_b3;
    const bool b3Up   = !b3 && s_b3;
    s_b1 = b1; s_b2 = b2; s_b3 = b3;

    if (b3Down) { s_b3DownMs = t_ms; s_b3Chord = false; }

    // B3 chords: Beads' ASSIGN combos, with B3 in ASSIGN's place.
    if (b3 && b1Down)
    {
        s_state.input = static_cast<uint8_t>((s_state.input + 1u) % kNumInputs);
        debug.Info("Input: %s", kInputNames[s_state.input]);
        MarkDirty();
        s_b3Chord = true; s_b3ChordMs = t_ms;
        b1Down = false;
    }
    if (b3 && b2Down)
    {
        s_state.gate_out = s_state.gate_out ? 0u : 1u;
        s_gateOut = s_state.gate_out != 0;
        debug.Info("Gate output: %s", s_gateOut ? "on (J10)" : "off");
        MarkDirty();
        s_b3Chord = true; s_b3ChordMs = t_ms;
        b2Down = false;
        s_b2Done = true;
    }

    // SEED + FREEZE latches SEED (Beads).
    if (b2 && b1Down && !s_b2Done)
    {
        s_seedLocked  = true;
        s_seedPressed = false;
        s_b2Done      = true;
        b1Down        = false;
    }

    if (b1Down) s_freezeButton = !s_freezeButton;

    if (b2Down)
    {
        s_b2DownMs    = t_ms;
        s_b2Done      = false;
        s_seedLocked  = false;   // a press always unlatches
        s_seedPressed = true;
    }
    if (b2 && !s_b2Done && t_ms - s_b2DownMs >= 4000u)   // long press latches
    {
        s_seedLocked  = true;
        s_seedPressed = false;
        s_b2Done      = true;
    }
    if (b2Up)
    {
        s_seedPressed = false;
        s_b2Done      = false;
    }

    // B3: tap cycles the media; a hold that turned nothing, held 1.5 s,
    // restarts the AGC; a hold that turned something was the layer.
    if (b3Up)
    {
        const bool edited = pager.HoldUsed(hw.buttons[kButtonB3]);
        if (edited)
        {
            MarkDirty();
        }
        else if (!s_b3Chord)
        {
            if (t_ms - s_b3DownMs < 1500u)
            {
                s_state.media = static_cast<uint8_t>((s_state.media + 3u) % 4u);
                debug.Info("Quality: %s", kMediaNames[s_state.media]);
                MarkDirty();
            }
            else
            {
                s_agcRestart = true;
                debug.Info("AGC restarted");
            }
        }
    }
}

/* ── Frame (16 ms): apply settings, save, report ───────────────────────── */
static void Frame()
{
    // A new media or input waits while frozen, as on Beads: the buffer would
    // be reinterpreted, and the frozen sound is the point.
    if (!s_frozen && s_transition == 0
        && (s_state.media != s_runMedia || s_state.input != s_runInput))
    {
        Configure(s_state.media, s_state.input);
    }

    const uint32_t now = daisy::System::GetNow();
    if (s_dirty && now - s_dirtySince >= kSaveAfterMs)
    {
        s_dirty = false;
        if (!presets.Save(0)) debug.Warn("Saving state failed");
    }

    g_cpuAvg.Set(cpu.GetAvgCpuLoad() * 100.0f);
    g_cpuMax.Set(cpu.GetMaxCpuLoad() * 100.0f);
    g_rate.Set(s_runRate);
    g_media.Set(s_runMedia);
    g_input.Set(s_runInput);
    g_frozen.Set(s_frozen);
    g_ext.Set(s_extClock);
    g_locked.Set(s_seedLocked);
    g_gov.Set(s_govScale);

    // The Seed's own LED: lit while the governor is thinning grains.
    hw.seed.SetLed(s_govScale < 0.999f);

    // Heartbeat, every 5 s: the worst block since the last one, the governor,
    // and what is running. If the module ever freezes, the beats stop, and the
    // last one says how loaded the audio was just before.
    static uint32_t lastBeatMs = 0;
    if (now - lastBeatMs >= 5000u)
    {
        lastBeatMs = now;
        float peak;
        {
            daisy::ScopedIrqBlocker block;
            peak       = s_peakLoad;
            s_peakLoad = 0.0f;
        }
        debug.Info("load avg %.0f%% peak %.0f%%, gov %.2f | %s, %s, %lu Hz%s%s",
                   (double)(cpu.GetAvgCpuLoad() * 100.0f), (double)(peak * 100.0f),
                   (double)s_govScale,
                   kMediaNames[s_runMedia < 4 ? s_runMedia : 3],
                   kInputNames[s_runInput < kNumInputs ? s_runInput : 0],
                   (unsigned long)s_runRate,
                   s_frozen ? ", frozen" : "",
                   s_extClock ? ", SEED clocked" : "");
    }
}

/* ── LEDs: the three buttons, after the SDK has drawn the rings ─────────── */
static void Render(uint32_t t_ms)
{
    LedPanel& leds = hw.leds;
    auto put = [&](uint8_t b, LedPanel::ButtonLed which, LedPanel::Rgb c, float k) {
        leds.SetButton(b, which, leds.ScaleGlobal(LedPanel::Scale(c, k)));
    };

    // Beads' triangle, 2 s period, 0..255.
    int tri = static_cast<int>((t_ms & 2047u) >> 2);
    tri     = tri >= 256 ? 511 - tri : tri;

    // While B3 is down, or just after a chord, B1 and B2 show what their chord
    // sets: the input mode, and whether J10 carries gates.
    const bool showSettings = s_b3 || (t_ms - s_b3ChordMs < 1000u && s_b3ChordMs != 0);
    if (showSettings)
    {
        const LedPanel::Rgb in = kInputColors[s_state.input < kNumInputs ? s_state.input : 0];
        put(kButtonB1, LedPanel::ButtonLed::Top, in, 1.0f);
        put(kButtonB1, LedPanel::ButtonLed::Bottom, in, 1.0f);
        const float g = s_state.gate_out ? 1.0f : 0.08f;
        put(kButtonB2, LedPanel::ButtonLed::Top, {0xFF, 0xC0, 0x40}, g);
        put(kButtonB2, LedPanel::ButtonLed::Bottom, {0xFF, 0xC0, 0x40}, g);
    }
    else
    {
        // FREEZE.
        const float f = s_frozen ? 1.0f : 0.0f;
        put(kButtonB1, LedPanel::ButtonLed::Top, {0xC0, 0xE0, 0xFF}, f);
        put(kButtonB1, LedPanel::ButtonLed::Bottom, {0xC0, 0xE0, 0xFF}, f);

        // SEED: breathing while latched, else lit by the button or a trigger.
        static uint32_t seenCount = 0, flashUntil = 0;
        if (s_seedCount != seenCount) { seenCount = s_seedCount; flashUntil = t_ms + 40u; }
        float s;
        if (s_seedLocked)
            s = static_cast<float>((tri * 3 >> 2) + 64) / 255.0f;
        else
            s = (s_seedPressed || static_cast<int32_t>(flashUntil - t_ms) > 0) ? 1.0f : 0.0f;
        put(kButtonB2, LedPanel::ButtonLed::Top, {0xFF, 0xC0, 0x40}, s);
        put(kButtonB2, LedPanel::ButtonLed::Bottom, {0xFF, 0xC0, 0x40}, s);
    }

    // QUALITY: the media colour, dimmed by the overwrite reserve as on Beads;
    // underneath, the input level, blinking while the AGC is still learning.
    // A new media chosen while frozen waits for the unfreeze; until then its
    // colour blinks, so a change that hasn't happened yet doesn't look done.
    const uint8_t media   = s_state.media < 4 ? s_state.media : 3;
    const bool    pending = s_state.media != s_runMedia || s_state.input != s_runInput;
    const float   blink   = pending && (t_ms & 256u) ? 0.15f : 1.0f;
    put(kButtonB3, LedPanel::ButtonLed::Top, kMediaColors[media], s_overwrite * blink);
    const float vu = s_agcAdjusting ? ((t_ms & 128u) ? 0.5f : 0.0f)
                                    : static_cast<float>(s_vu) / 255.0f;
    put(kButtonB3, LedPanel::ButtonLed::Bottom, {0x40, 0xFF, 0x40}, vu);
}

#ifdef WISH_BENCH
/* ── make BENCH=1: Beads' worst cases on this board, before audio starts ── */
// Beads was written to fit a 400 MHz H743 with its buffers on chip. Here they
// are in SDRAM, so a grain read is a cache line from SDRAM; at high pitch a
// grain strides through the buffer and nearly every read misses. Each case runs
// flat out in the control loop, 1 s to fill the grain pool and 1 s measured,
// and logs its load as a share of the audio block. Over 100% would starve the
// control loop: the LEDs, buttons and USB all stop.
static void Bench()
{
    struct Case
    {
        const char* name;
        uint8_t     media;
        bool        mono, synth;
        float       density, size, pitch, pitch_rnd;
        bool        governed;   // run the callback's governor on the measured load
    };
    static const Case kCases[] = {
        // name                                media mono   synth  dens  size  pitch  rnd   governed
        {"digital mono, dense",                  3, true,  false, 1.0f, 0.9f,  0.0f, 0.5f, false},
        {"digital mono, dense, pitch walk",      3, true,  false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"digital mono, dense, pitch per grain", 3, true,  false, 1.0f, 0.9f, 24.0f, 0.0f, false},
        {"digital mono, regular max, walk",      3, true,  false, 0.0f, 0.9f, 24.0f, 1.0f, false},
        {"digital stereo, dense, pitch walk",    3, false, false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"vintage mono, dense, pitch walk",      2, true,  false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"vintage stereo, dense, pitch walk",    2, false, false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"clean tape mono, dense, pitch walk",   1, true,  false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"clean tape stereo, dense, pitch walk", 1, false, false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"dirty tape mono, dense, pitch walk",   0, true,  false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"dirty tape stereo, dense, pitch walk", 0, false, false, 1.0f, 0.9f, 24.0f, 1.0f, false},
        {"delay mode, feedback 0.9",             3, true,  false, 0.75f, 1.0f, 0.0f, 0.5f, false},
        {"synth, dense, pitch walk",             3, true,  true,  1.0f, 0.9f, 24.0f, 1.0f, false},
        {"digital stereo, dense, walk, GOVERNED", 3, false, false, 1.0f, 0.9f, 24.0f, 1.0f, true},
    };

    const float ticksPerSecond = static_cast<float>(daisy::System::GetTickFreq());
    uint8_t*    buffers[3]     = {s_delay[0], s_delay[1], s_delay[2]};
    uint32_t    noise          = 12345u;

    debug.Info("bench: %lu MHz", (unsigned long)(daisy::System::GetSysClkFreq() / 1000000u));
    for (const Case& c : kCases)
    {
        processor.Init(buffers, s_reverb);
        synth.Init(s_reverb);
        if (!c.synth) processor.Configure(Processor::Media(c.media), c.mono);
        const float  rate  = c.synth ? 48000.0f : processor.sample_rate();
        const size_t block = static_cast<size_t>(rate) / kBlockRate;
        const float  budget = static_cast<float>(block) / rate * ticksPerSecond;

        Parameters p;
        p.time  = {0.1f, 0.0f, 0.0f, false};
        p.size  = {c.size, 0.0f, 0.0f, false};
        p.shape = {0.5f, 0.0f, 0.0f, false};
        p.pitch = {c.pitch, 2.0f * c.pitch_rnd - 1.0f, 0.0f, false};
        p.monophonic_mode = false;
        ModulatedParameter d0;
        d0.base = c.density;
        d0.cv   = 0.0f;
        float gov = 1.0f;
        Processor::MixParameters m;
        m.feedback      = c.size >= 1.0f ? 0.9f : 0.3f;
        m.dry_wet       = 1.0f;
        m.reverb_amount = 0.5f;
        m.output_gate   = false;

        FloatFrame in[kMaxBlockSize], out[kMaxBlockSize];
        bool       freeze[kMaxBlockSize] = {};
        float      sum = 0.0f, worst = 0.0f;
        constexpr int kBlocks = kBlockRate;   // 1 s
        for (int b = 0; b < 2 * kBlocks; b++)
        {
            for (size_t i = 0; i < block; i++)
            {
                noise = noise * 1664525u + 1013904223u;
                in[i].l = in[i].r = static_cast<float>(static_cast<int32_t>(noise)) * 2.3e-10f;
            }
            ModulatedParameter d = d0;
            ThrottleDensity(d, gov);
            const uint32_t t0 = daisy::System::GetTick();
            if (c.synth)
                synth.Render(beads::SEED_BUTTON_STATE_LOCKED, nullptr, freeze,
                             Synth::Resolution(c.media), d, p, m, out, block);
            else
                processor.Process(beads::SEED_BUTTON_STATE_LOCKED, nullptr, freeze, d, p, m,
                                  in, out, block);
            const float load = static_cast<float>(daisy::System::GetTick() - t0) / budget;
            if (c.governed) gov = GovernLoad(gov, load);
            if (b >= kBlocks)
            {
                sum += load;
                if (load > worst) worst = load;
            }
        }
        debug.Info("bench %-38s %5.0f Hz  avg %3.0f%%  worst %3.0f%%%s", c.name, (double)rate,
                   (double)(100.0f * sum / kBlocks), (double)(100.0f * worst),
                   c.governed ? "  (governor on)" : "");
    }
    processor.Init(buffers, s_reverb);
    synth.Init(s_reverb);
}
#endif

/* ── 480 MHz: wrap the SDK's seed.Init() (see the Makefile) ─────────────── */
#ifdef WISH_BOOST
extern "C" void __real__ZN5daisy9DaisySeed4InitEb(daisy::DaisySeed* seed, bool boost);
extern "C" void __wrap__ZN5daisy9DaisySeed4InitEb(daisy::DaisySeed* seed, bool /*boost*/)
{
    __real__ZN5daisy9DaisySeed4InitEb(seed, true);
}
#endif

int main()
{
    hw.Init();

    // All six middle jacks are inputs.
    for (uint8_t j = 0; j < 6; j++) hw.cv_jacks[j].DisableCvOutput();

    memset(s_delay, 0, sizeof s_delay);
    memset(s_reverb, 0, sizeof s_reverb);
    uint8_t* buffers[3] = {s_delay[0], s_delay[1], s_delay[2]};
    processor.Init(buffers, s_reverb);
    synth.Init(s_reverb);

    // Saved state, then the layer's values onto the pager. Catch applies: a
    // layer knob answers once it passes its saved value.
    presets.Manage(s_state);
    presets.Init();
    presets.BootLoad();
    s_gateOut = s_state.gate_out != 0;
    {
        float phys[kNumPots];
        for (uint8_t i = 0; i < kNumPots; i++) phys[i] = hw.pots[i].Value();
        for (uint8_t i = 0; i < kNumPots; i++)
            pager.SetStored(kPageLayer, i, s_state.layer[i], phys);
    }
    pager.SetPageColor(kPageLayer, kLayerColor);

    // Pot filters start where the knobs are, so nothing glides in at boot.
    f_time.Init(PotFilter::kNormal, 0.001f, hw.pots[kPotTopLeft].Value());
    f_size.Init(PotFilter::kNormal, 0.0f, hw.pots[kPotTopRight].Value());
    f_shape.Init(PotFilter::kNormal, 0.0f, hw.pots[kPotMiddleLeft].Value());
    f_density.Init(PotFilter::kNormal, 0.0f, hw.pots[kPotMiddleRight].Value());
    f_pitch.Init(PotFilter::kDetune, 0.0f, hw.pots[kPotBottomLeft].Value());
    f_dryWet.Init(PotFilter::kNormal, 0.0f, hw.pots[kPotBottomRight].Value());
    f_timeRand.Init(PotFilter::kAttenuverter, 0.0f, s_state.layer[kPotTopLeft]);
    f_sizeRand.Init(PotFilter::kAttenuverter, 0.0f, s_state.layer[kPotTopRight]);
    f_shapeRand.Init(PotFilter::kAttenuverter, 0.0f, s_state.layer[kPotMiddleLeft]);
    f_feedback.Init(PotFilter::kNormal, 0.0f, s_state.layer[kPotMiddleRight]);
    f_pitchRand.Init(PotFilter::kAttenuverter, 0.0f, s_state.layer[kPotBottomLeft]);
    f_reverb.Init(PotFilter::kNormal, 0.0f, s_state.layer[kPotBottomRight]);

    g_cpuAvg.Unit("%");
    g_cpuMax.Unit("%");
    g_rate.Unit("Hz");
    hostlink::GaugeBase* gauges[] = {&g_cpuAvg, &g_cpuMax, &g_rate,   &g_media, &g_input,
                                     &g_frozen, &g_ext,    &g_locked, &g_gov, &g_cal};
    for (hostlink::GaugeBase* g : gauges) debug.Watch(*g);
    g_cal.Set(hw.IsCalibrated());
    host.Extend(debug);

#ifdef WISH_BENCH
    Bench();   // before audio starts: see Bench()
#endif

    Configure(s_state.media, s_state.input);

    loop.Use(pager)
        .Use(s_mainPage)
        .Use(s_layerPage)
        .Use(host)
        .OnPoll(Poll)
        .OnFrame(Frame)
        .OnRender(Render);

    if (!host.ConfigurationOk() || !debug.ConfigurationOk())
        debug.Error("HostLink setup failed");
#ifdef WISH_NOJ3J6
    static const char* const kJacks = ", J3 and J6 off (NOJ3J6)";
#else
    static const char* const kJacks = "";
#endif
    debug.Info("Wish %s, %lu MHz, CV %s, %s, %s%s", WISH_GIT_HASH,
               (unsigned long)(daisy::System::GetSysClkFreq() / 1000000u),
               hw.IsCalibrated() ? "calibrated" : "UNCALIBRATED (hold B1+B2 at power-on)",
               kMediaNames[s_runMedia], kInputNames[s_runInput], kJacks);

    for (;;) loop.Tick();
}
