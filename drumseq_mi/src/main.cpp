#include "daisy_patch_sm.h"
#include "daisysp.h"

#include "grids_port.h"

#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// =============================================================================
// Phase 1 & 2 Rebuild: Simplified architecture matching braids_mi/multifx
// - ProcessAllControls called inside audio callback (proven pattern)
// - No seqlock complexity
// - Analog kit only in Grids mode for initial verification
// =============================================================================

namespace
{

DaisyPatchSM patch;
drumseq_mi::grids_port::GridsDrumGenerator grids;

// -----------------------------------------------------------------------------
// Drum Voices - Synthetic kit for Phase 1/2
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
// UI State (simplified - matches multifx pattern)
// -----------------------------------------------------------------------------
static Switch   s_mode_btn;  // B7
static Switch   s_shift_sw;  // B8
static uint8_t  g_patch_index = 0;
static bool     g_shift       = false;
static uint32_t g_led_ctr     = 0;

static constexpr float kLedVoltsOn = 5.0f;

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
// Internal Clock - 120 BPM, 16th notes (8 ticks/sec)
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
// Audio Callback - Simplified (matches braids_mi/multifx pattern)
// ProcessAllControls called inside callback for reliable timing
// -----------------------------------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    (void)in;

    // Process all controls inside callback (proven pattern from working patches)
    patch.ProcessAllControls();

    // Update UI state
    s_mode_btn.Debounce();
    s_shift_sw.Debounce();

    if(s_mode_btn.RisingEdge())
    {
        g_patch_index = (g_patch_index + 1) % 4;
    }
    g_shift = s_shift_sw.Pressed();

    // Update LED
    g_led_ctr += size;
    float t = static_cast<float>(g_led_ctr) / patch.AudioSampleRate();
    SetPanelLed(LedPulseState(static_cast<uint8_t>(g_patch_index + 1), t));

    // Read knobs - map bipolar (-1..+1) to unipolar (0..1)
    float k1 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_1)) + 1.0f));
    float k2 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_2)) + 1.0f));
    float k3 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_3)) + 1.0f));
    float k4 = Clamp01f(0.5f * (Clamp11f(patch.GetAdcValue(CV_4)) + 1.0f));

    const float sr = patch.AudioSampleRate();

    // Phase 1/2: Only Grids mode with analog kit for verification
    // Check for clock tick
    bool tick = InternalGridsClockTick(sr, size);

    if(tick)
    {
        // Convert knob values to Grids parameters (0..255)
        const uint8_t x    = static_cast<uint8_t>(k1 * 255.0f);
        const uint8_t y    = static_cast<uint8_t>(k2 * 255.0f);
        const uint8_t dens = static_cast<uint8_t>(k3 * 255.0f);
        const uint8_t rnd  = static_cast<uint8_t>(k4 * 255.0f);

        // Get triggers from Grids pattern generator
        const auto step = grids.Tick(x, y, dens, dens, dens, rnd);

        // Trigger drum voices with accent
        if(step.bd)
            TrigWithAccent(kick, step.bd_accent, kKickAccentNormal, kKickAccentStrong);
        if(step.sd)
            TrigWithAccent(snare, step.sd_accent, kSnareAccentNormal, kSnareAccentStrong);
        if(step.hh)
            TrigWithAccent(hat, step.hh_accent, kHatAccentNormal, kHatAccentStrong);
    }

    // Render audio - mix all three voices
    constexpr float kMaster = 0.30f;  // Master level for mix
    for(size_t i = 0; i < size; i++)
    {
        float mix = 0.0f;
        mix += 0.95f * kick.Process(false);
        mix += 0.70f * snare.Process(false);
        mix += 1.35f * hat.Process(false);
        mix *= kMaster;
        mix = FastSaturate(mix);
        out[0][i] = mix;
        out[1][i] = mix;
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Main - Simplified initialization
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
    s_mode_btn.Init(patch.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);
    s_shift_sw.Init(patch.B8, sr, Switch::TYPE_TOGGLE, Switch::POLARITY_NORMAL);

    // Start audio
    patch.StartAudio(AudioCallback);

    // Main loop - just keep running (all work done in callback)
    while(1)
    {
        System::Delay(10);
    }
}
