// SORROW — 3 synthetic drum voices + X/Y pattern sequencer for the Daisy Patch
// Init. The synthesis, panel handling and UI here are original work; the
// patterns come from grids_port, which is derived from Mutable Instruments
// Grids by Emilie Gillet.
//
// Not affiliated with or endorsed by Mutable Instruments or Electrosmith; the
// name Grids is used only to describe lineage. See README.md.
//
// License: GPL-3.0-or-later. Grids is GPL — unlike most of the Mutable
// `eurorack` sources, which are MIT — so this firmware is copyleft as a whole,
// including this file, however original it is on its own. See daisy_grids/LICENSE.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 David Baghurst

#include "daisy_patch_sm.h"
#include "daisysp.h"

#include "grids_port.h"

#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// =============================================================================
// Grids Drum Sequencer for Daisy Patch.Init
// 
// B8 Toggle Switch: Internal synth drums (off) / External triggers (on)
//   External triggers: B5=Kick, B6=Snare, CV_OUT_1=HiHat
//   Flipping External -> Internal rolls a fresh randomized kit.
//
// B10 (Gate In 1): External clock input - rising edge advances step
// B9  (Gate In 2): Reset input - rising edge resets pattern to step 0
//
// B7 Momentary Button: press cycles sub-modes
//   (LED flashes = mode index; Pattern = no flash)
//     Mode 0 (no flash): Pattern - CV1=X, CV2=Y, CV3=Density, CV4=Randomness
//     Mode 1 (1 pulse):  Edit Kick  - CV1=Freq, CV2=Decay, CV3=Pan
//     Mode 2 (2 pulses): Edit Snare - CV1=Freq, CV2=Snappy, CV3=Pan
//     Mode 3 (3 pulses): Edit HiHat - CV1=Freq, CV2=Decay, CV3=Pan
//   Edit knobs use soft-takeover: a knob only grabs its parameter once it
//   crosses the stored value, so entering a mode never makes params jump.
//
// A fresh kit is also rolled at power-up. The LED blips to confirm each roll.
// =============================================================================

namespace
{

DaisyPatchSM patch;
daisy_grids::grids_port::GridsDrumGenerator grids;

// -----------------------------------------------------------------------------
// Drum Voices - Synthetic kit for internal mode
// -----------------------------------------------------------------------------
SyntheticBassDrum  kick;
SyntheticSnareDrum snare;
HiHat<>            hat;

// Accent levels for Grids triggers
static constexpr float kKickAccentNormal  = 0.55f;
static constexpr float kKickAccentStrong  = 1.00f;
static constexpr float kSnareAccentNormal = 0.45f;
static constexpr float kSnareAccentStrong = 1.00f;
static constexpr float kHatAccentNormal   = 0.55f;
static constexpr float kHatAccentStrong   = 1.00f;

template <typename Drum>
static inline void TrigWithAccent(Drum& drum,
                                  bool  accent,
                                  float normal_level,
                                  float strong_level)
{
    drum.SetAccent(accent ? strong_level : normal_level);
    drum.Trig();
}

// -----------------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------------
static inline float Clamp01f(float v)
{
    return fminf(fmaxf(v, 0.0f), 1.0f);
}

static inline float Clamp11f(float v)
{
    return fminf(fmaxf(v, -1.0f), 1.0f);
}

static inline float FastSaturate(float x)
{
    const float a = fabsf(x);
    return x / (1.0f + a);
}

// -----------------------------------------------------------------------------
// UI State
// -----------------------------------------------------------------------------
static Switch   s_mode_btn;       // B7 - momentary, cycles sub-modes
static Switch   s_output_sw;      // B8 - toggle, internal vs external output
static uint8_t  g_sub_mode = 0;   // 0=Pattern, 1=EditKick, 2=EditSnare, 3=EditHat
static constexpr uint8_t kNumSubModes = 4;
static bool     g_external_output = false;  // false=internal synth, true=external triggers
static uint32_t g_led_ctr     = 0;

// Per-drum pan parameters (pan: 0=left, 0.5=center, 1=right)
// Equal-power gains precomputed at control rate: L = sqrt(1-pan), R = sqrt(pan)
static float g_kick_pan  = 0.5f;
static float g_kick_gl   = 0.707f, g_kick_gr   = 0.707f;
static float g_snare_pan = 0.5f;
static float g_snare_gl  = 0.707f, g_snare_gr  = 0.707f;
static float g_hat_pan   = 0.5f;
static float g_hat_gl    = 0.707f, g_hat_gr    = 0.707f;

// -----------------------------------------------------------------------------
// Editable macro parameters (normalized 0..1) per drum.
// p1/p2 map to the two knob-controlled params; pan reuses the g_*_pan globals.
// Defaults match the engine Init() values below so boot sound is unchanged.
//   Kick:  p1=freq(30..150Hz),  p2=decay(0.05..0.55)
//   Snare: p1=freq(100..400Hz), p2=snappy(0..1)
//   Hat:   p1=freq(4k..16kHz),  p2=decay(0.02..0.82)
// -----------------------------------------------------------------------------
static float g_kick_p1  = 0.208f, g_kick_p2  = 0.34f;
static float g_snare_p1 = 0.283f, g_snare_p2 = 0.75f;
static float g_hat_p1   = 0.333f, g_hat_p2   = 0.6625f;

// Soft-takeover state: a knob only grabs its parameter once it crosses the
// stored value, so entering an edit mode (or rolling a new kit) doesn't make
// params jump to the knob's current physical position. Indices: 0=k1,1=k2,2=k3.
static bool  g_tk_caught[3] = {false, false, false};
static float g_tk_prev[3]   = {0.0f, 0.0f, 0.0f};
static bool  g_tk_init      = true;   // reset prev[] on next edit-mode callback
static constexpr float kCatchEps = 0.03f;

// Brief LED confirmation flash after a randomize gesture.
static uint32_t g_rand_flash = 0;
static constexpr uint32_t kFlashSamples = 8000;  // ~0.17s at 48kHz

// B7 press lockout: after a mode change, ignore further press edges for a
// short window so contact bounce / electrical noise can't advance the mode
// more than once per physical press. ~200ms is well above any bounce but
// still allows brisk deliberate presses.
static uint32_t g_btn_lockout = 0;
static constexpr uint32_t kBtnLockoutSamples = 9600;  // ~0.20s at 48kHz

// Output drive into the soft saturator. The saturator (x/(1+|x|)) otherwise
// pulls a single hit down ~7dB; driving it harder restores level and adds
// punch while still limiting peaks below full scale.
static constexpr float kOutputDrive = 3.5f;

// Lightweight xorshift RNG for kit randomization.
static uint32_t g_rng = 0x1234567u;
static inline float RandF01()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return static_cast<float>(g_rng & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

static constexpr float kLedVoltsOn = 5.0f;

// -----------------------------------------------------------------------------
// External Clock & Reset (B10, B9)
// Manual edge detection with state tracking for reliability
// Clock multiplier: external clock is assumed to be quarter notes (1 ppqn)
// We multiply by 8 to get 8 steps per quarter note (32 steps per bar for Grids)
// Once external clock is detected, it stays latched (reset only resets position)
// -----------------------------------------------------------------------------
static bool g_ext_clk_state = false;  // Current clock state
static bool g_ext_rst_state = false;  // Current reset state
static bool g_use_ext_clock = false;  // True if external clock mode (latches on first edge)
static bool g_awaiting_first_clock = false; // After reset, suppress sub-ticks until next clock edge

// Clock multiplier state (8× to convert quarter notes to 32nd-note Grids steps)
// Grids has 32 steps per bar = 8 steps per quarter note, so 1 ppqn × 8 = 32 steps/bar
static constexpr uint8_t kClockMultiplier = 8;
static uint32_t g_ext_clk_period = 0;     // Samples between last two clock edges
static uint32_t g_ext_clk_last_edge = 0;  // Sample count at last clock edge
static uint32_t g_ext_clk_counter = 0;    // Counter for generating multiplied ticks
static uint8_t  g_ext_clk_mult_phase = 0; // Which of the 8 sub-ticks we're on
static uint32_t g_sample_counter = 0;     // Global sample counter

// -----------------------------------------------------------------------------
// External Trigger Outputs (Mode 1)
// B5 = Kick, B6 = Snare, CV_OUT_1 = HiHat
// -----------------------------------------------------------------------------
static GPIO g_gate_kick;   // B5
static GPIO g_gate_snare;  // B6
static constexpr float kTriggerVolts = 5.0f;

// Trigger duration in samples (approx 10ms at 48kHz)
static constexpr uint32_t kTriggerSamples = 480;
static uint32_t g_trig_kick_remaining  = 0;
static uint32_t g_trig_snare_remaining = 0;
static uint32_t g_trig_hat_remaining   = 0;

static inline void SetPanelLed(bool on)
{
    // Panel LED on Patch.Init is driven via CV_OUT_2
    patch.WriteCvOut(CV_OUT_2, on ? kLedVoltsOn : 0.0f);
}

static inline bool LedPulseState(uint8_t count, float t)
{
    // Pulse train: short pulses separated by gaps, repeated cyclically
    constexpr float kCycleLen  = 2.0f;
    constexpr float kPulseOn   = 0.15f;
    constexpr float kPulseGap  = 0.12f;
    float cyc = fmodf(t, kCycleLen);
    for(uint8_t i = 0; i < count; i++)
    {
        float s = i * (kPulseOn + kPulseGap);
        float e = s + kPulseOn;
        if(cyc >= s && cyc < e)
            return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Macro application - map normalized p1/p2 to each engine's parameters.
// -----------------------------------------------------------------------------
static inline void ApplyKick()
{
    kick.SetFreq(30.0f + g_kick_p1 * 120.0f);
    kick.SetDecay(0.05f + g_kick_p2 * 0.50f);
}
static inline void ApplySnare()
{
    snare.SetFreq(100.0f + g_snare_p1 * 300.0f);
    snare.SetSnappy(g_snare_p2);
}
static inline void ApplyHat()
{
    hat.SetFreq(4000.0f + g_hat_p1 * 12000.0f);
    hat.SetDecay(0.02f + g_hat_p2 * 0.80f);
}

// Soft-takeover: update `stored` from `knob` only after the knob has caught up
// (crossed the stored value, or landed within kCatchEps). `prev` is the knob's
// value from the previous callback, used for crossing detection.
static inline void SoftTakeover(float& stored, float knob, bool& caught, float& prev)
{
    if(!caught)
    {
        const bool crossed = (prev - stored) * (knob - stored) <= 0.0f;
        if(crossed || fabsf(knob - stored) < kCatchEps)
            caught = true;
    }
    if(caught)
        stored = knob;
    prev = knob;
}

// -----------------------------------------------------------------------------
// Kit randomization - roll fresh, range-bounded macros so it always lands
// somewhere musical. Each randomize also reaches the "hidden" engine params
// that have no knob, then applies the result immediately.
// -----------------------------------------------------------------------------
static inline void RandomizeKick()
{
    g_kick_p1  = 0.05f + RandF01() * 0.45f;
    g_kick_p2  = 0.25f + RandF01() * 0.65f;
    g_kick_pan = 0.25f + RandF01() * 0.50f;
    kick.SetTone(0.10f + RandF01() * 0.50f);
    kick.SetDirtiness(RandF01() * 0.40f);
    kick.SetFmEnvelopeAmount(RandF01() * 0.50f);
    kick.SetFmEnvelopeDecay(0.05f + RandF01() * 0.35f);
    ApplyKick();
    g_kick_gl = sqrtf(1.0f - g_kick_pan);
    g_kick_gr = sqrtf(g_kick_pan);
}
static inline void RandomizeSnare()
{
    g_snare_p1  = 0.10f + RandF01() * 0.60f;
    g_snare_p2  = 0.30f + RandF01() * 0.65f;
    g_snare_pan = 0.25f + RandF01() * 0.50f;
    snare.SetFmAmount(RandF01() * 0.60f);
    snare.SetDecay(0.04f + RandF01() * 0.16f);
    ApplySnare();
    g_snare_gl = sqrtf(1.0f - g_snare_pan);
    g_snare_gr = sqrtf(g_snare_pan);
}
static inline void RandomizeHat()
{
    g_hat_p1  = 0.20f + RandF01() * 0.70f;
    g_hat_p2  = 0.20f + RandF01() * 0.65f;
    g_hat_pan = 0.25f + RandF01() * 0.50f;
    hat.SetTone(0.40f + RandF01() * 0.50f);
    hat.SetNoisiness(0.70f + RandF01() * 0.30f);
    ApplyHat();
    g_hat_gl = sqrtf(1.0f - g_hat_pan);
    g_hat_gr = sqrtf(g_hat_pan);
}

// Context-aware: in an edit mode reroll just that drum; in Pattern mode (0)
// reroll the whole kit. Resets soft-takeover so knobs must re-catch the new kit.
static inline void RandomizeContext(uint8_t mode)
{
    switch(mode)
    {
        case 1: RandomizeKick();  break;
        case 2: RandomizeSnare(); break;
        case 3: RandomizeHat();   break;
        default:
            RandomizeKick();
            RandomizeSnare();
            RandomizeHat();
            break;
    }
    g_tk_caught[0] = g_tk_caught[1] = g_tk_caught[2] = false;
    g_tk_init      = true;
    g_rand_flash   = kFlashSamples;
}

// -----------------------------------------------------------------------------
// Internal Clock - 120 BPM, 16th notes (8 ticks/sec) for pattern mode
// -----------------------------------------------------------------------------
static uint32_t g_internal_clk_samples = 0;

static inline bool InternalGridsClockTick(float sample_rate_hz, size_t block_size)
{
    constexpr float kTicksPerSec = 8.0f;
    const float     sr           = fmaxf(sample_rate_hz, 1.0f);
    const uint32_t  samples_per_tick
        = static_cast<uint32_t>((sr / kTicksPerSec) + 0.5f);

    g_internal_clk_samples += static_cast<uint32_t>(block_size);
    if(g_internal_clk_samples >= samples_per_tick)
    {
        g_internal_clk_samples -= samples_per_tick;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Edit Mode Clock - 1 beat per second for drum parameter editing
// -----------------------------------------------------------------------------
static uint32_t g_edit_clk_samples = 0;

static inline bool EditModeClockTick(float sample_rate_hz, size_t block_size)
{
    constexpr float kTicksPerSec = 1.0f;  // 1 beat per second
    const float     sr           = fmaxf(sample_rate_hz, 1.0f);
    const uint32_t  samples_per_tick
        = static_cast<uint32_t>((sr / kTicksPerSec) + 0.5f);

    g_edit_clk_samples += static_cast<uint32_t>(block_size);
    if(g_edit_clk_samples >= samples_per_tick)
    {
        g_edit_clk_samples -= samples_per_tick;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Audio Callback
// Mode 0: Internal synth drums | Mode 1: External triggers
// -----------------------------------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    (void)in;

    // -----------------------------------------------------------------
    // Read gate inputs FIRST, before any other processing
    // This ensures we catch short pulses reliably
    // -----------------------------------------------------------------
    const bool clk_now = patch.gate_in_1.State();
    const bool rst_now = patch.gate_in_2.State();
    const bool ext_clk_rising = clk_now && !g_ext_clk_state;
    const bool ext_rst_rising = rst_now && !g_ext_rst_state;
    g_ext_clk_state = clk_now;
    g_ext_rst_state = rst_now;

    // Process all controls inside callback (proven pattern from working patches)
    patch.ProcessAllControls();

    // Update UI state
    s_mode_btn.Debounce();
    s_output_sw.Debounce();

    // B7 momentary button: press cycles sub-modes (pattern + 3 edit modes).
    // A lockout after each accepted press rejects contact bounce/noise so one
    // physical press advances the mode exactly once.
    if(g_btn_lockout > 0)
        g_btn_lockout = (g_btn_lockout > size) ? g_btn_lockout - size : 0;

    if(s_mode_btn.RisingEdge() && g_btn_lockout == 0)
    {
        g_sub_mode = (g_sub_mode + 1) % kNumSubModes;
        g_tk_caught[0] = g_tk_caught[1] = g_tk_caught[2] = false;
        g_tk_init      = true;
        g_btn_lockout  = kBtnLockoutSamples;
    }

    // B8 toggle switch selects internal synth vs external triggers.
    // Flipping the toggle INTO internal mode (external -> internal) rolls a
    // fresh kit. In external mode the internal voices are silent, so this is
    // a natural "give me a new kit" gesture without overloading B7.
    const bool ext_now = s_output_sw.Pressed();
    if(g_external_output && !ext_now)  // external -> internal transition
    {
        RandomizeContext(0);  // reroll all three drums
    }
    g_external_output = ext_now;

    // Update LED - pulse count indicates sub-mode.
    // Default/drum mode (sub_mode 0) shows no flash; edit modes flash 1-3 times.
    // A randomize gesture briefly lights the LED solid as confirmation.
    g_led_ctr += size;
    float t = static_cast<float>(g_led_ctr) / patch.AudioSampleRate();
    bool  led_on;
    if(g_rand_flash > 0)
    {
        led_on       = true;
        g_rand_flash = (g_rand_flash > size) ? g_rand_flash - size : 0;
    }
    else
    {
        led_on = LedPulseState(static_cast<uint8_t>(g_sub_mode), t);
    }
    SetPanelLed(led_on);

    // Read knobs - CV_1..CV_4 pots return approximately 0..1 when nothing is patched
    // (The bipolar -1..+1 range only applies when external CV is connected)
    float k1 = Clamp01f(patch.GetAdcValue(CV_1));
    float k2 = Clamp01f(patch.GetAdcValue(CV_2));
    float k3 = Clamp01f(patch.GetAdcValue(CV_3));
    float k4 = Clamp01f(patch.GetAdcValue(CV_4));

    // Read CV inputs CV_5..CV_8 as bipolar modulation sources (-1..+1)
    // These are CV-only jacks (no pots) for external modulation in Pattern mode
    const float cv5_mod = Clamp11f(patch.GetAdcValue(CV_5)) * 0.5f;  // X mod (±50%)
    const float cv6_mod = Clamp11f(patch.GetAdcValue(CV_6)) * 0.5f;  // Y mod (±50%)
    const float cv7_mod = Clamp11f(patch.GetAdcValue(CV_7)) * 0.5f;  // Density mod (±50%)
    const float cv8_mod = Clamp11f(patch.GetAdcValue(CV_8)) * 0.5f;  // Chaos mod (±50%)

    const float sr = patch.AudioSampleRate();

    // Update global sample counter
    g_sample_counter += size;

    // Reset pattern on rising edge of reset input
    // Reset only resets the pattern position and clock multiplier phase;
    // it does NOT clear external clock mode, so clock source is preserved
    // across resets (common eurorack use case: periodic resets with ext clock).
    // After reset, we suppress interpolated sub-ticks until the next real
    // clock edge arrives. This matches original Grids: reset positions the
    // step pointer and the clock drives playback. In typical eurorack setups
    // the clock edge follows reset within a few ms, so this is imperceptible.
    if(ext_rst_rising)
    {
        grids.Reset();
        g_ext_clk_mult_phase = 0;
        g_ext_clk_counter = 0;
        g_internal_clk_samples = 0;  // Align internal clock phase on reset too
        g_awaiting_first_clock = true;

        // Clear last-edge timestamp so the first clock after reset does NOT
        // recompute g_ext_clk_period from the stale pre-reset timestamp.
        // The old period value (from the previous two clock edges at the
        // correct tempo) is preserved and used for sub-tick interpolation,
        // keeping the first quarter note properly subdivided.
        g_ext_clk_last_edge = 0;
    }

    // Track external clock presence and period
    // Once external clock is detected, it stays latched permanently.
    // Tick is generated immediately on every clock edge (including the first).
    if(ext_clk_rising)
    {
        // Calculate period since last clock edge (unsigned subtraction
        // handles g_sample_counter wrap correctly).
        uint32_t now = g_sample_counter;
        if(g_ext_clk_last_edge > 0)
        {
            g_ext_clk_period = now - g_ext_clk_last_edge;
        }
        g_ext_clk_last_edge = now;
        g_ext_clk_counter = 0;
        g_ext_clk_mult_phase = 0;

        g_use_ext_clock = true;
        g_awaiting_first_clock = false;  // Clock arrived, resume normal operation
    }

    // Generate multiplied clock ticks (8× for Grids steps from quarter notes)
    bool ext_tick = false;
    if(g_use_ext_clock)
    {
        // Always generate a tick on the clock edge itself (sub-tick 0 of 8).
        // This works even on the very first edge before period is known.
        if(ext_clk_rising)
        {
            ext_tick = true;
        }
        else if(g_ext_clk_period > 0 && !g_awaiting_first_clock)
        {
            // Generate interpolated sub-ticks at 1/8, 2/8, ..., 7/8 of the period.
            // Suppressed after reset until the next real clock edge arrives,
            // so stale-period sub-ticks don't consume steps before the clock.
            g_ext_clk_counter += size;
            uint32_t tick_interval = g_ext_clk_period / kClockMultiplier;
            if(tick_interval > 0 && g_ext_clk_mult_phase < (kClockMultiplier - 1))
            {
                uint32_t next_tick_at = (g_ext_clk_mult_phase + 1) * tick_interval;
                if(g_ext_clk_counter >= next_tick_at)
                {
                    ext_tick = true;
                    g_ext_clk_mult_phase++;
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // Edit modes: All 4 CVs control different params per sub-mode
    // Mode 0: Pattern (Grids X/Y/Density/Randomness)
    // Mode 1-3: Drum edit (CV1=Freq, CV2=param, CV3=Pan, CV4=Volume)
    // -----------------------------------------------------------------
    // On entering an edit mode (or after a randomize) seed the takeover
    // baseline so crossing detection has a valid previous knob value.
    if(g_tk_init && g_sub_mode != 0)
    {
        g_tk_prev[0] = k1;
        g_tk_prev[1] = k2;
        g_tk_prev[2] = k3;
        g_tk_init    = false;
    }

    switch(g_sub_mode)
    {
        case 1: // Edit Kick - CV1=Freq, CV2=Decay, CV3=Pan (soft-takeover)
            SoftTakeover(g_kick_p1,  k1, g_tk_caught[0], g_tk_prev[0]);
            SoftTakeover(g_kick_p2,  k2, g_tk_caught[1], g_tk_prev[1]);
            SoftTakeover(g_kick_pan, k3, g_tk_caught[2], g_tk_prev[2]);
            ApplyKick();
            g_kick_gl = sqrtf(1.0f - g_kick_pan);
            g_kick_gr = sqrtf(g_kick_pan);
            break;
        case 2: // Edit Snare - CV1=Freq, CV2=Snappy, CV3=Pan (soft-takeover)
            SoftTakeover(g_snare_p1,  k1, g_tk_caught[0], g_tk_prev[0]);
            SoftTakeover(g_snare_p2,  k2, g_tk_caught[1], g_tk_prev[1]);
            SoftTakeover(g_snare_pan, k3, g_tk_caught[2], g_tk_prev[2]);
            ApplySnare();
            g_snare_gl = sqrtf(1.0f - g_snare_pan);
            g_snare_gr = sqrtf(g_snare_pan);
            break;
        case 3: // Edit HiHat - CV1=Freq, CV2=Decay, CV3=Pan (soft-takeover)
            SoftTakeover(g_hat_p1,  k1, g_tk_caught[0], g_tk_prev[0]);
            SoftTakeover(g_hat_p2,  k2, g_tk_caught[1], g_tk_prev[1]);
            SoftTakeover(g_hat_pan, k3, g_tk_caught[2], g_tk_prev[2]);
            ApplyHat();
            g_hat_gl = sqrtf(1.0f - g_hat_pan);
            g_hat_gr = sqrtf(g_hat_pan);
            break;
        default: // Mode 0: Pattern mode - all 4 knobs for Grids
            break;
    }

    // -----------------------------------------------------------------
    // Trigger logic depends on mode:
    // Pattern mode (0): Use Grids pattern with external or internal clock
    // Edit modes (1-3): Single drum at 1 beat per second
    // -----------------------------------------------------------------
    if(g_sub_mode == 0)
    {
        // Pattern mode: use external clock (4× multiplied) if present, else internal 120 BPM
        bool tick = g_use_ext_clock ? ext_tick : InternalGridsClockTick(sr, size);

        if(tick)
        {
            // Grids parameters from pots + CV modulation
            // CV_5-CV_8 add ±50% modulation to the pot values
            const float x_val    = Clamp01f(k1 + cv5_mod);
            const float y_val    = Clamp01f(k2 + cv6_mod);
            const float dens_val = Clamp01f(k3 + cv7_mod);
            const float rnd_val  = Clamp01f(k4 + cv8_mod);

            const uint8_t x    = static_cast<uint8_t>(x_val * 255.0f);
            const uint8_t y    = static_cast<uint8_t>(y_val * 255.0f);
            const uint8_t dens = static_cast<uint8_t>(dens_val * 255.0f);
            const uint8_t rnd  = static_cast<uint8_t>(rnd_val * 255.0f);

            // Get triggers from Grids pattern generator
            const auto step = grids.Tick(x, y, dens, dens, dens, rnd);

            if(!g_external_output)
            {
                // Internal: synthetic drums via audio out
                if(step.bd)
                    TrigWithAccent(kick, step.bd_accent, kKickAccentNormal, kKickAccentStrong);
                if(step.sd)
                    TrigWithAccent(snare, step.sd_accent, kSnareAccentNormal, kSnareAccentStrong);
                if(step.hh)
                    TrigWithAccent(hat, step.hh_accent, kHatAccentNormal, kHatAccentStrong);
            }
            else
            {
                // External: trigger outputs B5=Kick, B6=Snare, CV_OUT_1=HiHat
                if(step.bd)
                    g_trig_kick_remaining = kTriggerSamples;
                if(step.sd)
                    g_trig_snare_remaining = kTriggerSamples;
                if(step.hh)
                    g_trig_hat_remaining = kTriggerSamples;
            }
        }
    }
    else
    {
        // Edit mode: trigger only the selected drum at 1 beat per second
        bool edit_tick = EditModeClockTick(sr, size);

        if(edit_tick)
        {
            switch(g_sub_mode)
            {
                case 1: // Edit Kick - trigger kick only
                    kick.SetAccent(0.8f);
                    kick.Trig();
                    break;
                case 2: // Edit Snare - trigger snare only
                    snare.SetAccent(0.8f);
                    snare.Trig();
                    break;
                case 3: // Edit HiHat - trigger hat only
                    hat.SetAccent(0.8f);
                    hat.Trig();
                    break;
            }
        }
    }

    // Update external trigger outputs (decay the triggers over time)
    if(g_external_output)
    {
        // Set gate outputs high if trigger is active
        g_gate_kick.Write(g_trig_kick_remaining > 0);
        g_gate_snare.Write(g_trig_snare_remaining > 0);
        patch.WriteCvOut(CV_OUT_1, g_trig_hat_remaining > 0 ? kTriggerVolts : 0.0f);

        // Decrement trigger counters
        if(g_trig_kick_remaining > 0)
            g_trig_kick_remaining = (g_trig_kick_remaining > size) ? g_trig_kick_remaining - size : 0;
        if(g_trig_snare_remaining > 0)
            g_trig_snare_remaining = (g_trig_snare_remaining > size) ? g_trig_snare_remaining - size : 0;
        if(g_trig_hat_remaining > 0)
            g_trig_hat_remaining = (g_trig_hat_remaining > size) ? g_trig_hat_remaining - size : 0;
    }
    else
    {
        // Ensure trigger outputs are off when using internal synth
        g_gate_kick.Write(false);
        g_gate_snare.Write(false);
        patch.WriteCvOut(CV_OUT_1, 0.0f);
    }

    // Render audio (internal synth when not using external triggers)
    if(!g_external_output)
    {
        for(size_t i = 0; i < size; i++)
        {
            // Process each drum voice
            const float kick_out  = kick.Process(false);
            const float snare_out = snare.Process(false);
            const float hat_out   = hat.Process(false);

            // Equal-power pan (gains precomputed at control rate)
            const float kick_l  = kick_out  * g_kick_gl;
            const float kick_r  = kick_out  * g_kick_gr;
            const float snare_l = snare_out * g_snare_gl;
            const float snare_r = snare_out * g_snare_gr;
            const float hat_l   = hat_out   * g_hat_gl;
            const float hat_r   = hat_out   * g_hat_gr;

            // Mix, drive, and saturate (drive lifts level and adds punch)
            float mix_l = 0.95f * kick_l + 0.70f * snare_l + 1.35f * hat_l;
            float mix_r = 0.95f * kick_r + 0.70f * snare_r + 1.35f * hat_r;
            out[0][i] = FastSaturate(mix_l * kOutputDrive);
            out[1][i] = FastSaturate(mix_r * kOutputDrive);
        }
    }
    else
    {
        // External mode: Silence on audio outputs (external modules make sound)
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = 0.0f;
            out[1][i] = 0.0f;
        }
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(void)
{
    patch.Init();

    // Quick LED self-test
    for(int i = 0; i < 6; i++)
    {
        patch.SetLed((i & 1) == 0);
        System::Delay(80);
    }
    patch.SetLed(false);

    const float sr = patch.AudioSampleRate();

    // Initialize Grids pattern generator
    grids.Init(static_cast<uint16_t>(System::GetNow() & 0xFFFFu));

    // Seed the kit-randomization RNG (avoid a zero state).
    g_rng = System::GetNow() | 1u;

    // Initialize synthetic drum voices with tamed defaults
    kick.Init(sr);
    kick.SetFreq(55.0f);
    kick.SetDecay(0.22f);
    kick.SetTone(0.25f);
    kick.SetDirtiness(0.03f);
    kick.SetFmEnvelopeAmount(0.10f);
    kick.SetFmEnvelopeDecay(0.10f);

    snare.Init(sr);
    snare.SetFreq(185.0f);
    snare.SetDecay(0.06f);
    snare.SetFmAmount(0.00f);
    snare.SetSnappy(0.75f);

    hat.Init(sr);
    hat.SetFreq(8000.0f);
    hat.SetDecay(0.55f);
    hat.SetTone(0.70f);
    hat.SetNoisiness(0.95f);

    // Boot with a fresh randomized kit (overrides the tuned defaults above).
    RandomizeKick();
    RandomizeSnare();
    RandomizeHat();

    // Initialize UI switches
    s_mode_btn.Init(patch.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);  // Cycles sub-modes
    s_output_sw.Init(patch.B8, sr, Switch::TYPE_TOGGLE, Switch::POLARITY_NORMAL);      // Internal/External

    // Initialize external trigger gate outputs (B5, B6)
    g_gate_kick.Init(patch.B5, GPIO::Mode::OUTPUT);
    g_gate_snare.Init(patch.B6, GPIO::Mode::OUTPUT);

    // Start audio
    patch.StartAudio(AudioCallback);

    // Main loop - just keep running (all work done in callback)
    while(1)
    {
        System::Delay(10);
    }
}
