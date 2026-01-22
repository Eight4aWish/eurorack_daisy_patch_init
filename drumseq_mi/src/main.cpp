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
//
// B10 (Gate In 1): External clock input - rising edge advances step
// B9  (Gate In 2): Reset input - rising edge resets pattern to step 0
//
// B7 Momentary Button: Cycle through sub-modes
//   Mode 0 (1 pulse):  Pattern - CV1=X, CV2=Y, CV3=Density, CV4=Randomness
//   Mode 1 (2 pulses): Edit Kick  - CV1=Freq, CV2=Decay, CV3=Pan, CV4=Volume
//   Mode 2 (3 pulses): Edit Snare - CV1=Freq, CV2=Snappy, CV3=Pan, CV4=Volume
//   Mode 3 (4 pulses): Edit HiHat - CV1=Freq, CV2=Decay, CV3=Pan, CV4=Volume
// =============================================================================

namespace
{

DaisyPatchSM patch;
drumseq_mi::grids_port::GridsDrumGenerator grids;

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

// Per-drum mix parameters (pan: 0=left, 0.5=center, 1=right; vol: 0-1)
static float g_kick_pan  = 0.5f;
static float g_kick_vol  = 0.8f;
static float g_snare_pan = 0.5f;
static float g_snare_vol = 0.7f;
static float g_hat_pan   = 0.5f;
static float g_hat_vol   = 0.6f;

static constexpr float kLedVoltsOn = 5.0f;

// -----------------------------------------------------------------------------
// External Clock & Reset (B10, B9)
// Manual edge detection with state tracking for reliability
// Clock multiplier: external clock is assumed to be quarter notes (1 ppqn)
// We multiply by 4 to get 16th notes for Grids
// Once external clock is detected, internal clock is disabled until reset
// -----------------------------------------------------------------------------
static bool g_ext_clk_state = false;  // Current clock state
static bool g_ext_rst_state = false;  // Current reset state
static bool g_use_ext_clock = false;  // True if external clock mode (latches on)

// Clock multiplier state (4× to convert quarter notes to 16th notes)
static constexpr uint8_t kClockMultiplier = 4;
static uint32_t g_ext_clk_period = 0;     // Samples between last two clock edges
static uint32_t g_ext_clk_last_edge = 0;  // Sample count at last clock edge
static uint32_t g_ext_clk_counter = 0;    // Counter for generating multiplied ticks
static uint8_t  g_ext_clk_mult_phase = 0; // Which of the 4 sub-ticks we're on
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

    // B7 momentary button cycles through sub-modes (pattern + 3 edit modes)
    if(s_mode_btn.RisingEdge())
    {
        g_sub_mode = (g_sub_mode + 1) % kNumSubModes;
    }
    // B8 toggle switch selects internal synth vs external triggers
    g_external_output = s_output_sw.Pressed();

    // Update LED - pulse count indicates sub-mode (1-4 pulses)
    g_led_ctr += size;
    float t = static_cast<float>(g_led_ctr) / patch.AudioSampleRate();
    SetPanelLed(LedPulseState(static_cast<uint8_t>(g_sub_mode + 1), t));

    // Read knobs - map bipolar (-1..+1) to unipolar (0..1)
    float k1 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_1)) + 1.0f));
    float k2 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_2)) + 1.0f));
    float k3 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_3)) + 1.0f));
    float k4 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_4)) + 1.0f));

    const float sr = patch.AudioSampleRate();

    // Reset pattern on rising edge of reset input
    if(ext_rst_rising)
    {
        grids.Reset();
        g_ext_clk_mult_phase = 0;
    }

    // Update global sample counter
    g_sample_counter += size;

    // Track external clock presence and period
    // Once external clock is detected, stay in external mode (no fallback to internal)
    // Use reset input to return to internal clock mode
    if(ext_clk_rising)
    {
        // Calculate period since last clock edge
        uint32_t now = g_sample_counter;
        if(g_ext_clk_last_edge > 0 && now > g_ext_clk_last_edge)
        {
            g_ext_clk_period = now - g_ext_clk_last_edge;
        }
        g_ext_clk_last_edge = now;
        g_ext_clk_counter = 0;
        g_ext_clk_mult_phase = 0;
        
        g_use_ext_clock = true;
    }
    
    // Reset input also clears external clock mode (returns to internal clock)
    if(ext_rst_rising)
    {
        g_use_ext_clock = false;
        g_ext_clk_period = 0;
        g_ext_clk_last_edge = 0;
    }

    // Generate multiplied clock ticks (4× for 16th notes from quarter notes)
    bool ext_tick = false;
    if(g_use_ext_clock && g_ext_clk_period > 0)
    {
        // First tick happens immediately on clock edge
        if(ext_clk_rising)
        {
            ext_tick = true;
        }
        else
        {
            // Generate interpolated ticks at 1/4, 2/4, 3/4 of the period
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
    switch(g_sub_mode)
    {
        case 1: // Edit Kick
            kick.SetFreq(30.0f + k1 * 120.0f);      // 30-150 Hz
            kick.SetDecay(0.05f + k2 * 0.50f);      // 0.05-0.55
            g_kick_pan = k3;
            g_kick_vol = k4;
            break;
        case 2: // Edit Snare
            snare.SetFreq(100.0f + k1 * 300.0f);    // 100-400 Hz
            snare.SetSnappy(k2);                    // 0-1
            g_snare_pan = k3;
            g_snare_vol = k4;
            break;
        case 3: // Edit HiHat
            hat.SetFreq(4000.0f + k1 * 12000.0f);   // 4k-16k Hz
            hat.SetDecay(0.02f + k2 * 0.80f);       // 0.02-0.82
            g_hat_pan = k3;
            g_hat_vol = k4;
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
            // Grids parameters from all 4 knobs
            const uint8_t x    = static_cast<uint8_t>(k1 * 255.0f);
            const uint8_t y    = static_cast<uint8_t>(k2 * 255.0f);
            const uint8_t dens = static_cast<uint8_t>(k3 * 255.0f);
            const uint8_t rnd  = static_cast<uint8_t>(k4 * 255.0f);

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

            // Per-drum pan/volume: pan 0=left, 0.5=center, 1=right
            const float kick_l  = kick_out  * g_kick_vol  * (1.0f - g_kick_pan);
            const float kick_r  = kick_out  * g_kick_vol  * g_kick_pan;
            const float snare_l = snare_out * g_snare_vol * (1.0f - g_snare_pan);
            const float snare_r = snare_out * g_snare_vol * g_snare_pan;
            const float hat_l   = hat_out   * g_hat_vol   * (1.0f - g_hat_pan);
            const float hat_r   = hat_out   * g_hat_vol   * g_hat_pan;

            // Mix and saturate
            float mix_l = 0.95f * kick_l + 0.70f * snare_l + 1.35f * hat_l;
            float mix_r = 0.95f * kick_r + 0.70f * snare_r + 1.35f * hat_r;
            out[0][i] = FastSaturate(mix_l * 0.5f);
            out[1][i] = FastSaturate(mix_r * 0.5f);
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
