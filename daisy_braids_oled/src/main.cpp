/**
 * JOY — macro oscillator for the Electrosmith Daisy Patch Init
 *
 * 48 oscillator models with a 64x48 OLED display for patch/bank navigation.
 * Based on Mutable Instruments Braids by Emilie Gillet (MIT); not affiliated
 * with or endorsed by Mutable Instruments or Electrosmith. See README.
 *
 * Hardware: Daisy Patch SM, 64x48 SSD1306 OLED on soft I2C (A2=SDA, A3=SCL)
 * 
 * CONTROLS:
 *   KNOB 1 (CV_1): Timbre
 *   KNOB 2 (CV_2): Color  
 *   KNOB 3 (CV_3): Attack time (1ms - 6s)
 *   KNOB 4 (CV_4): Decay time (1ms - 6s)
 *   
 *   B7 Short Press: Cycle through patches (in Patch mode) or banks (in Bank mode)
 *   B7 Long Press:  Toggle between Patch and Bank navigation modes
 *   
 *   CV_5: V/Oct pitch input (-5V to +5V)
 *   CV_6: Timbre modulation input
 *   CV_7: Color modulation input
 *   
 *   GATE IN 1: Trigger/Gate for envelope
 *   GATE IN 2: Hard sync
 *
 * PATCHES: 48 Braids oscillator models organized into 8 thematic banks of 6
 */

#include "daisy_patch_sm.h"
#include "oled_soft_i2c.h"

#include <braids/macro_oscillator.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace patch_sm;

namespace {

constexpr size_t kBraidsBlockSize = 24;

// V/Oct calibration
#ifndef VOCT_BASE_MIDI
#define VOCT_BASE_MIDI 48
#endif

#ifndef VOCT_CENTER_NORM
#define VOCT_CENTER_NORM 0.007074f   // measured cal: nulls intercept (0.002133f left C1-C4 +30c; +0.30 st / 0.30/60.73)
#endif

constexpr int32_t kBaseNoteQ7 = (static_cast<int32_t>(VOCT_BASE_MIDI) << 7);
constexpr float   kVoctCenterNorm = static_cast<float>(VOCT_CENTER_NORM);

// Bank/Patch organization: 8 banks of 4-8 patches = 48 shapes.
// Banks follow the Braids manual's own section groupings, so sizes vary.
constexpr int kBankCount = 8;
constexpr int kMaxPatchesPerBank = 8;

// Bank definitions - thematic groupings
struct BankDef {
    const char* name;
    const char* short_name;
    int patch_count;
    braids::MacroOscillatorShape shapes[kMaxPatchesPerBank];
    const char* patch_names[kMaxPatchesPerBank];
    // Per-patch TIMBRE/COLOR reminder line ("XXXX  YYYY", col 0 and col 6),
    // abbreviated from the Braids quickstart fold-out table.
    const char* knob_labels[kMaxPatchesPerBank];
};

// Short names for the 3x3 grid menu (BNK + 8 banks)
const char* kBankShortNames[kBankCount] = {
    "ANA", "SYN", "STK", "FLT", "PHY", "PRC", "WAV", "NSE"
};

const BankDef kBanks[kBankCount] = {
    // Bank 0: ANALOG - classic analog waveforms + sub-oscillator variants
    {
        "ANALOG", "ANA", 6,
        {
            braids::MACRO_OSC_SHAPE_CSAW,
            braids::MACRO_OSC_SHAPE_MORPH,
            braids::MACRO_OSC_SHAPE_SAW_SQUARE,
            braids::MACRO_OSC_SHAPE_SINE_TRIANGLE,
            braids::MACRO_OSC_SHAPE_SQUARE_SUB,
            braids::MACRO_OSC_SHAPE_SAW_SUB
        },
        {"CSAW", "/\\/|-_-_", "/|/|-_-_", "FOLD", "SUB-_", "SUB/|"},
        {"WIDT  POLR", "WAVE  DIST", "PW    SHAP", "FOLD  SI>T", "PW    SUB", "SHAP  SUB"}
    },
    // Bank 1: SYNC+3X - hardsync pairs and triple oscillators
    {
        "SYNC+3X", "SYN", 6,
        {
            braids::MACRO_OSC_SHAPE_SQUARE_SYNC,
            braids::MACRO_OSC_SHAPE_SAW_SYNC,
            braids::MACRO_OSC_SHAPE_TRIPLE_SAW,
            braids::MACRO_OSC_SHAPE_TRIPLE_SQUARE,
            braids::MACRO_OSC_SHAPE_TRIPLE_TRIANGLE,
            braids::MACRO_OSC_SHAPE_TRIPLE_SINE
        },
        {"SYN-_", "SYN/|", "/|/| x3", "-_ x3", "/\\ x3", "SI x3"},
        {"RTIO  BAL", "RTIO  BAL", "DET2  DET3", "DET2  DET3", "DET2  DET3", "DET2  DET3"}
    },
    // Bank 2: STACK+FM - combs, ring mod, swarm, additive and FM
    {
        "STACK+FM", "STK", 8,
        {
            braids::MACRO_OSC_SHAPE_BUZZ,
            braids::MACRO_OSC_SHAPE_TRIPLE_RING_MOD,
            braids::MACRO_OSC_SHAPE_SAW_SWARM,
            braids::MACRO_OSC_SHAPE_SAW_COMB,
            braids::MACRO_OSC_SHAPE_HARMONICS,
            braids::MACRO_OSC_SHAPE_FM,
            braids::MACRO_OSC_SHAPE_FEEDBACK_FM,
            braids::MACRO_OSC_SHAPE_CHAOTIC_FEEDBACK_FM
        },
        {"_|_|_|_", "RING", "/|/|/|/|", "/|/|_|_|", "HARM", "FM", "FBFM", "WTFM"},
        {"SMTH  DTUN", "2/1   3/1", "DTUN  HPF", "DLAY  FDBK", "HRM#  PEAK",
         "INDX  RTIO", "INDX  RTIO", "INDX  RTIO"}
    },
    // Bank 3: FLT+VOX - digital filters and vocal/formant synthesis
    {
        "FLT+VOX", "FLT", 7,
        {
            braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_LP,
            braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_PK,
            braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_BP,
            braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_HP,
            braids::MACRO_OSC_SHAPE_VOSIM,
            braids::MACRO_OSC_SHAPE_VOWEL,
            braids::MACRO_OSC_SHAPE_VOWEL_FOF
        },
        {"ZLPF", "ZPKF", "ZBPF", "ZHPF", "VOSM", "VOWL", "VFOF"},
        {"CUTF  WAVE", "CUTF  WAVE", "CUTF  WAVE", "CUTF  WAVE",
         "FRM1  FRM2", "AEIO  GNDR", "AEIO  GNDR"}
    },
    // Bank 4: PHYSIC - the manual's "Physical simulations"
    {
        "PHYSIC", "PHY", 4,
        {
            braids::MACRO_OSC_SHAPE_PLUCKED,
            braids::MACRO_OSC_SHAPE_BOWED,
            braids::MACRO_OSC_SHAPE_BLOWN,
            braids::MACRO_OSC_SHAPE_FLUTED
        },
        {"PLUK", "BOWD", "BLOW", "FLUT"},
        {"DECY  POS", "FRIC  POS", "PRES  GEOM", "PRES  GEOM"}
    },
    // Bank 5: PERCUS - the manual's "Percussions" + the hidden extra
    {
        "PERCUS", "PRC", 6,
        {
            braids::MACRO_OSC_SHAPE_STRUCK_BELL,
            braids::MACRO_OSC_SHAPE_STRUCK_DRUM,
            braids::MACRO_OSC_SHAPE_KICK,
            braids::MACRO_OSC_SHAPE_CYMBAL,
            braids::MACRO_OSC_SHAPE_SNARE,
            braids::MACRO_OSC_SHAPE_QUESTION_MARK
        },
        {"BELL", "DRUM", "KICK", "CYMB", "SNAR", "????"},
        {"DECY  HARM", "DECY  HARM", "DECY  BRIT", "CUTF  NOIZ",
         "TONE  NOIZ", "????  ????"}
    },
    // Bank 6: WAVES - the manual's "Wavetables"
    {
        "WAVES", "WAV", 4,
        {
            braids::MACRO_OSC_SHAPE_WAVETABLES,
            braids::MACRO_OSC_SHAPE_WAVE_MAP,
            braids::MACRO_OSC_SHAPE_WAVE_LINE,
            braids::MACRO_OSC_SHAPE_WAVE_PARAPHONIC
        },
        {"WTBL", "WMAP", "WLIN", "WTx4"},
        {"POS   TABL", "XPOS  YPOS", "POS   INTP", "POS   CHRD"}
    },
    // Bank 7: NOISE - the manual's "Noise" + TOY* (lo-fi/glitch)
    {
        "NOISE", "NSE", 7,
        {
            braids::MACRO_OSC_SHAPE_FILTERED_NOISE,
            braids::MACRO_OSC_SHAPE_TWIN_PEAKS_NOISE,
            braids::MACRO_OSC_SHAPE_CLOCKED_NOISE,
            braids::MACRO_OSC_SHAPE_GRANULAR_CLOUD,
            braids::MACRO_OSC_SHAPE_PARTICLE_NOISE,
            braids::MACRO_OSC_SHAPE_DIGITAL_MODULATION,
            braids::MACRO_OSC_SHAPE_TOY
        },
        {"NOIS", "TWNQ", "CLKN", "CLOU", "PRTC", "QPSK", "TOY*"},
        {"RESO  LPHP", "RESO  RTIO", "CYCL  QNTZ", "DENS  DISP",
         "DENS  DISP", "RATE  DATA", "SMPL  BITS"}
    }
};

inline float Clamp01(float v)
{
    return std::max(0.0f, std::min(1.0f, v));
}

inline float Clamp11(float v)
{
    return std::max(-1.0f, std::min(1.0f, v));
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
    if(v > 32767)  return 32767;
    if(v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

// AD Envelope
struct AdEnvelope
{
    enum class Stage : uint8_t { Dead, Attack, Sustain, Decay };

    void Init(float sample_rate_hz)
    {
        sample_rate_hz_ = std::max(1.0f, sample_rate_hz);
        dt_ms_ = 1000.0f / sample_rate_hz_;
        stage_ = Stage::Dead;
        level_ = 0.0f;
        attack_ms_ = 10.0f;
        decay_ms_ = 100.0f;
    }

    void SetGate(bool gate)
    {
        const bool rising = gate && !gate_;
        const bool falling = !gate && gate_;
        gate_ = gate;

        if(rising) stage_ = Stage::Attack;
        else if(falling && stage_ != Stage::Dead) stage_ = Stage::Decay;
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
                if(attack_ms_ <= 0.0f) level_ = 1.0f;
                else {
                    level_ += dt_ms_ / attack_ms_;
                    if(level_ >= 1.0f) level_ = 1.0f;
                }
                if(level_ >= 1.0f)
                    stage_ = gate_ ? Stage::Sustain : Stage::Decay;
                break;

            case Stage::Sustain:
                level_ = 1.0f;
                if(!gate_) stage_ = Stage::Decay;
                break;

            case Stage::Decay:
                if(decay_ms_ <= 0.0f) level_ = 0.0f;
                else {
                    level_ -= dt_ms_ / decay_ms_;
                    if(level_ <= 0.0f) level_ = 0.0f;
                }
                if(level_ <= 0.0f) stage_ = Stage::Dead;
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

// Hardware
DaisyPatchSM hw;
Switch       nav_button;  // B7 - navigation button

// Oscillator and envelope
braids::MacroOscillator osc;
AdEnvelope              amp_env;

// OLED
oled::SoftI2C i2c;
oled::SSD1306 display;

// Audio buffers
uint8_t sync_buffer[kBraidsBlockSize];
int16_t render_buffer[kBraidsBlockSize];

// Navigation state
enum class NavMode { Patch, Bank };
NavMode  nav_mode = NavMode::Patch;
int      current_bank = 0;
int      current_patch = 0;
bool     display_dirty = true;

// Persistent settings: last selected bank/patch, stored in QSPI flash.
// Requires APP_TYPE=BOOT_SRAM (app runs from SRAM, so QSPI is writable).
// Bump kSettingsVersion whenever the bank layout changes so a stale saved
// index falls back to defaults instead of landing on the wrong model.
constexpr int      kSettingsVersion    = 1;
constexpr uint32_t kSettingsQspiOffset = 0x7F0000;  // last 64KB of 8MB chip

struct JoySettings
{
    int version;
    int bank;
    int patch;
    bool operator!=(const JoySettings &rhs) const
    {
        return version != rhs.version || bank != rhs.bank
               || patch != rhs.patch;
    }
};

PersistentStorage<JoySettings> settings_storage(hw.qspi);
volatile bool     settings_dirty      = false;
volatile uint32_t settings_dirty_time = 0;
constexpr uint32_t kSettingsSaveDelayMs = 2000;  // debounce to limit wear

// Button timing for long press detection
uint32_t button_press_start = 0;
bool     button_was_pressed = false;
constexpr uint32_t kLongPressMs = 500;

// Gate state (read once per callback)
bool gate1_trig = false;
bool gate2_trig = false;
bool gate1_state = false;

// Control update rate limiting for OLED
size_t   control_ticks = 0;
constexpr size_t kDisplayUpdateTicks = 16; // Update display every N audio blocks

// LED blink for feedback
static constexpr float kPanelLedVoltsOn = 4.0f;
bool led_state = false;
size_t led_off_countdown = 0;

inline void SetPanelLed(bool on)
{
    hw.SetLed(on);
    hw.WriteCvOut(CV_OUT_2, on ? kPanelLedVoltsOn : 0.0f);
}

inline void BlinkLed()
{
    led_state = true;
    SetPanelLed(true);
    led_off_countdown = 8; // ~8 audio blocks = ~4ms
}

void UpdateDisplay()
{
    if(!display_dirty) return;
    
    display.Clear();
    
    if(nav_mode == NavMode::Bank) {
        // BANK MODE: 3x3 grid menu
        // Grid layout: 3 columns x 3 rows
        // Cell width: ~21 pixels (64/3), height: 16 pixels (48/3)
        constexpr uint8_t cellW = 21;
        constexpr uint8_t cellH = 16;
        
        // Row 0: BNK (title, inverted), bank 0, bank 1
        // Row 1: bank 2, bank 3, bank 4
        // Row 2: bank 5, bank 6, bank 7
        
        // Draw "BNK" title in first cell (inverted)
        display.FillRect(0, 0, cellW, cellH);
        display.DrawString(3, 4, "BNK", true);
        
        // Draw bank 0 (ANA) in second cell
        uint8_t x = cellW;
        uint8_t y = 0;
        if(current_bank == 0) {
            display.FillRect(x, y, cellW, cellH);
            display.DrawString(x + 3, y + 4, kBankShortNames[0], true);
        } else {
            display.DrawString(x + 3, y + 4, kBankShortNames[0], false);
        }
        
        // Draw bank 1 (SUB) in third cell
        x = cellW * 2;
        if(current_bank == 1) {
            display.FillRect(x, y, cellW + 1, cellH);
            display.DrawString(x + 3, y + 4, kBankShortNames[1], true);
        } else {
            display.DrawString(x + 3, y + 4, kBankShortNames[1], false);
        }
        
        // Row 1: banks 2, 3, 4
        y = cellH;
        for(int i = 0; i < 3; i++) {
            x = i * cellW;
            int bank = i + 2;
            if(current_bank == bank) {
                display.FillRect(x, y, cellW + (i == 2 ? 1 : 0), cellH);
                display.DrawString(x + 3, y + 4, kBankShortNames[bank], true);
            } else {
                display.DrawString(x + 3, y + 4, kBankShortNames[bank], false);
            }
        }
        
        // Row 2: banks 5, 6, 7
        y = cellH * 2;
        for(int i = 0; i < 3; i++) {
            x = i * cellW;
            int bank = i + 5;
            if(current_bank == bank) {
                display.FillRect(x, y, cellW + (i == 2 ? 1 : 0), cellH);
                display.DrawString(x + 3, y + 4, kBankShortNames[bank], true);
            } else {
                display.DrawString(x + 3, y + 4, kBankShortNames[bank], false);
            }
        }
    } else {
        // PATCH MODE: bank / patch name / divider / knob functions
        display.DrawStringCentered(0, kBanks[current_bank].name, false);
        display.DrawStringCentered(10,
                 kBanks[current_bank].patch_names[current_patch], false);

        // Divider
        display.DrawHLine(0, 19, 64);

        // Knob reminders: per-model TIMBRE/COLOR (from the Braids manual
        // fold-out table), fixed internal AD envelope on knobs 3/4.
        display.DrawString(0, 23,
                 kBanks[current_bank].knob_labels[current_patch], false);
        display.DrawString(0, 33, "ATK   DCY", false);
    }
    
    display.Update();
    display_dirty = false;
}

void SetPatch(int bank, int patch)
{
    current_bank = bank % kBankCount;
    current_patch = patch % kBanks[current_bank].patch_count;

    braids::MacroOscillatorShape shape = kBanks[current_bank].shapes[current_patch];
    osc.set_shape(shape);

    display_dirty = true;
    BlinkLed();

    // Schedule a debounced settings save (performed in the main loop).
    settings_dirty      = true;
    settings_dirty_time = System::GetNow();
}

void ProcessNavigation()
{
    nav_button.Debounce();
    
    uint32_t now = System::GetNow();
    
    if(nav_button.Pressed())
    {
        if(!button_was_pressed)
        {
            // Button just pressed - record start time
            button_press_start = now;
            button_was_pressed = true;
        }
    }
    else
    {
        if(button_was_pressed)
        {
            // Button just released
            uint32_t press_duration = now - button_press_start;
            
            if(press_duration >= kLongPressMs)
            {
                // Long press: toggle navigation mode
                nav_mode = (nav_mode == NavMode::Patch) ? NavMode::Bank : NavMode::Patch;
                display_dirty = true;
                BlinkLed();
            }
            else
            {
                // Short press: cycle within current mode
                if(nav_mode == NavMode::Patch)
                {
                    SetPatch(current_bank,
                             (current_patch + 1) % kBanks[current_bank].patch_count);
                }
                else
                {
                    SetPatch((current_bank + 1) % kBankCount, current_patch);
                }
            }
            
            button_was_pressed = false;
        }
    }
    
    // LED timeout
    if(led_off_countdown > 0)
    {
        led_off_countdown--;
        if(led_off_countdown == 0)
        {
            led_state = false;
            SetPanelLed(false);
        }
    }
}

void ProcessControls()
{
    hw.ProcessAllControls();
    
    ProcessNavigation();
    
    // Envelope from gate
    amp_env.SetGate(gate1_state);
    
    // Strike on trigger
    if(gate1_trig)
    {
        osc.Strike();
    }
    
    // KNOB 1 (CV_1): Timbre
    const float timbre_knob = Clamp01(hw.GetAdcValue(CV_1));
    // KNOB 2 (CV_2): Color
    const float color_knob = Clamp01(hw.GetAdcValue(CV_2));
    
    // CV modulation inputs
    const float timbre_mod = Clamp11(hw.GetAdcValue(CV_6)) * 0.5f;
    const float color_mod = Clamp11(hw.GetAdcValue(CV_7)) * 0.5f;
    
    osc.set_parameters(Float01ToParamQ15(timbre_knob + timbre_mod),
                       Float01ToParamQ15(color_knob + color_mod));
    
    // KNOB 3 (CV_3): Attack
    const float env_attack_norm = Clamp01(hw.GetAdcValue(CV_3));
    // KNOB 4 (CV_4): Decay
    const float env_decay_norm = Clamp01(hw.GetAdcValue(CV_4));
    
    // Wide-range AD envelope (1ms - 6s, exponential response)
    static constexpr float kMaxAttackMs = 6000.0f;
    static constexpr float kMaxDecayMs  = 6000.0f;
    const float attack_ms = 1.0f + (env_attack_norm * env_attack_norm) * kMaxAttackMs;
    const float decay_ms  = 1.0f + (env_decay_norm * env_decay_norm) * kMaxDecayMs;
    amp_env.SetAttackDecayMs(attack_ms, decay_ms);
    
    // V/Oct pitch from CV_5
    const float voct_cv = Clamp11(hw.GetAdcValue(CV_5));
    const float voct_semitones = (voct_cv - kVoctCenterNorm) * 60.73f;   // measured cal: -14.4c/oct slope (was 60.0f)
    const int32_t pitch_q7 = kBaseNoteQ7 + static_cast<int32_t>(SemitonesToQ7(voct_semitones));
    osc.set_pitch(ClampI16(pitch_q7));
    
    // Hard sync from Gate In 2
    std::memset(sync_buffer, 0, sizeof(sync_buffer));
    if(gate2_trig)
        sync_buffer[0] = 1;
    
    // Update display periodically (not every audio block)
    control_ticks++;
    if(control_ticks >= kDisplayUpdateTicks)
    {
        control_ticks = 0;
        UpdateDisplay();
    }
}

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    (void)in;
    
    // Read gate state once per callback
    gate1_state = hw.gate_in_1.State();
    gate1_trig  = hw.gate_in_1.Trig();
    gate2_trig  = hw.gate_in_2.Trig();
    
    // Process in Braids-sized chunks
    size_t i = 0;
    while(i < size)
    {
        const size_t n = std::min(kBraidsBlockSize, size - i);
        
        ProcessControls();
        
        osc.Render(sync_buffer, render_buffer, n);
        
        for(size_t j = 0; j < n; j++)
        {
            const float env_amp = amp_env.Process();
            const float s = static_cast<float>(render_buffer[j]) / 32768.0f;
            const float y = s * env_amp;  // Fixed level (no VCA control)
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
    
    // B7 navigation button
    nav_button.Init(hw.B7,
                    sample_rate,
                    Switch::TYPE_MOMENTARY,
                    Switch::POLARITY_INVERTED);
    
    // DAC for panel LED
    hw.StartDac();
    
    hw.SetAudioBlockSize(kBraidsBlockSize);
    
    // Initialize OLED on soft I2C (A2=SDA, A3=SCL via expansion header)
    i2c.Init(hw.A2, hw.A3);
    display.Init(&i2c);
    display.Clear();
    
    // Splash screen — product name (see README: "two for joy"). The upstream
    // Braids name is credited in the docs, not shown on the panel.
    display.DrawStringLargeCentered(10, "JOY", false);
    display.DrawStringCentered(34, "v1.2", false);
    display.Update();
    System::Delay(800);
    
    // Initialize oscillator
    osc.Init();
    amp_env.Init(sample_rate);
    
    // Restore last selected patch from QSPI (defaults to bank 0, patch 0).
    JoySettings defaults{kSettingsVersion, 0, 0};
    settings_storage.Init(defaults, kSettingsQspiOffset);
    if(settings_storage.GetSettings().version != kSettingsVersion)
        settings_storage.RestoreDefaults();
    SetPatch(settings_storage.GetSettings().bank,
             settings_storage.GetSettings().patch);
    settings_dirty = false;  // restoring is not a change worth saving

    SetPanelLed(false);

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        // Debounced persistence: save the current patch once it has been
        // stable for a while. Runs here (not in the audio callback) because
        // the QSPI erase blocks for tens of ms; the audio interrupt keeps
        // running from SRAM meanwhile.
        if(settings_dirty
           && System::GetNow() - settings_dirty_time > kSettingsSaveDelayMs)
        {
            settings_dirty = false;
            JoySettings &s = settings_storage.GetSettings();
            s.version = kSettingsVersion;
            s.bank    = current_bank;
            s.patch   = current_patch;
            settings_storage.Save();  // no-op if nothing actually changed
        }
        System::Delay(1);
    }
}
