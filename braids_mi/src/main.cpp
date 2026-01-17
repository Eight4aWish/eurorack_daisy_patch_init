#include "daisy_patch_sm.h"

#include "braids_variant.h"
#include "voct_config.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace patch_sm;

namespace {

constexpr size_t kBraidsBlockSize = 24;

// V/Oct calibration knobs.
// - VOCT_BASE_MIDI: MIDI note at 0V. Common conventions: C2=36, C3=48, C4=60.
// - VOCT_CENTER_NORM: normalized ADC value corresponding to 0V (typically ~0.5 for bipolar inputs).
#ifndef VOCT_BASE_MIDI
#define VOCT_BASE_MIDI 48
#endif

#ifndef VOCT_CENTER_NORM
#define VOCT_CENTER_NORM 0.5f
#endif

constexpr int32_t kBaseNoteQ7 = (static_cast<int32_t>(VOCT_BASE_MIDI) << 7);
constexpr float   kVoctCenterNorm = static_cast<float>(VOCT_CENTER_NORM);

constexpr int kModelBankCount = 4;

inline float Clamp01(float v)
{
    return std::max(0.0f, std::min(1.0f, v));
}

inline int16_t Float01ToParamQ15(float v)
{
    v = Clamp01(v);
    return static_cast<int16_t>(std::lround(v * 32767.0f));
}

inline int16_t SemitonesToQ7(float semitones)
{
    return static_cast<int16_t>(std::lround(semitones * 128.0f));
}

inline int16_t ClampI16(int32_t v)
{
    if(v > 32767)
        return 32767;
    if(v < -32768)
        return -32768;
    return static_cast<int16_t>(v);
}

struct AdEnvelope
{
    enum class Stage : uint8_t
    {
        Dead,
        Attack,
        Sustain,
        Decay,
    };

    void Init(float sample_rate_hz)
    {
        sample_rate_hz_ = std::max(1.0f, sample_rate_hz);
        dt_ms_ = 1000.0f / sample_rate_hz_;
        stage_ = Stage::Dead;
        level_ = 0.0f;
        attack_ms_ = 10.0f;
        decay_ms_ = 100.0f;
    }

    void Trigger()
    {
        stage_ = Stage::Attack;
    }

    void SetGate(bool gate)
    {
        const bool rising = gate && !gate_;
        const bool falling = !gate && gate_;
        gate_ = gate;

        if(rising)
        {
            stage_ = Stage::Attack;
        }
        else if(falling)
        {
            if(stage_ != Stage::Dead)
                stage_ = Stage::Decay;
        }
    }

    void SetAttackDecayMs(float attack_ms, float decay_ms)
    {
        attack_ms_ = std::max(0.0f, attack_ms);
        decay_ms_ = std::max(0.0f, decay_ms);
    }

    float Process()
    {
        switch(stage_)
        {
            case Stage::Dead:
                level_ = 0.0f;
                break;

            case Stage::Attack:
            {
                if(attack_ms_ <= 0.0f)
                {
                    level_ = 1.0f;
                }
                else
                {
                    level_ += dt_ms_ / attack_ms_;
                    if(level_ >= 1.0f)
                        level_ = 1.0f;
                }

                if(level_ >= 1.0f)
                    stage_ = gate_ ? Stage::Sustain : Stage::Decay;
            }
            break;

            case Stage::Sustain:
                level_ = 1.0f;
                if(!gate_)
                    stage_ = Stage::Decay;
                break;

            case Stage::Decay:
            {
                if(decay_ms_ <= 0.0f)
                {
                    level_ = 0.0f;
                }
                else
                {
                    level_ -= dt_ms_ / decay_ms_;
                    if(level_ <= 0.0f)
                        level_ = 0.0f;
                }

                if(level_ <= 0.0f)
                    stage_ = Stage::Dead;
            }
            break;
        }

        return level_;
    }

  private:
    float sample_rate_hz_ = 48000.0f;
    float dt_ms_ = 1000.0f / 48000.0f;
    Stage stage_ = Stage::Dead;
    float level_ = 0.0f;
    float attack_ms_ = 10.0f;
    float decay_ms_ = 100.0f;
    bool  gate_ = false;
};

} // namespace

DaisyPatchSM hw;
Switch       shift_switch;
Switch       bank_button;

braids::MacroOscillator osc;
AdEnvelope              amp_env;

uint8_t sync_buffer[kBraidsBlockSize];
int16_t render_buffer[kBraidsBlockSize];

// State carried between callbacks
float page_level = 0.8f;

float env_attack_norm = 0.35f;
float env_decay_norm  = 0.55f;

int model_bank = 0;

// LED blink state for bank indication.
float  control_rate_hz       = 0.0f;
size_t led_tick_countdown    = 0;
int    led_toggles_remaining = 0;
bool   led_state             = false;

// Gate edges must be read exactly once per audio callback.
// Trig() is edge-based; multiple reads per callback can consume the edge.
bool gate1_trig = false;
bool gate2_trig = false;
bool gate1_state = false;

static constexpr float kPanelLedVoltsOn = 4.0f;

inline void SetPanelLed(bool on)
{
    // Seed LED (may not be visible once mounted)
    hw.SetLed(on);
    // Front-panel LED on many Patch SM builds is driven from a CV DAC output.
    hw.WriteCvOut(CV_OUT_2, on ? kPanelLedVoltsOn : 0.0f);
}

inline void SetBankLedSteady()
{
    // After the bank-change blink pattern completes, keep the LED off.
    // (The blink count is the bank indicator.)
    led_state = false;
    SetPanelLed(false);
}

inline void StartBankBlink()
{
    // Blink N times (N = bank+1) on bank change.
    const int blinks = model_bank + 1;
    led_toggles_remaining = blinks * 2;
    led_tick_countdown = 0;
}

inline void TickBankLed()
{
    if(led_toggles_remaining <= 0)
        return;

    if(led_tick_countdown > 0)
    {
        led_tick_countdown--;
        return;
    }

    // ~120ms per edge (on/off)
    const float interval_s = 0.12f;
    led_tick_countdown = static_cast<size_t>(std::max(1.0f, control_rate_hz * interval_s));

    led_state = !led_state;
    SetPanelLed(led_state);
    led_toggles_remaining--;
    if(led_toggles_remaining <= 0)
        SetBankLedSteady();
}

void ProcessControls()
{
    hw.ProcessAllControls();
    shift_switch.Debounce();
    bank_button.Debounce();

    const bool shift = shift_switch.Pressed();

    // Bank select on B7.
    if(bank_button.RisingEdge())
    {
        model_bank = (model_bank + 1) % kModelBankCount;
        StartBankBlink();
    }

    TickBankLed();

    // Gate-controlled envelope (AR): sustain while gate held.
    amp_env.SetGate(gate1_state);

    // Strike on rising edge.
    if(gate1_trig)
    {
        osc.Strike();
    }

    // Model select on CV_4 with hysteresis (prevents jitter at boundaries)
    const float model_knob = Clamp01(hw.GetAdcValue(CV_4));
#if defined(BRAIDS_VARIANT_FULL)
    // Upstream Braids has 48 shapes (0..47) and MACRO_OSC_SHAPE_LAST == 48.
    // Hard-code this so the UI exposes all shapes even if a trimmed header
    // accidentally ends up on the include path.
    constexpr int last_shape = 48;
#else
    const int last_shape = static_cast<int>(braids::MACRO_OSC_SHAPE_LAST);
#endif
    const int   max_shape  = last_shape - 1;
    const int   models_per_bank = std::max(1, (last_shape + kModelBankCount - 1) / kModelBankCount);
    const int   max_bank   = (max_shape + models_per_bank) / models_per_bank;
    if(model_bank >= max_bank)
        model_bank = max_bank - 1;

    static int within_idx = 0;

    // Step size and deadband width (fraction of one step)
    const float step = 1.0f / static_cast<float>(models_per_bank);
    const float h    = 0.18f; // ~18% of a step on each side

    // Desired index without hysteresis.
    const int desired = std::max(0,
                                 std::min(models_per_bank - 1,
                                          static_cast<int>(model_knob * static_cast<float>(models_per_bank))));

    // Allow fast jumps when the knob moves far, but require crossing a deadband
    // before stepping across each boundary.
        if(desired > within_idx)
    {
                while(desired > within_idx
                            && model_knob >= (static_cast<float>(within_idx + 1) * step + h * step))
                        within_idx++;
    }
        else if(desired < within_idx)
    {
                while(desired < within_idx
                            && model_knob <= (static_cast<float>(within_idx) * step - h * step))
                        within_idx--;
    }

        const int shape_idx = model_bank * models_per_bank + within_idx;
        osc.set_shape(static_cast<braids::MacroOscillatorShape>(std::min(shape_idx, max_shape)));

    // Page mapping (CV_1..CV_3)
    if(!shift)
    {
        const float timbre_knob = hw.GetAdcValue(CV_1);
        const float color_knob  = hw.GetAdcValue(CV_2);
        page_level              = Clamp01(hw.GetAdcValue(CV_3));

        // Bipolar mod sources (Patch SM CV_5..CV_8 are centered at ~0.5)
        const float timbre_mod = (hw.GetAdcValue(CV_6) - 0.5f) * 2.0f * 0.5f;
        const float color_mod  = (hw.GetAdcValue(CV_7) - 0.5f) * 2.0f * 0.5f;

        osc.set_parameters(Float01ToParamQ15(timbre_knob + timbre_mod),
                           Float01ToParamQ15(color_knob + color_mod));
    }
    else
    {
        env_attack_norm = Clamp01(hw.GetAdcValue(CV_1));
        env_decay_norm  = Clamp01(hw.GetAdcValue(CV_2));
        page_level     = Clamp01(hw.GetAdcValue(CV_3));
    }

    // Wide-range AD envelope mapping (based on the pico2w_oc envelope curve).
    // Squaring gives better control over longer times.
    static constexpr float kMaxAttackMs = 6000.0f;
    static constexpr float kMaxDecayMs  = 6000.0f;
    const float attack_ms = 1.0f + (env_attack_norm * env_attack_norm) * kMaxAttackMs;
    const float decay_ms  = 1.0f + (env_decay_norm * env_decay_norm) * kMaxDecayMs;
    amp_env.SetAttackDecayMs(attack_ms, decay_ms);

    // Pitch: CV_5 as V/Oct (Patch SM bipolar CV is normalized to 0..1).
    // Map -5..+5V to 0..1, with 0V at ~kVoctCenterNorm.
    // Then map to ±60 semitones around 0V.
    const float voct_cv        = Clamp01(hw.GetAdcValue(CV_5));
    const float voct_semitones = (voct_cv - kVoctCenterNorm) * 120.0f;

    const int32_t pitch_q7 = kBaseNoteQ7 + static_cast<int32_t>(SemitonesToQ7(voct_semitones));
    osc.set_pitch(ClampI16(pitch_q7));

    // Sync edge from Gate In 2 (pulse once when a rising edge was detected).
    std::memset(sync_buffer, 0, sizeof(sync_buffer));
    if(gate2_trig)
        sync_buffer[0] = 1;
}

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    (void)in;

    // IMPORTANT: read gate state/edges exactly once per audio callback.
    gate1_state = hw.gate_in_1.State();
    gate1_trig  = hw.gate_in_1.Trig();
    gate2_trig  = hw.gate_in_2.Trig();

    // Process in Braids-sized chunks (osc has internal 24-sample buffers).
    size_t i = 0;
    while(i < size)
    {
        const size_t n = std::min(kBraidsBlockSize, size - i);

        ProcessControls();

        // Render into int16 buffer (Q15-ish audio)
        osc.Render(sync_buffer, render_buffer, n);

        for(size_t j = 0; j < n; j++)
        {
            const float env_amp = amp_env.Process();
            const float s = static_cast<float>(render_buffer[j]) / 32768.0f;
            const float y = s * page_level * env_amp;
            out[0][i + j] = y;
            out[1][i + j] = y;
        }

        i += n;
    }
}

int main(void)
{
    hw.Init();

    const float sample_rate = hw.AudioSampleRate();
    control_rate_hz          = sample_rate / static_cast<float>(kBraidsBlockSize);

    // Patch SM panel controls
    shift_switch.Init(hw.B8, sample_rate, Switch::TYPE_TOGGLE, Switch::POLARITY_NORMAL);
    bank_button.Init(hw.B7,
                     sample_rate,
                     Switch::TYPE_MOMENTARY,
                     Switch::POLARITY_INVERTED);

    // Required for driving CV_OUT_2 (used as panel LED on many Patch SM panels).
    hw.StartDac();

    hw.SetAudioBlockSize(kBraidsBlockSize);

    osc.Init();

    amp_env.Init(sample_rate);

    SetBankLedSteady();

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
        System::Delay(1);
}
