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
 *   B7 held at power-up: enter V/Oct calibration (patch 1V, press; 3V, press)
 *
 *   CV_5: V/Oct pitch input (-5V to +5V)
 *   CV_6: Timbre modulation input
 *   CV_7: Color modulation input
 *   
 *   GATE IN 1: Trigger/Gate for envelope. With nothing patched here the AD
 *              never takes over and the oscillator drones, as a stock Braids
 *              does (its AD->VCA depth defaults to off). The first gate edge
 *              hands the VCA to the envelope until the next power cycle.
 *   GATE IN 2: Hard sync
 *
 * Audio runs at 96 kHz to match the rate Braids' DSP and lookup tables were
 * written for — see the note in main().
 *
 * PATCHES: 48 Braids oscillator models organized into 8 thematic banks of 6
 */

#include "daisy_patch_sm.h"
#include "oled_soft_i2c.h"
#include "joy_dsp.h"
#include "voct_cal.h"

#include <braids/macro_oscillator.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace patch_sm;

namespace {

constexpr size_t kBraidsBlockSize = 24;

// V/Oct calibration — these are the compile-time DEFAULTS (also the fallback if
// an end user never runs the calibration routine, and overridable per build via
// -D). At runtime they can be replaced by a stored two-point calibration
// (hold B7 at power-up); see RunCalibration() and g_voct_cal.
#ifndef VOCT_BASE_MIDI
#define VOCT_BASE_MIDI 48
#endif

#ifndef VOCT_CENTER_NORM
#define VOCT_CENTER_NORM 0.007074f   // measured cal: nulls intercept (0.002133f left C1-C4 +30c; +0.30 st / 0.30/60.73)
#endif

#ifndef VOCT_SLOPE
#define VOCT_SLOPE 60.73f            // measured cal: -14.4 c/oct correction (was 60.0f)
#endif

constexpr int32_t kBaseNoteQ7 = (static_cast<int32_t>(VOCT_BASE_MIDI) << 7);
constexpr float   kVoctCenterNorm = static_cast<float>(VOCT_CENTER_NORM);
constexpr float   kVoctSlope      = static_cast<float>(VOCT_SLOPE);

// Active calibration, loaded from persistent settings at boot (defaults above).
joy::VoctCal g_voct_cal = {kVoctCenterNorm, kVoctSlope};

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

// The clamp/fixed-point helpers and the AD envelope live in common/joy_dsp.h so
// Joy and Joy Lite share one implementation (this file used to carry its own
// copy). Pulled in here by name so the call sites below read unchanged.
using joy::AdEnvelope;
using joy::Clamp01;
using joy::Clamp11;
using joy::ClampI16;
using joy::Float01ToParamQ15;
using joy::SemitonesToQ7;

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

// Navigation state. Owned by the audio callback (ProcessNavigation runs there);
// the main loop only ever sees it through display_state.
enum class NavMode { Patch, Bank };
NavMode  nav_mode = NavMode::Patch;
int      current_bank = 0;
int      current_patch = 0;

// Everything the screen needs, packed into one word so the main loop reads a
// consistent bank/patch/mode triple without locking: a plain 32-bit load can't
// tear, whereas three separate reads could catch a half-applied patch change.
// Written only by PublishDisplayState() (audio callback side).
volatile uint32_t display_state = 0;
volatile bool     display_dirty = true;

inline void PublishDisplayState()
{
    display_state = (static_cast<uint32_t>(nav_mode) << 16)
                    | (static_cast<uint32_t>(current_bank & 0xFF) << 8)
                    | static_cast<uint32_t>(current_patch & 0xFF);
    display_dirty = true;
}

// Persistent settings: last selected bank/patch, stored in QSPI flash.
// Requires APP_TYPE=BOOT_SRAM (app runs from SRAM, so QSPI is writable).
// Bump kSettingsVersion whenever the bank layout changes so a stale saved
// index falls back to defaults instead of landing on the wrong model.
constexpr int      kSettingsVersion    = 2;  // v2: added V/Oct calibration
constexpr uint32_t kSettingsQspiOffset = 0x7F0000;  // last 64KB of 8MB chip

struct JoySettings
{
    int   version;
    int   bank;
    int   patch;
    float voct_center; // stored 1V/oct calibration (see voct_cal.h)
    float voct_slope;
    bool operator!=(const JoySettings &rhs) const
    {
        return version != rhs.version || bank != rhs.bank
               || patch != rhs.patch || voct_center != rhs.voct_center
               || voct_slope != rhs.voct_slope;
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
    // Counted in audio blocks, so it tracks the callback rate: 16 blocks of 24
    // samples is ~4 ms at 96 kHz (it was 8 blocks for the same 4 ms at 48 kHz).
    led_off_countdown = 16;
}

// Renders and pushes a frame. Runs in the main loop, never in the audio
// callback: pushing 64x48 pixels over bit-banged I2C takes far longer than one
// audio block, so doing it here keeps the render interrupt free of a multi-
// millisecond stall on every patch change.
void UpdateDisplay()
{
    if(!display_dirty) return;

    // Clear before rendering, not after: a navigation change that lands while
    // the frame is being pushed then re-arms the flag and gets its own frame,
    // instead of being swallowed.
    display_dirty = false;

    const uint32_t snapshot = display_state;
    const NavMode  cur_mode = static_cast<NavMode>((snapshot >> 16) & 0xFF);
    const int      cur_bank = static_cast<int>((snapshot >> 8) & 0xFF);
    const int      cur_patch = static_cast<int>(snapshot & 0xFF);

    display.Clear();

    if(cur_mode == NavMode::Bank) {
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
        if(cur_bank == 0) {
            display.FillRect(x, y, cellW, cellH);
            display.DrawString(x + 3, y + 4, kBankShortNames[0], true);
        } else {
            display.DrawString(x + 3, y + 4, kBankShortNames[0], false);
        }
        
        // Draw bank 1 (SUB) in third cell
        x = cellW * 2;
        if(cur_bank == 1) {
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
            if(cur_bank == bank) {
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
            if(cur_bank == bank) {
                display.FillRect(x, y, cellW + (i == 2 ? 1 : 0), cellH);
                display.DrawString(x + 3, y + 4, kBankShortNames[bank], true);
            } else {
                display.DrawString(x + 3, y + 4, kBankShortNames[bank], false);
            }
        }
    } else {
        // PATCH MODE: bank / patch name / divider / knob functions
        display.DrawStringCentered(0, kBanks[cur_bank].name, false);
        display.DrawStringCentered(10,
                 kBanks[cur_bank].patch_names[cur_patch], false);

        // Divider
        display.DrawHLine(0, 19, 64);

        // Knob reminders: per-model TIMBRE/COLOR (from the Braids manual
        // fold-out table), fixed internal AD envelope on knobs 3/4.
        display.DrawString(0, 23,
                 kBanks[cur_bank].knob_labels[cur_patch], false);
        display.DrawString(0, 33, "ATK   DCY", false);
    }

    display.Update();
}

void SetPatch(int bank, int patch)
{
    current_bank = bank % kBankCount;
    current_patch = patch % kBanks[current_bank].patch_count;

    braids::MacroOscillatorShape shape = kBanks[current_bank].shapes[current_patch];
    osc.set_shape(shape);

    PublishDisplayState();
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
                PublishDisplayState();
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
    
    // V/Oct pitch from CV_5 (runtime calibration; see g_voct_cal).
    const float voct_cv = Clamp11(hw.GetAdcValue(CV_5));
    const float voct_semitones = joy::VoctToSemitones(g_voct_cal, voct_cv);
    const int32_t pitch_q7 = kBaseNoteQ7 + static_cast<int32_t>(SemitonesToQ7(voct_semitones));
    osc.set_pitch(ClampI16(pitch_q7));
    
    // Hard sync from Gate In 2
    std::memset(sync_buffer, 0, sizeof(sync_buffer));
    if(gate2_trig)
        sync_buffer[0] = 1;
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

// Wait for the button to be released and then pressed again (a fresh press),
// debouncing continuously. Used to step through the calibration prompts.
void WaitForFreshPress()
{
    do {
        hw.ProcessAllControls();
        nav_button.Debounce();
        System::Delay(1);
    } while(nav_button.Pressed());
    do {
        hw.ProcessAllControls();
        nav_button.Debounce();
        System::Delay(1);
    } while(!nav_button.Pressed());
}

// Average the CV_5 reading over a short window for a stable capture.
float CaptureVoct()
{
    constexpr int kN = 200; // ~200 ms
    float acc = 0.0f;
    for(int i = 0; i < kN; i++)
    {
        hw.ProcessAllControls();
        acc += Clamp11(hw.GetAdcValue(CV_5));
        System::Delay(1);
    }
    return acc / static_cast<float>(kN);
}

// Two-point 1V/oct calibration (entered by holding B7 at power-up). Requires
// StartAdc() already running. Prompts on the OLED, captures 1 V then 3 V, stores
// the result to persistent settings and updates g_voct_cal. A bad capture keeps
// the previous/default calibration (ComputeVoctCal falls back).
void RunCalibration()
{
    auto prompt = [](const char* volts) {
        display.Clear();
        display.DrawStringCentered(0, "CALIBRATE", false);
        display.DrawStringLargeCentered(15, volts, false);
        display.DrawStringCentered(38, "patch+B7", false);
        display.Update();
    };

    prompt("1V");
    WaitForFreshPress();
    const float adc_1v = CaptureVoct();

    prompt("3V");
    WaitForFreshPress();
    const float adc_3v = CaptureVoct();

    g_voct_cal = joy::ComputeVoctCal(adc_1v, adc_3v, g_voct_cal);

    JoySettings &s = settings_storage.GetSettings();
    s.voct_center  = g_voct_cal.center;
    s.voct_slope   = g_voct_cal.slope;
    settings_storage.Save();

    display.Clear();
    display.DrawStringLargeCentered(15, "DONE", false);
    display.Update();
    System::Delay(900);
}

int main(void)
{
    hw.Init();

    // Braids is a 96 kHz machine: braids.cc runs its timer at F_CPU/96000, and
    // the oscillator increment/delay tables in resources.cc are generated for
    // that rate (braids/resources/lookup_tables.py). Running the same DSP at
    // libDaisy's 48 kHz default consumed those per-sample increments at half
    // speed, which put every model an octave above its nominal pitch, halved
    // the effective anti-aliasing headroom and ran every table-derived time
    // constant (comb delays, resonators, drum decays, grain rates) twice as
    // fast. Matching Braids' rate is what makes the vendored DSP behave as
    // written; it also puts the control rate at Braids' own 96000/24 = 4 kHz.
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_96KHZ);
    hw.SetAudioBlockSize(kBraidsBlockSize);

    const float sample_rate = hw.AudioSampleRate();

    // B7 navigation button. Debounce() is called once per audio block from
    // ProcessNavigation, so the switch wants the callback rate, not the sample
    // rate — passing the latter made the debounce window 24x shorter than
    // intended (and would have shifted again with the rate change above).
    nav_button.Init(hw.B7,
                    hw.AudioCallbackRate(),
                    Switch::TYPE_MOMENTARY,
                    Switch::POLARITY_INVERTED);

    // DAC for panel LED
    hw.StartDac();

    // Initialize OLED on soft I2C (A2=SDA, A3=SCL via expansion header)
    i2c.Init(hw.A2, hw.A3);
    display.Init(&i2c);
    display.Clear();
    
    // Splash screen — product name (see README: "two for joy"). The upstream
    // Braids name is credited in the docs, not shown on the panel.
    display.DrawStringLargeCentered(10, "JOY", false);
    display.DrawStringCentered(34, "v1.4", false);
    display.Update();
    System::Delay(800);
    
    // Initialize oscillator
    osc.Init();
    amp_env.Init(sample_rate);
    
    // Restore last selected patch + calibration from QSPI. Defaults fall back to
    // the compile-time calibration constants.
    JoySettings defaults{kSettingsVersion, 0, 0, kVoctCenterNorm, kVoctSlope};
    settings_storage.Init(defaults, kSettingsQspiOffset);
    if(settings_storage.GetSettings().version != kSettingsVersion)
        settings_storage.RestoreDefaults();
    const JoySettings &saved = settings_storage.GetSettings();
    SetPatch(saved.bank, saved.patch);
    g_voct_cal = {saved.voct_center, saved.voct_slope};
    settings_dirty = false;  // restoring is not a change worth saving

    SetPanelLed(false);

    hw.StartAdc();

    // Hold B7 at power-up to enter V/Oct calibration.
    for(int i = 0; i < 40; i++) { hw.ProcessAllControls(); nav_button.Debounce(); System::Delay(1); }
    if(nav_button.Pressed())
        RunCalibration();

    hw.StartAudio(AudioCallback);

    while(1)
    {
        // Screen redraws happen here rather than in the audio callback: the
        // soft-I2C frame push blocks for milliseconds, which at 96 kHz is
        // hundreds of audio blocks' worth of render interrupt.
        UpdateDisplay();

        // Debounced persistence: save the current patch once it has been
        // stable for a while. Runs here (not in the audio callback) because
        // the QSPI erase blocks for tens of ms; the audio interrupt keeps
        // running from SRAM meanwhile.
        if(settings_dirty
           && System::GetNow() - settings_dirty_time > kSettingsSaveDelayMs)
        {
            settings_dirty = false;
            // Same snapshot the screen uses, for the same reason: bank and
            // patch are written together by the audio callback.
            const uint32_t snapshot = display_state;
            JoySettings &s = settings_storage.GetSettings();
            s.version = kSettingsVersion;
            s.bank    = static_cast<int>((snapshot >> 8) & 0xFF);
            s.patch   = static_cast<int>(snapshot & 0xFF);
            settings_storage.Save();  // no-op if nothing actually changed
        }
        System::Delay(1);
    }
}
