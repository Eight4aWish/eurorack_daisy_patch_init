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

#include "drum_voices.h"
#include "grids_port.h"
#include "speech_data.h"
#include "user_bank.h"

#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// =============================================================================
// Grids Drum Sequencer for Daisy Patch.Init
// 
// B8 Toggle Switch: rolls a fresh randomized kit on the return flip (the
//   toggle's position means nothing; only the edge does, so the gesture is
//   flip-out-and-back). The internal kit and the trigger outputs both run all
//   the time - B5=Kick, B6=Snare, CV_OUT_1=HiHat play the same pattern as the
//   audio outs, so an external module can be driven alongside the internal
//   voices rather than instead of them.
//
// B10 (Gate In 1): Clock input - resolution detected automatically
// B9  (Gate In 2): Reset input - rising edge resets pattern to step 0
//
// B7 Momentary Button: short press cycles pages, hold cycles drum-map bank
//     (the module announces which bank: Original / Club / Traditional / Latin)
//     Home (no flash): CV1=X, CV2=Y, CV3=Density, CV4=Randomness
//     Kit  (1 pulse):  CV1/2/3 = kick/snare/hat density trim, CV4 = Wildness
//   The sequencer keeps running on every page - the Kit page borrows the knobs,
//   not the transport. Home knobs stay live; Kit knobs use soft-takeover, so a
//   knob only grabs its parameter once it crosses the stored value and nothing
//   jumps when you arrive.
//
//   On the Home page the LED marks the bar: long flash on step 0, short on
//   step 16. That is how the detected clock ratio gets checked without a screen.
//
// A fresh kit is also rolled at power-up. The LED blips to confirm each roll.
//
// Kit and pattern are orthogonal: rolling never touches x/y, density,
// randomness, the step counter or the Grids LFSR, so a groove survives any
// number of rolls and you can hear the same pattern through a different kit.
// =============================================================================

namespace
{

DaisyPatchSM patch;

// -----------------------------------------------------------------------------
// Diagnostics (set each to 0 for normal operation)
// -----------------------------------------------------------------------------
// Switch diagnostic. Set to 1 to bypass all page/roll logic and drive the LED
// straight from the raw debounced switch state:
//   B7 held      -> LED solid, full brightness
//   B8 "pressed" -> LED solid, dim
// That isolates whether the switches are being read at all from whether the
// logic acting on them is right. Set back to 0 for normal operation.
#define SORROW_SWITCH_DIAG 0

// Blink out the rolled kit at boot, before audio starts. Lets a kit that hangs
// the firmware be identified on a module with no screen.
#define SORROW_REPORT_KIT 0

// Log measured model costs over USB serial at boot. Far better than blink
// codes: connect the module by USB and read /dev/tty.usbmodem*. Non-blocking,
// so it costs nothing when nothing is listening.
#define SORROW_LOG_USB 1

// Report audio CPU load on the LED. This is the one diagnostic that still works
// when the switches are dead: Switch::Debounce() only advances when
// System::GetNow() does, so if the audio callback starves SysTick the switches
// freeze permanently while audio and the LED - both driven by sample counts
// inside the callback - carry on as if nothing were wrong. That is exactly the
// reported symptom, and it is kit-dependent because some models cost more.
#define SORROW_REPORT_CPU 1
static constexpr float kCpuAlarmLoad = 0.85f;

daisy_grids::grids_port::GridsDrumGenerator grids;

// -----------------------------------------------------------------------------
// Drum voices. The three slots are played by whichever models the last roll
// selected - see drum_voices.h. Nothing here knows or cares which they are.
// -----------------------------------------------------------------------------
using sorrow::Slot;

// Accent levels for Grids triggers
static constexpr float kKickAccentNormal  = 0.55f;
static constexpr float kKickAccentStrong  = 1.00f;
static constexpr float kSnareAccentNormal = 0.45f;
static constexpr float kSnareAccentStrong = 1.00f;
static constexpr float kHatAccentNormal   = 0.55f;
static constexpr float kHatAccentStrong   = 1.00f;

static inline void TrigWithAccent(Slot  slot,
                                  bool  accent,
                                  float normal_level,
                                  float strong_level)
{
    sorrow::VoiceTrig(slot, accent ? strong_level : normal_level);
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
static Switch   s_output_sw;      // B8 - toggle, rolls a new kit on the return flip
static uint8_t  g_sub_mode = 0;   // 0=Home (pattern), 1=Kit (densities + wildness)
static constexpr uint8_t kNumSubModes = 2;
static bool     g_b8_prev = false;  // B8's position last block, for reroll edge detection
static uint32_t g_led_ctr     = 0;

#if SORROW_REPORT_CPU
static CpuLoadMeter g_cpu_meter;
#endif

// Equal-power pan gains, recomputed from the pool whenever the kit is rolled:
// L = sqrt(1-pan), R = sqrt(pan).
static float g_kick_gl  = 0.707f, g_kick_gr  = 0.707f;
static float g_snare_gl = 0.707f, g_snare_gr = 0.707f;
static float g_hat_gl   = 0.707f, g_hat_gr   = 0.707f;

// -----------------------------------------------------------------------------
// Kit page parameters.
//
// Per-part density is an additive trim around the Home page's master density:
//     density_part = clamp(master + (trim - 0.5))
// Centred trims reproduce the single-density behaviour exactly, so the master
// still sweeps the whole kit as one gesture while the trims set the balance.
//
// Wildness scales how far a kit roll may go - which models are eligible, how
// far their parameters roam, and how willing the three slots are to come from
// different families.
// -----------------------------------------------------------------------------
// Home page parameters. Live while Home is showing, held while a settings page
// borrows the knobs. Home stays live rather than soft-takeover so performance
// feel is unaffected, matching the house convention in docs/PANEL.md.
static float g_home_x    = 0.5f;
static float g_home_y    = 0.5f;
static float g_home_dens = 0.5f;
static float g_home_rnd  = 0.0f;

static float g_dens_kick  = 0.5f;
static float g_dens_snare = 0.5f;
static float g_dens_hat   = 0.5f;
static float g_wildness   = 0.35f;

// Soft-takeover state: a knob only grabs its parameter once it crosses the
// stored value, so entering the Kit page doesn't make params jump to the knob's
// current physical position. Indices: 0=k1, 1=k2, 2=k3, 3=k4.
static bool  g_tk_caught[4] = {false, false, false, false};
static float g_tk_prev[4]   = {0.0f, 0.0f, 0.0f, 0.0f};
static bool  g_tk_init      = true;   // reset prev[] on next Kit-page callback
static constexpr float kCatchEps = 0.03f;

// Brief LED confirmation flash after a randomize gesture.
static uint32_t g_rand_flash = 0;
static constexpr float    kFlashSeconds = 0.17f;
static uint32_t           kFlashSamples = 8000;  // set from sample rate at boot

// Bar indicator. With no screen this is the only way to see whether the
// detected clock ratio is right: if the LED pulses in time with the bar you
// are hearing, the division is correct. Long on the phrase start (step 0),
// short on the second bar (step 16), so "ONE two" is distinguishable.
static uint32_t g_bar_flash = 0;
static constexpr float kBarFlashLongSeconds  = 0.08f;
static constexpr float kBarFlashShortSeconds = 0.03f;
static uint32_t        kBarFlashLong         = 3800;  // set from sample rate
static uint32_t        kBarFlashShort        = 1400;  // set from sample rate
static constexpr uint8_t  kStepsPerBar   = 16;    // 32-step pattern = 2 bars of 4/4

// B7 gestures. Short press (on release) cycles the page; press and hold cycles
// the drum-map bank. Page change fires on release rather than on the edge so a
// long press does not also change the page on its way past.
static uint32_t g_btn_held      = 0;      // samples held, 0 when open
static bool     g_btn_long_done = false;  // long action already fired this hold
static constexpr float kLongPressSeconds = 0.80f;
static constexpr float kMinPressSeconds  = 0.02f;
static uint32_t kLongPressSamples = 38400;
static uint32_t kMinPressSamples  = 960;

// Spoken announcements. The LED is already busy marking the bar, and a second
// visual signal competing with it is genuinely hard to read - that is what
// happened to the button and to the bank blink code. Speech puts state changes
// on an uncontested channel, and lets the LED go back to doing one job.
static const uint8_t* g_speech     = nullptr;
static size_t         g_speech_len = 0;
static float          g_speech_pos = 0.0f;

static inline void Announce(const uint8_t* pcm, size_t len)
{
    g_speech     = pcm;
    g_speech_len = len;
    g_speech_pos = 0.0f;
}

// Level of the announcement, and how far the kit ducks under it.
static constexpr float kSpeechLevel = 0.70f;
static constexpr float kSpeechDuck  = 0.35f;

// B7 press lockout: after a mode change, ignore further press edges for a
// short window so contact bounce / electrical noise can't advance the mode
// more than once per physical press. ~200ms is well above any bounce but
// still allows brisk deliberate presses.
static uint32_t g_btn_lockout = 0;
static constexpr float    kBtnLockoutSeconds = 0.20f;
static uint32_t           kBtnLockoutSamples = 9600;  // set from sample rate

// Output drive into the soft saturator. The saturator (x/(1+|x|)) otherwise
// pulls a single hit down ~7dB; driving it harder restores level and adds
// punch while still limiting peaks below full scale.
static constexpr float kOutputDrive = 3.5f;

// Per-slot mix balance, applied after each model's boot-measured level trim.
//
// These were 0.95 / 0.70 / 1.35, tuned by ear for v1's three fixed voices where
// the hi-hat was naturally quiet and needed lifting. Boot-time normalisation now
// brings every model to a common peak already, so that 1.35 became a second
// correction stacked on the first: the hat arrived at the saturator around 3.3
// where the snare arrived at 1.7, making it both the loudest voice and the most
// compressed. Roughly twice as much drive as anything else, for no reason
// beyond history.
//
// The trim upstream equalises *peak*, which is the wrong measure for balancing
// percussion: a hat is over in milliseconds, so at equal peak it carries far
// less energy and sounds much quieter. Measured across 300 rolls, rms/peak is
// 0.168 for the kick, 0.106 for the snare and 0.048 for the hat - so simply
// matching the kick's energy needs 1.00 / 1.36 / 3.41.
//
// These set a musical balance on top of that: snare about 3 dB under the kick,
// hat about 8 dB under. Flattening them toward 1.0 buries the hat, which is the
// mistake that produced this comment.
//
// Worth noting the hat figure lands within a whisker of the 0.95/0.70/1.35 that
// was tuned purely by ear for v1 - the ear and the arithmetic agree here.
static constexpr float kMixKick  = 1.00f;
static constexpr float kMixSnare = 0.95f;
static constexpr float kMixHat   = 1.30f;

// Lightweight xorshift RNG for kit randomization.
static uint32_t g_rng = 0x1234567u;
static inline uint32_t RngNext()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static inline float RandF01()
{
    return static_cast<float>(RngNext() & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

// Panel LED drive. CV_OUT_2 feeds the LED through a series resistor, so this
// sets brightness as well as on/off, and the relationship is steep: subtract
// the LED's forward drop (~2 V for red) and 3.0 V passes barely a third of the
// current 5.0 V does, with the rest of the range disappearing fast below that.
//
// Two levels rather than one. Confirmations - the boot signature and a kit roll
// - drive full scale so they are never in doubt, because those are exactly the
// events you look at the LED to check. The repeating indicators run dimmer,
// since they are on most of the time and were what felt too bright.
static constexpr float kLedVoltsFull = 5.0f;
static constexpr float kLedVoltsDim  = 3.4f;

// -----------------------------------------------------------------------------
// External Clock & Reset (B10, B9)
// Manual edge detection with state tracking for reliability
// Clock: the incoming rate is detected (see the ratio table below) and turned
// into steps at 4 steps per quarter note - one step per 16th, matching the
// internal clock's 8 ticks/sec at 120 BPM. So a 32-step pattern spans TWO bars.
//
// This is a deliberate departure from real Grids, which runs kPulsesPerStep = 3
// at 24 ppqn - 8 steps per quarter, a 32nd-note grid, one bar per pattern.
//
// The reason is swing. Swing is a 16th-note feel, so the module has to take its
// steps from a 16th-note clock for something like Pam's to be able to shuffle
// them. At Grids' 32nd-note step rate the swing would land between the notes it
// is meant to displace and do nothing useful.
//
// Two consequences worth knowing:
//   - Emilie's tables still read correctly here, because she writes almost
//     entirely on the even (16th) slots - 858 of her 907 non-zero values.
//   - Banks derived FOR this module are 32 sixteenths, i.e. two bars, and have
//     to be re-derived at one bar before going onto real Grids hardware, or they
//     play at double time. See tools/grids_firmware/.
// External clock is used while it keeps arriving; if it stops for ~4 beats
// (2 s floor) the module falls back to its internal clock. Reset only moves the
// pattern position and does not change the clock source.
// -----------------------------------------------------------------------------
static bool g_ext_clk_state = false;  // Current clock state
static bool g_ext_rst_state = false;  // Current reset state
static bool g_use_ext_clock = false;  // True if external clock mode (latches on first edge)
static bool g_awaiting_first_clock = false; // After reset, suppress sub-ticks until next clock edge

// Sample count at the last clock edge, used only for the drop-out timeout.
// Deliberately separate from g_ext_clk_last_edge, which reset() clears to
// suppress a stale period recompute; this one must survive a reset so a reset
// is never mistaken for the clock going away.
static uint32_t g_ext_clk_last_seen = 0;

// -----------------------------------------------------------------------------
// Clock in: one pulse, one step, one 16th note. Fixed.
//
// Send Sorrow a 16th-note clock. That is the whole contract. A 32-step pattern
// is two bars, and the internal clock runs at the same rate (8 ticks/sec = 4 per
// quarter at 120 BPM) so patched and unpatched behave identically.
//
// Until v2.2.0 this measured the incoming pulse period and guessed the source's
// resolution - 24, 8, 4, 2 or 1 ppqn - then divided or multiplied to suit. It is
// gone, and the reasons are worth keeping:
//
//   - No way to correct a wrong guess. There is no Time page and no manual
//     ratio, so a misdetection was uncorrectable. The bar LED showed the player
//     it was wrong without letting them do anything about it.
//   - Tempo-dependent. The period bands only separate cleanly between 88 and
//     176 BPM. Outside that a sub-quarter clock misread by a factor of two, so
//     the module behaved differently at 80 BPM than at 120.
//   - Multiplying drifted. Intermediate steps were predicted from the last
//     measured period rather than driven by edges, so any tempo change smeared
//     them. Dividing and 1:1 are edge-driven and cannot.
//   - Swing worked by accident. A swung clock alternates long-short, so half its
//     pulses measured into the wrong band; it only survived because a new band
//     needed three consecutive sightings to be accepted, which alternating
//     readings never gave it. True, but nothing knew it was load-bearing.
//
// Every clock source can divide - Pam's, Move, Ornament and Crime. Asking for a
// 16th clock costs the player one setting, once, and buys behaviour that is the
// same at every tempo and immune to swing, because nothing interprets the clock
// at all any more.
// -----------------------------------------------------------------------------

// How long the external clock may go quiet before the internal clock takes
// over. Also floors the adaptive window, which is 4 measured beats.
static constexpr float kClockDropoutSeconds = 5.0f;

// Hard ceiling on that window, and on the measured period that scales it.
//
// The period is simply the gap between two consecutive rising edges, so any
// stray pair - patching a cable, a clock started and then stopped, a noise
// glitch - can measure an arbitrarily long "period". Unbounded, a 60 s gap set
// a four-minute drop-out window: the sequencer stopped, nothing triggered, and
// the module sat silent while still burning full CPU processing untriggered
// voices, before apparently curing itself. Nothing above the slowest detection
// band (1500 ms) is a musical clock, so the period is clamped there and the
// window is capped outright.
static constexpr float kMaxClockPeriodSeconds  = 2.0f;
static constexpr float kMaxClockDropoutSeconds = 8.0f;

// The period is still measured, but only to size the drop-out window - never to
// decide a ratio.
static uint32_t g_ext_clk_period = 0;     // Samples between last two clock edges
static uint32_t g_ext_clk_last_edge = 0;  // Sample count at last clock edge
static uint32_t g_sample_counter = 0;     // Global sample counter

// -----------------------------------------------------------------------------
// External Trigger Outputs (Mode 1)
// B5 = Kick, B6 = Snare, CV_OUT_1 = HiHat
// -----------------------------------------------------------------------------
static GPIO g_gate_kick;   // B5
static GPIO g_gate_snare;  // B6
static constexpr float kTriggerVolts = 5.0f;

// Trigger duration in samples (approx 10ms at 48kHz)
static constexpr float kTriggerSeconds = 0.010f;  // 10 ms, a safe eurorack trigger
static uint32_t        kTriggerSamples = 480;      // set from sample rate at boot
static uint32_t g_trig_kick_remaining  = 0;
static uint32_t g_trig_snare_remaining = 0;
static uint32_t g_trig_hat_remaining   = 0;



static inline void SetPanelLed(bool on, float volts)
{
    // Panel LED on Patch.Init is driven via CV_OUT_2
    patch.WriteCvOut(CV_OUT_2, on ? volts : 0.0f);
}

// Length of one LED cycle, in seconds. The phase counter is wrapped to this so
// it stays small enough for float32 to resolve.
static constexpr float kLedCycleSeconds = 2.0f;


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
// Roll a fresh kit: new models in the three slots, new parameters for each, at
// whatever reach wildness currently allows. Pan gains are recomputed here so
// the audio loop never has to.
//
// Deliberately touches nothing the sequencer owns - not x/y, not density, not
// the step counter, not the Grids LFSR. A groove you like survives any number
// of rolls, which is the whole point of separating kit from pattern.
// -----------------------------------------------------------------------------
static inline void RollKit()
{
    sorrow::VoicesRoll(g_wildness, RandF01);

    const float kp = sorrow::VoicePan(Slot::Kick);
    const float sp = sorrow::VoicePan(Slot::Snare);
    const float hp = sorrow::VoicePan(Slot::Hat);
    g_kick_gl  = sqrtf(1.0f - kp);
    g_kick_gr  = sqrtf(kp);
    g_snare_gl = sqrtf(1.0f - sp);
    g_snare_gr = sqrtf(sp);
    g_hat_gl   = sqrtf(1.0f - hp);
    g_hat_gr   = sqrtf(hp);

    g_rand_flash = kFlashSamples;
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
// Audio Callback
// Renders the internal kit and drives the trigger outputs, every block.
// -----------------------------------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    (void)in;

    // -----------------------------------------------------------------
#if SORROW_REPORT_CPU
    g_cpu_meter.OnBlockStart();
#endif

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

    // Advance the kit RNG every block, whether or not anything reads it.
    // System::GetNow() is only ~480 ms at seed time on every boot (the LED
    // self-test dominates), so the seed alone is near-constant and the kit
    // sequence used to repeat identically after each power-up. Free-running
    // here means the state when B8 is flipped depends on how long the user
    // waited, which at ~1 ms per block is real entropy.
    RngNext();

    // B7 momentary button: press cycles sub-modes (pattern + 3 edit modes).
    // A lockout after each accepted press rejects contact bounce/noise so one
    // physical press advances the mode exactly once.
    if(g_btn_lockout > 0)
        g_btn_lockout = (g_btn_lockout > size) ? g_btn_lockout - size : 0;

    if(s_mode_btn.Pressed())
    {
        g_btn_held += size;
        if(!g_btn_long_done && g_btn_held >= kLongPressSamples)
        {
            // Hold: next drum-map bank.
            const uint8_t next
                = (daisy_grids::grids_port::GetBank() + 1)
                  % daisy_grids::grids_port::BankCount();
            daisy_grids::grids_port::SetBank(next);
            switch(next)
            {
                case 0:
                    Announce(sorrow::kSpeechBankOriginal,
                             sorrow::kSpeechBankOriginalLen);
                    break;
                case 1:
                    Announce(sorrow::kSpeechBankClub, sorrow::kSpeechBankClubLen);
                    break;
                case 2:
                    Announce(sorrow::kSpeechBankTrad, sorrow::kSpeechBankTradLen);
                    break;
                case 3:
                    Announce(sorrow::kSpeechBankLatin, sorrow::kSpeechBankLatinLen);
                    break;
                default:
                    Announce(sorrow::kSpeechBankUser, sorrow::kSpeechBankUserLen);
                    break;
            }
            g_btn_long_done = true;
        }
    }
    else
    {
        // Released. A short press cycles the page; a long one already acted.
        if(g_btn_held >= kMinPressSamples && !g_btn_long_done
           && g_btn_lockout == 0)
        {
            g_sub_mode = (g_sub_mode + 1) % kNumSubModes;
            g_tk_caught[0] = g_tk_caught[1] = g_tk_caught[2] = g_tk_caught[3]
                = false;
            g_tk_init     = true;
            g_btn_lockout = kBtnLockoutSamples;
        }
        g_btn_held      = 0;
        g_btn_long_done = false;
    }

    // B8 toggle rolls a fresh kit on the return flip. The toggle no longer
    // selects an output mode - audio and trigger outputs both run all the time -
    // so its position carries no meaning; only the edge does. Deliberately kept
    // one-directional: flip out and back is the gesture, and the double flip is
    // more satisfying to play than a single throw.
    const bool b8_now = s_output_sw.Pressed();
    if(g_b8_prev && !b8_now)
    {
        RollKit();
    }
    g_b8_prev = b8_now;

    // Update LED - pulse count indicates sub-mode.
    // Default/drum mode (sub_mode 0) shows no flash; edit modes flash 1-3 times.
    // A randomize gesture briefly lights the LED solid as confirmation.
    // Wrap at one LED cycle so t never grows large enough for float32 to
    // quantise the pulse windows (it used to drift after a few hours up).
    g_led_ctr += size;
    const uint32_t led_cycle_samples
        = static_cast<uint32_t>(kLedCycleSeconds * patch.AudioSampleRate());
    if(led_cycle_samples > 0 && g_led_ctr >= led_cycle_samples)
        g_led_ctr -= led_cycle_samples;
    bool  led_on;
    float led_volts = kLedVoltsDim;
    if(g_bar_flash > 0)
        g_bar_flash = (g_bar_flash > size) ? g_bar_flash - size : 0;

    if(g_rand_flash > 0)
    {
        // Kit roll confirmation: full brightness, so a flip that did register
        // is never mistaken for one that didn't.
        led_on       = true;
        led_volts    = kLedVoltsFull;
        g_rand_flash = (g_rand_flash > size) ? g_rand_flash - size : 0;
    }
    else if(g_sub_mode == 0)
    {
        // Home: brief flashes marking the bar.
        led_on = g_bar_flash > 0;
    }
    else
    {
        // Settings pages: steady, not flashing. A pulse count was unreadable
        // against the bar marker - one intermittent flash looks like any other -
        // so the pages differ in character instead. Home blinks, Kit is solid.
        led_on = true;
    }
#if SORROW_SWITCH_DIAG
    // Raw switch state wins over everything, so a dead switch and dead logic
    // can be told apart.
    if(s_mode_btn.Pressed())
    {
        led_on    = true;
        led_volts = kLedVoltsFull;
    }
    else if(s_output_sw.Pressed())
    {
        led_on    = true;
        led_volts = kLedVoltsDim;
    }
    else
    {
        led_on = false;
    }
#endif
    SetPanelLed(led_on, led_volts);

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
        g_internal_clk_samples = 0;  // Align internal clock phase on reset too
        g_awaiting_first_clock = true;

        // Clear last-edge timestamp so the first clock after reset does NOT
        // recompute g_ext_clk_period from the stale pre-reset timestamp - it
        // sizes the drop-out window, and a bogus period would mis-size it.
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
            const uint32_t measured = now - g_ext_clk_last_edge;
            const uint32_t max_period
                = static_cast<uint32_t>(kMaxClockPeriodSeconds * sr);
            // A gap longer than any musical clock is not a period; it is two
            // unrelated edges. Clamp rather than believe it.
            g_ext_clk_period = measured < max_period ? measured : max_period;
        }
        g_ext_clk_last_edge = now;

        g_use_ext_clock = true;
        g_awaiting_first_clock = false;  // Clock arrived, resume normal operation
        g_ext_clk_last_seen = now;
    }

    // Clock drop-out: if the external clock stops, fall back to the internal
    // clock instead of latching until power-cycle. The window scales with the
    // measured period (4 beats) so slow clocks aren't mistaken for a drop-out,
    // with a floor for the case where no period is known yet. The floor is long
    // enough that a deliberate pause reads as a pause rather than a stop.
    if(g_use_ext_clock)
    {
        const uint32_t floor_samples
            = static_cast<uint32_t>(kClockDropoutSeconds * sr);
        const uint32_t ceiling_samples
            = static_cast<uint32_t>(kMaxClockDropoutSeconds * sr);
        uint32_t timeout = static_cast<uint32_t>(
            fmaxf(static_cast<float>(floor_samples),
                  static_cast<float>(g_ext_clk_period) * 4.0f));
        if(timeout > ceiling_samples)
            timeout = ceiling_samples;
        if((g_sample_counter - g_ext_clk_last_seen) > timeout)
        {
            g_use_ext_clock        = false;
            g_ext_clk_period       = 0;
            g_ext_clk_last_edge    = 0;
            g_awaiting_first_clock = false;
            g_internal_clk_samples = 0;
        }
    }

    // One rising edge, one step. Every step is driven by a real edge, so the
    // sequencer cannot drift from the clock and swing passes straight through.
    const bool ext_tick = g_use_ext_clock && ext_clk_rising;

    // -----------------------------------------------------------------
    // Kit page (1 LED pulse): per-part density trims + wildness.
    // Home page (no pulse): all four knobs belong to Grids.
    //
    // On entering the page, seed the takeover baseline so crossing detection
    // has a valid previous knob value and nothing jumps.
    // -----------------------------------------------------------------
    if(g_tk_init && g_sub_mode != 0)
    {
        g_tk_prev[0] = k1;
        g_tk_prev[1] = k2;
        g_tk_prev[2] = k3;
        g_tk_prev[3] = k4;
        g_tk_init    = false;
    }

    if(g_sub_mode == 1)
    {
        SoftTakeover(g_dens_kick,  k1, g_tk_caught[0], g_tk_prev[0]);
        SoftTakeover(g_dens_snare, k2, g_tk_caught[1], g_tk_prev[1]);
        SoftTakeover(g_dens_hat,   k3, g_tk_caught[2], g_tk_prev[2]);
        SoftTakeover(g_wildness,   k4, g_tk_caught[3], g_tk_prev[3]);
    }
    else
    {
        g_home_x    = k1;
        g_home_y    = k2;
        g_home_dens = k3;
        g_home_rnd  = k4;
    }

    // -----------------------------------------------------------------
    // The sequencer runs on every page. The Kit page borrows the knobs, not
    // the transport - you have to hear the densities you are setting.
    // -----------------------------------------------------------------
    {
        const bool tick
            = g_use_ext_clock ? ext_tick : InternalGridsClockTick(sr, size);

        if(tick)
        {
            // Grids parameters from pots + CV modulation
            // CV_5-CV_8 add ±50% modulation to the pot values
            const float x_val    = Clamp01f(g_home_x + cv5_mod);
            const float y_val    = Clamp01f(g_home_y + cv6_mod);
            const float dens_val = Clamp01f(g_home_dens + cv7_mod);
            const float rnd_val  = Clamp01f(g_home_rnd + cv8_mod);

            // Per-part density is an additive trim around the master, so
            // centred trims behave exactly as the single density did, and the
            // master still sweeps all three while preserving their balance.
            const float dk = Clamp01f(dens_val + (g_dens_kick - 0.5f));
            const float ds = Clamp01f(dens_val + (g_dens_snare - 0.5f));
            const float dh = Clamp01f(dens_val + (g_dens_hat - 0.5f));

            const uint8_t x       = static_cast<uint8_t>(x_val * 255.0f);
            const uint8_t y       = static_cast<uint8_t>(y_val * 255.0f);
            const uint8_t rnd     = static_cast<uint8_t>(rnd_val * 255.0f);
            const uint8_t dens_bd = static_cast<uint8_t>(dk * 255.0f);
            const uint8_t dens_sd = static_cast<uint8_t>(ds * 255.0f);
            const uint8_t dens_hh = static_cast<uint8_t>(dh * 255.0f);

            // Get triggers from Grids pattern generator
            // step() is the index about to play; read it before Tick advances.
            const uint8_t step_idx = grids.step();
            if(step_idx == 0)
                g_bar_flash = kBarFlashLong;
            else if(step_idx == kStepsPerBar)
                g_bar_flash = kBarFlashShort;

            const auto step = grids.Tick(x, y, dens_bd, dens_sd, dens_hh, rnd);

            // Internal voices and trigger outputs both fire: the audio out
            // plays the kit while B5/B6/CV_OUT_1 drive external modules from
            // the same pattern.
            if(step.bd)
            {
                TrigWithAccent(Slot::Kick, step.bd_accent, kKickAccentNormal, kKickAccentStrong);
                g_trig_kick_remaining = kTriggerSamples;
            }
            if(step.sd)
            {
                TrigWithAccent(Slot::Snare, step.sd_accent, kSnareAccentNormal, kSnareAccentStrong);
                g_trig_snare_remaining = kTriggerSamples;
            }
            if(step.hh)
            {
                TrigWithAccent(Slot::Hat, step.hh_accent, kHatAccentNormal, kHatAccentStrong);
                g_trig_hat_remaining = kTriggerSamples;
            }
        }
    }

    // Trigger outputs, always live: an unpatched gate out costs nothing, and
    // this way the internal kit and an external module can share one pattern.
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

    // Render audio. The kit always plays, whatever B8's position: the voices
    // are never left unprocessed, so envelopes can't stall and resume mid-tail.
    {
        for(size_t i = 0; i < size; i++)
        {
            // Process each drum voice
            const float kick_out  = sorrow::VoiceProcess(Slot::Kick);
            const float snare_out = sorrow::VoiceProcess(Slot::Snare);
            const float hat_out   = sorrow::VoiceProcess(Slot::Hat);

            // Equal-power pan (gains precomputed at control rate)
            const float kick_l  = kick_out  * g_kick_gl;
            const float kick_r  = kick_out  * g_kick_gr;
            const float snare_l = snare_out * g_snare_gl;
            const float snare_r = snare_out * g_snare_gr;
            const float hat_l   = hat_out   * g_hat_gl;
            const float hat_r   = hat_out   * g_hat_gr;

            // Mix, drive, and saturate (drive lifts level and adds punch)
            float mix_l = kMixKick * kick_l + kMixSnare * snare_l + kMixHat * hat_l;
            float mix_r = kMixKick * kick_r + kMixSnare * snare_r + kMixHat * hat_r;
            float l     = FastSaturate(mix_l * kOutputDrive);
            float r     = FastSaturate(mix_r * kOutputDrive);

            // Spoken announcement over the top, with the kit ducked under it.
            // Linear interpolation because the source is 11 kHz against a
            // 48 kHz output and nearest-neighbour would alias audibly on
            // speech, which is all transients and formants.
            if(g_speech)
            {
                const size_t idx = static_cast<size_t>(g_speech_pos);
                if(idx + 1 >= g_speech_len)
                {
                    g_speech = nullptr;
                }
                else
                {
                    const float frac = g_speech_pos - static_cast<float>(idx);
                    const float a    = (static_cast<float>(g_speech[idx]) - 128.0f)
                                    / 128.0f;
                    const float b    = (static_cast<float>(g_speech[idx + 1]) - 128.0f)
                                    / 128.0f;
                    const float v    = (a + (b - a) * frac) * kSpeechLevel;
                    g_speech_pos += sorrow::kSpeechRateHz / sr;
                    l = l * kSpeechDuck + v;
                    r = r * kSpeechDuck + v;
                }
            }

            out[0][i] = l;
            out[1][i] = r;
        }
    }

#if SORROW_REPORT_CPU
    g_cpu_meter.OnBlockEnd();
#endif
}

} // namespace

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(void)
{
    patch.Init();

    // Flush denormals to zero. libDaisy enables the FPU but never sets FZ, and
    // this firmware is full of exponentially decaying envelopes and ringing
    // resonators - exactly the things that trail off into denormal territory
    // and stay there. Standard practice for audio DSP on Cortex-M.
    __set_FPSCR(__get_FPSCR() | (1u << 24));

    // Back to 48 kHz. 32 kHz was adopted because the DaisySP drum models were
    // unaffordable here - the cheapest possible kit cost 53% of a sample period
    // and the cheapest hi-hat alone 31%, which starved SysTick and froze the
    // panel. Forking the snare and the hi-hat removed that constraint: the
    // floor is now 15% at 32 kHz, so roughly 22% here, and the hat frequency
    // ranges no longer have to duck under a 16 kHz Nyquist.
    //
    // Flip this to SAI_32KHZ to A/B; the hat ranges in drum_voices.cpp are the
    // only thing tied to the choice.
    patch.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

#if SORROW_LOG_USB
    patch.StartLog(false);   // non-blocking: runs fine with nothing attached
#endif

    // Boot signature on the *panel* LED. patch.SetLed() only drives the Daisy's
    // own on-board LED, which is invisible once the module is racked, so the
    // old self-test could never answer "did my new firmware actually start?".
    // Two slow blinks here, clearly distinct from the short bar flashes, is
    // proof the app is alive before anything else has had a chance to go wrong.
    for(int i = 0; i < 4; i++)
    {
        const bool on = (i & 1) == 0;
        patch.SetLed(on);
        SetPanelLed(on, kLedVoltsFull);
        System::Delay(180);
    }
    patch.SetLed(false);
    SetPanelLed(false, kLedVoltsFull);

    const float sr = patch.AudioSampleRate();

    // Seed from analogue noise, not from the clock.
    //
    // System::GetNow() is only ever ~700 ms here - the boot LED signature
    // dominates and is itself fixed - so seeding from it gave a near-constant
    // seed and therefore the same kit from every cold boot. Free-running the
    // RNG in the callback fixes rolls made later, once the user has waited an
    // unpredictable time, but does nothing for the roll that happens at boot.
    //
    // Unpatched CV inputs float, so their ADC readings carry real noise in the
    // low bits, and the knob positions add whatever the user last left them at.
    uint32_t seed = System::GetNow();
    for(int pass = 0; pass < 24; ++pass)
    {
        patch.ProcessAllControls();
        for(int ch = 0; ch < 8; ++ch)
        {
            seed = seed * 1664525u + 1013904223u;
            seed ^= static_cast<uint32_t>(fabsf(patch.GetAdcValue(ch)) * 65535.0f);
        }
        System::Delay(2);
    }

    g_rng = seed | 1u;
    grids.Init(static_cast<uint16_t>(((seed >> 16) ^ seed) & 0xFFFFu));

    // These are all durations, not sample counts. They were written as constants
    // for 48 kHz, so dropping to 32 kHz silently stretched every one of them by
    // half - including the trigger output width, which went from 10 ms to 15 ms.
    // Derive them from the running sample rate instead.
    kFlashSamples      = static_cast<uint32_t>(kFlashSeconds * sr);
    kBarFlashLong      = static_cast<uint32_t>(kBarFlashLongSeconds * sr);
    kBarFlashShort     = static_cast<uint32_t>(kBarFlashShortSeconds * sr);
    kBtnLockoutSamples = static_cast<uint32_t>(kBtnLockoutSeconds * sr);
    kTriggerSamples    = static_cast<uint32_t>(kTriggerSeconds * sr);
    kLongPressSamples  = static_cast<uint32_t>(kLongPressSeconds * sr);
    kMinPressSamples   = static_cast<uint32_t>(kMinPressSeconds * sr);


    // Reading a user bank off the card is parked - see include/user_bank.h for
    // what is wrong and how far the diagnosis got. Enable with
    // SORROW_SD_USER_BANK=1 when picking it back up; three banks otherwise.
#if SORROW_SD_USER_BANK
    const uint32_t sd_t0 = System::GetNow();
    sorrow::LoadUserBankFromSd();
    const uint32_t sd_ms = System::GetNow() - sd_t0;
#endif

    // Construct every model in the pool, then boot with a rolled kit.
    sorrow::VoicesInit(sr, []() -> uint32_t { return System::GetUs(); });
    RollKit();

#if SORROW_LOG_USB && SORROW_SD_USER_BANK
    patch.PrintLine("user bank: %s (%d ms)  sd=%d fs=%d mount=%d path=%s",
                    sorrow::UserBankStatus(), static_cast<int>(sd_ms),
                    sorrow::UserBankSdResult(), sorrow::UserBankFsResult(),
                    sorrow::UserBankMountResult(), sorrow::UserBankPath());
    patch.PrintLine("           open=%d", sorrow::UserBankOpenResult());
#endif

#if SORROW_LOG_USB
    // Dump what the boot benchmark measured, so the cost budget is set from
    // numbers rather than inference. Two desktop benchmarks misled me about
    // which models were expensive; the target measuring itself does not.
    static const char* const kSlotNames[3] = {"kick", "snare", "hat"};
    patch.PrintLine("");
    patch.PrintLine("=== Sorrow v%s ===", SORROW_VERSION);
    patch.PrintLine("pattern banks: %d%s",
                    daisy_grids::grids_port::BankCount(),
                    daisy_grids::grids_port::BankCount() > 3
                        ? " (user bank loaded from SD)"
                        : " (no user bank on card)");
    patch.PrintLine("model cost, pct of one sample period:");
    for(uint8_t slot = 0; slot < 3; ++slot)
    {
        const Slot sl = static_cast<Slot>(slot);
        for(uint8_t m = 0; m < sorrow::VoiceModelCount(sl); ++m)
        {
            patch.PrintLine("  %-6s %-12s %3d pct",
                            kSlotNames[slot],
                            sorrow::VoiceModelNameAt(sl, m),
                            static_cast<int>(sorrow::VoiceCostAt(sl, m) * 100.0f + 0.5f));
            System::Delay(12);   // don't outrun the USB CDC buffer
        }
    }
    patch.PrintLine("  cheapest possible kit: %d pct",
                    static_cast<int>(sorrow::VoiceCheapestKitCost() * 100.0f + 0.5f));
    patch.PrintLine("");
#endif

#if SORROW_REPORT_KIT
    // Blink out which models the boot roll selected, before audio starts, so
    // the report always completes even if the running firmware then misbehaves.
    // Per slot: (index + 1) short blinks, then a gap. Kick, snare, hat.
    //   Kick : 1=synth BD  2=analog BD  3=modal BD
    //   Snare: 1=synth SD  2=analog SD  3=modal SD  4=string SD
    //   Hat  : 1=square HH 2=ringmod HH 3=modal HH
    for(int repeat = 0; repeat < 2; ++repeat)
    {
        for(uint8_t slot = 0; slot < 3; ++slot)
        {
            const uint8_t n
                = sorrow::VoiceModelIndex(static_cast<Slot>(slot)) + 1;
            for(uint8_t b = 0; b < n; ++b)
            {
                SetPanelLed(true, kLedVoltsFull);
                System::Delay(120);
                SetPanelLed(false, kLedVoltsFull);
                System::Delay(200);
            }
            System::Delay(700);   // gap between slots
        }
        System::Delay(1200);      // gap between repeats
    }
#endif

    // Initialize UI switches
    s_mode_btn.Init(patch.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);  // Cycles sub-modes
    s_output_sw.Init(patch.B8, sr, Switch::TYPE_TOGGLE, Switch::POLARITY_NORMAL);      // Rolls the kit

    // Initialize external trigger gate outputs (B5, B6)
    g_gate_kick.Init(patch.B5, GPIO::Mode::OUTPUT);
    g_gate_snare.Init(patch.B6, GPIO::Mode::OUTPUT);

#if SORROW_REPORT_CPU
    g_cpu_meter.Init(sr, patch.AudioBlockSize());
#endif

    // Start audio
    patch.StartAudio(AudioCallback);

    // Main loop. All audio work happens in the callback; this only reports.
    uint32_t next_report = 0;
    while(1)
    {
        System::Delay(10);

#if SORROW_LOG_USB && SORROW_REPORT_CPU
        // Total audio load, measured rather than inferred. The per-model costs
        // only cover the three voices; this includes the sequencer, clock,
        // panel and mix, which is what actually decides a safe kit budget.
        const uint32_t now = System::GetNow();
        if(now >= next_report)
        {
            next_report = now + 2000;
            patch.PrintLine("cpu avg %3d pct  max %3d pct   kit: %s / %s / %s",
                            static_cast<int>(g_cpu_meter.GetAvgCpuLoad() * 100.0f + 0.5f),
                            static_cast<int>(g_cpu_meter.GetMaxCpuLoad() * 100.0f + 0.5f),
                            sorrow::VoiceModelName(Slot::Kick),
                            sorrow::VoiceModelName(Slot::Snare),
                            sorrow::VoiceModelName(Slot::Hat));
        }
#endif
    }
}
