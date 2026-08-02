/**
 * JOY LITE — screenless macro oscillator for the Electrosmith Daisy Patch Init.
 *
 * A curated 16 of the Braids models that Plaits doesn't cover, in two banks of 8
 * (toggle = bank A/B). No OLED: the LED blinks the model number (1-8) on change.
 * Based on Mutable Instruments Braids by Emilie Gillet (MIT); not affiliated
 * with or endorsed by Mutable Instruments or Electrosmith. See README.
 *
 * CONTROLS:
 *   KNOB 1 (CV_1): Timbre        KNOB 2 (CV_2): Color
 *   KNOB 3 (CV_3): Attack        KNOB 4 (CV_4): Decay
 *   CV_5: V/Oct   CV_6: Timbre mod   CV_7: Color mod
 *   GATE IN 1: trigger/gate      GATE IN 2: hard sync
 *   TOGGLE (B8): bank A / bank B
 *   B7 short: next model in bank (wraps); LED blinks the number
 *   B7 long : re-blink current model number
 *   B7 held at power-up: V/Oct calibration (LED solid = feed 1V + press;
 *                        LED blinking = feed 3V + press; flurry = done)
 */

#include "daisy_patch_sm.h"
#include "voct_cal.h"
#include "joy_dsp.h"

#include <braids/macro_oscillator.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace patch_sm;

namespace {

constexpr size_t kBraidsBlockSize = 24;

// V/Oct calibration defaults (also the fallback / per-build overrides).
#ifndef VOCT_BASE_MIDI
#define VOCT_BASE_MIDI 48
#endif
#ifndef VOCT_CENTER_NORM
#define VOCT_CENTER_NORM 0.007074f
#endif
#ifndef VOCT_SLOPE
#define VOCT_SLOPE 60.73f
#endif
constexpr int32_t kBaseNoteQ7     = (static_cast<int32_t>(VOCT_BASE_MIDI) << 7);
constexpr float   kVoctCenterNorm = static_cast<float>(VOCT_CENTER_NORM);
constexpr float   kVoctSlope      = static_cast<float>(VOCT_SLOPE);
joy::VoctCal      g_voct_cal      = {kVoctCenterNorm, kVoctSlope};

// The curated 16: Braids models not really covered by Plaits, two banks of 8.
constexpr int kBanks         = 2;
constexpr int kModelsPerBank = 8;
const braids::MacroOscillatorShape kModels[kBanks][kModelsPerBank] = {
    // Bank A (toggle down) — headliners
    {
        braids::MACRO_OSC_SHAPE_CSAW,
        braids::MACRO_OSC_SHAPE_SAW_SYNC,
        braids::MACRO_OSC_SHAPE_TRIPLE_RING_MOD,
        braids::MACRO_OSC_SHAPE_VOSIM,
        braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_BP,
        braids::MACRO_OSC_SHAPE_CHAOTIC_FEEDBACK_FM,
        braids::MACRO_OSC_SHAPE_DIGITAL_MODULATION,
        braids::MACRO_OSC_SHAPE_TOY,
    },
    // Bank B (toggle up) — deeper cuts
    {
        braids::MACRO_OSC_SHAPE_SQUARE_SYNC,
        braids::MACRO_OSC_SHAPE_BUZZ,
        braids::MACRO_OSC_SHAPE_SAW_COMB,
        braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_LP,
        braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_HP,
        braids::MACRO_OSC_SHAPE_CLOCKED_NOISE,
        braids::MACRO_OSC_SHAPE_TWIN_PEAKS_NOISE,
        braids::MACRO_OSC_SHAPE_QUESTION_MARK,
    },
};

// Persistent settings: per-bank last model + calibration.
constexpr int      kSettingsVersion    = 1;
constexpr uint32_t kSettingsQspiOffset = 0x7F0000;
struct JoyLiteSettings
{
    int   version;
    int   model[kBanks];
    float voct_center;
    float voct_slope;
    bool operator!=(const JoyLiteSettings &r) const
    {
        return version != r.version || model[0] != r.model[0]
               || model[1] != r.model[1] || voct_center != r.voct_center
               || voct_slope != r.voct_slope;
    }
};

} // namespace

// Hardware
DaisyPatchSM hw;
Switch       nav_button;   // B7
Switch       bank_toggle;  // B8

braids::MacroOscillator osc;
joy::AdEnvelope         amp_env;

uint8_t sync_buffer[kBraidsBlockSize];
int16_t render_buffer[kBraidsBlockSize];

PersistentStorage<JoyLiteSettings> settings_storage(hw.qspi);
volatile bool     settings_dirty      = false;
volatile uint32_t settings_dirty_time = 0;
constexpr uint32_t kSettingsSaveDelayMs = 2000;

// Navigation state
int  current_bank  = 0;
int  model_idx[kBanks] = {0, 0};

// Gate state (read once per callback)
bool gate1_state = false;
bool gate1_trig  = false;
bool gate2_trig  = false;

// Button timing
uint32_t button_press_start = 0;
bool     button_was_pressed = false;
constexpr uint32_t kLongPressMs = 500;

// LED: number-blink requests (set from the audio thread, rendered in main loop).
static constexpr float kPanelLedVoltsOn = 4.0f;
volatile int g_blink_request = -1;  // >=0 => blink that many times

inline void SetPanelLed(bool on)
{
    hw.SetLed(on);
    hw.WriteCvOut(CV_OUT_2, on ? kPanelLedVoltsOn : 0.0f);
}

void ApplyModel()
{
    osc.set_shape(kModels[current_bank][model_idx[current_bank]]);
}

inline void RequestBlink()
{
    g_blink_request = model_idx[current_bank] + 1;  // 1..8
}

void MarkDirty()
{
    settings_dirty      = true;
    settings_dirty_time = System::GetNow();
}

void ProcessNavigation()
{
    nav_button.Debounce();
    bank_toggle.Debounce();

    // Toggle selects the bank live; flipping it recalls that bank's last model.
    const int new_bank = bank_toggle.Pressed() ? 1 : 0;
    if(new_bank != current_bank)
    {
        current_bank = new_bank;
        ApplyModel();
        RequestBlink();
        MarkDirty();
    }

    const uint32_t now = System::GetNow();
    if(nav_button.Pressed())
    {
        if(!button_was_pressed)
        {
            button_press_start = now;
            button_was_pressed = true;
        }
    }
    else if(button_was_pressed)
    {
        const uint32_t dur = now - button_press_start;
        if(dur >= kLongPressMs)
        {
            RequestBlink();  // long press: re-show current model number
        }
        else
        {
            model_idx[current_bank]
                = (model_idx[current_bank] + 1) % kModelsPerBank;
            ApplyModel();
            RequestBlink();
            MarkDirty();
        }
        button_was_pressed = false;
    }
}

void ProcessControls()
{
    hw.ProcessAllControls();
    ProcessNavigation();

    amp_env.SetGate(gate1_state);
    if(gate1_trig)
        osc.Strike();

    const float timbre_knob = joy::Clamp01(hw.GetAdcValue(CV_1));
    const float color_knob  = joy::Clamp01(hw.GetAdcValue(CV_2));
    const float timbre_mod  = joy::Clamp11(hw.GetAdcValue(CV_6)) * 0.5f;
    const float color_mod   = joy::Clamp11(hw.GetAdcValue(CV_7)) * 0.5f;
    osc.set_parameters(joy::Float01ToParamQ15(timbre_knob + timbre_mod),
                       joy::Float01ToParamQ15(color_knob + color_mod));

    const float env_attack_norm = joy::Clamp01(hw.GetAdcValue(CV_3));
    const float env_decay_norm  = joy::Clamp01(hw.GetAdcValue(CV_4));
    static constexpr float kMaxMs = 6000.0f;
    amp_env.SetAttackDecayMs(1.0f + env_attack_norm * env_attack_norm * kMaxMs,
                             1.0f + env_decay_norm * env_decay_norm * kMaxMs);

    const float   voct_cv        = joy::Clamp11(hw.GetAdcValue(CV_5));
    const float   voct_semitones = joy::VoctToSemitones(g_voct_cal, voct_cv);
    const int32_t pitch_q7 = kBaseNoteQ7 + static_cast<int32_t>(joy::SemitonesToQ7(voct_semitones));
    osc.set_pitch(joy::ClampI16(pitch_q7));

    std::memset(sync_buffer, 0, sizeof(sync_buffer));
    if(gate2_trig)
        sync_buffer[0] = 1;
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in;
    gate1_state = hw.gate_in_1.State();
    gate1_trig  = hw.gate_in_1.Trig();
    gate2_trig  = hw.gate_in_2.Trig();

    size_t i = 0;
    while(i < size)
    {
        const size_t n = std::min(kBraidsBlockSize, size - i);
        ProcessControls();
        osc.Render(sync_buffer, render_buffer, n);
        for(size_t j = 0; j < n; j++)
        {
            const float env_amp = amp_env.Process();
            const float s       = static_cast<float>(render_buffer[j]) / 32768.0f;
            const float y       = s * env_amp;
            out[0][i + j]       = y;
            out[1][i + j]       = y;
        }
        i += n;
    }
}

// ---- Calibration (LED-guided, no screen) --------------------------------

// Wait for a fresh press (release then press). While waiting: LED solid if
// !blink, else a slow ~3 Hz blink. Distinguishes the two calibration steps.
void WaitForFreshPress(bool blink)
{
    uint32_t t  = System::GetNow();
    bool     on = true;
    SetPanelLed(true);
    auto drive = [&]() {
        if(blink)
        {
            const uint32_t now = System::GetNow();
            if(now - t > 150)
            {
                t  = now;
                on = !on;
                SetPanelLed(on);
            }
        }
    };
    do {
        hw.ProcessAllControls();
        nav_button.Debounce();
        drive();
        System::Delay(1);
    } while(nav_button.Pressed());
    do {
        hw.ProcessAllControls();
        nav_button.Debounce();
        drive();
        System::Delay(1);
    } while(!nav_button.Pressed());
}

float CaptureVoct()
{
    constexpr int kN = 200;
    float         acc = 0.0f;
    for(int i = 0; i < kN; i++)
    {
        hw.ProcessAllControls();
        acc += joy::Clamp11(hw.GetAdcValue(CV_5));
        System::Delay(1);
    }
    return acc / static_cast<float>(kN);
}

void RunCalibration()
{
    WaitForFreshPress(false);  // LED solid: patch 1 V, press
    const float adc_1v = CaptureVoct();

    WaitForFreshPress(true);   // LED blinking: patch 3 V, press
    const float adc_3v = CaptureVoct();

    g_voct_cal = joy::ComputeVoctCal(adc_1v, adc_3v, g_voct_cal);
    JoyLiteSettings &s = settings_storage.GetSettings();
    s.voct_center      = g_voct_cal.center;
    s.voct_slope       = g_voct_cal.slope;
    settings_storage.Save();

    for(int i = 0; i < 10; i++)  // done: rapid flurry
    {
        SetPanelLed(i & 1);
        System::Delay(60);
    }
    SetPanelLed(false);
}

int main(void)
{
    hw.Init();
    const float sample_rate = hw.AudioSampleRate();

    nav_button.Init(hw.B7, sample_rate, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);
    bank_toggle.Init(hw.B8, sample_rate, Switch::TYPE_TOGGLE, Switch::POLARITY_NORMAL);

    hw.StartDac();
    hw.SetAudioBlockSize(kBraidsBlockSize);

    osc.Init();
    amp_env.Init(sample_rate);

    // Restore per-bank model + calibration (defaults fall back to the constants).
    JoyLiteSettings defaults{kSettingsVersion, {0, 0}, kVoctCenterNorm, kVoctSlope};
    settings_storage.Init(defaults, kSettingsQspiOffset);
    if(settings_storage.GetSettings().version != kSettingsVersion)
        settings_storage.RestoreDefaults();
    const JoyLiteSettings &saved = settings_storage.GetSettings();
    model_idx[0] = saved.model[0] % kModelsPerBank;
    model_idx[1] = saved.model[1] % kModelsPerBank;
    g_voct_cal   = {saved.voct_center, saved.voct_slope};
    settings_dirty = false;

    SetPanelLed(false);
    hw.StartAdc();

    // Bank follows the toggle position; set the starting model accordingly.
    for(int i = 0; i < 40; i++) { hw.ProcessAllControls(); nav_button.Debounce(); bank_toggle.Debounce(); System::Delay(1); }
    current_bank = bank_toggle.Pressed() ? 1 : 0;
    ApplyModel();

    // Hold B7 at power-up to enter V/Oct calibration.
    if(nav_button.Pressed())
        RunCalibration();

    RequestBlink();  // show the starting model number
    hw.StartAudio(AudioCallback);

    // Main loop: render the LED (number blink over gate activity) + persist.
    uint32_t led_next    = 0;
    bool     led_on      = false;
    int      blinks_left = 0;
    while(1)
    {
        const uint32_t now = System::GetNow();

        if(g_blink_request >= 0)
        {
            blinks_left     = g_blink_request;
            g_blink_request = -1;
            led_on          = false;
            led_next        = now;
        }

        if(blinks_left > 0)
        {
            if((int32_t)(now - led_next) >= 0)
            {
                if(!led_on) { SetPanelLed(true);  led_on = true;  led_next = now + 140; }
                else        { SetPanelLed(false); led_on = false; led_next = now + 160; blinks_left--; }
            }
        }
        else
        {
            SetPanelLed(gate1_state);  // idle: mirror the gate
        }

        if(settings_dirty
           && System::GetNow() - settings_dirty_time > kSettingsSaveDelayMs)
        {
            settings_dirty     = false;
            JoyLiteSettings &s = settings_storage.GetSettings();
            s.version          = kSettingsVersion;
            s.model[0]         = model_idx[0];
            s.model[1]         = model_idx[1];
            settings_storage.Save();
        }

        System::Delay(1);
    }
}
