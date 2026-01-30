/**
 * MultiFX OLED
 * 
 * Multi-effect processor for Daisy Patch.Init with OLED display.
 * 
 * Hardware: Daisy Patch SM, 64x48 SSD1306 OLED on soft I2C (A2=SDA, A3=SCL)
 * 
 * EFFECTS:
 *   0: REVERB   - Stereo reverb (ReverbSc)
 *   1: RESO     - Resonator (Rings-inspired modal filter)
 *   2: DLY+REV  - Delay into Reverb chain
 *   3: GRAIN    - Granular pitch shifter
 *
 * CONTROLS:
 *   B7 Short Press: Cycle through effects
 *   B7 Long Press:  Enter/exit effect selection menu
 *   
 *   CV_1..CV_4: Effect-specific parameters (shown on display)
 *   B10: External clock/gate input (for delay sync)
 *   
 *   Audio In L/R -> Processing -> Audio Out L/R
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "oled_soft_i2c.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

namespace {

constexpr int kEffectCount = 4;

// Effect definitions with parameter labels
struct EffectDef {
    const char* name;
    const char* short_name;
    const char* param1;  // CV_1
    const char* param2;  // CV_2
    const char* param3;  // CV_3
    const char* param4;  // CV_4
};

const EffectDef kEffects[kEffectCount] = {
    {"REVERB",   "REV",  "Time", "Damp", "InLv", "Send"},
    {"RESONATR", "RESO", "Freq", "Damp", "InLv", "Mix"},
    {"DLY+REV",  "D+R",  "DTim", "Fdbk", "InLv", "Mix"},
    {"GRANULAR", "GRAN", "Ptch", "Size", "InLv", "Mix"},
};

// Short names for 2x2 grid
const char* kEffectGridNames[kEffectCount] = {
    "REV", "RESO", "D+R", "GRAN"
};

} // namespace

// Hardware
DaisyPatchSM patch;
Switch       nav_button;  // B7

// Effects
ReverbSc     reverb;

// Delay lines (SDRAM)
static constexpr size_t kMaxDelaySamples = 96000; // 2.0s @ 48k
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayL;
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayR;

// Resonator filters
static Svf bp1L, bp2L, bp1R, bp2R;
static float excite_envL = 0.0f, excite_envR = 0.0f;

// Granular buffers (SDRAM)
DSY_SDRAM_BSS static float g_bufferL[24000];
DSY_SDRAM_BSS static float g_bufferR[24000];
static size_t g_bufWrite = 0;
static float  g_grainPhase = 0.0f;
static float  g_grainRate  = 1.0f;
static float  g_grainSizeS = 0.06f;
static float  g_density    = 0.6f;
static uint32_t g_rng_state = 0x12345678u;

// Clock detection for delay sync
static float   g_clk_prev    = 0.0f;
static bool    g_clk_gate    = false;
static uint32_t g_clk_last_ticks = 0;
static float   g_clk_interval_s = 0.0f;

// OLED
oled::SoftI2C i2c;
oled::SSD1306 display;

// Navigation state
enum class NavMode { Effect, Menu };
NavMode  nav_mode = NavMode::Effect;
int      current_effect = 0;
bool     display_dirty = true;

// Button timing for long press
uint32_t button_press_start = 0;
bool     button_was_pressed = false;
constexpr uint32_t kLongPressMs = 500;

// LED
static constexpr float kPanelLedVoltsOn = 2.0f;
bool led_state = false;
size_t led_off_countdown = 0;

// Display update rate limiting
size_t   control_ticks = 0;
constexpr size_t kDisplayUpdateTicks = 16;

inline void SetPanelLed(bool on)
{
    patch.SetLed(on);
    patch.WriteCvOut(CV_OUT_2, on ? kPanelLedVoltsOn : 0.0f);
}

inline void BlinkLed()
{
    led_state = true;
    SetPanelLed(true);
    led_off_countdown = 8;
}

void UpdateDisplay()
{
    if(!display_dirty) return;
    
    display.Clear();
    
    if(nav_mode == NavMode::Menu) {
        // MENU MODE: 2x2 grid
        // Grid layout: 2 columns x 2 rows
        // Cell width: 32 pixels (64/2), height: 24 pixels (48/2)
        constexpr uint8_t cellW = 32;
        constexpr uint8_t cellH = 24;
        
        // Draw 2x2 grid
        for(int row = 0; row < 2; row++) {
            for(int col = 0; col < 2; col++) {
                int effect = row * 2 + col;
                uint8_t x = col * cellW;
                uint8_t y = row * cellH;
                
                if(current_effect == effect) {
                    // Highlight current selection
                    display.FillRect(x, y, cellW, cellH);
                    display.DrawString(x + 5, y + 8, kEffectGridNames[effect], true);
                } else {
                    display.DrawRect(x, y, cellW, cellH);
                    display.DrawString(x + 5, y + 8, kEffectGridNames[effect], false);
                }
            }
        }
    } else {
        // EFFECT MODE: Effect name on top, CV functions below
        
        // Top line: effect name
        display.DrawStringCentered(0, kEffects[current_effect].name, false);
        
        // Divider
        display.DrawHLine(0, 9, 64);
        
        // CV function reminders (effect-specific)
        char line1[16], line2[16];
        snprintf(line1, sizeof(line1), "%-5s %-5s", 
                 kEffects[current_effect].param1,
                 kEffects[current_effect].param2);
        snprintf(line2, sizeof(line2), "%-5s %-5s",
                 kEffects[current_effect].param3,
                 kEffects[current_effect].param4);
        display.DrawString(0, 14, line1, false);
        display.DrawString(0, 24, line2, false);
    }
    
    display.Update();
    display_dirty = false;
}

void SetEffect(int effect)
{
    current_effect = effect % kEffectCount;
    display_dirty = true;
    BlinkLed();
}

void ProcessNavigation()
{
    nav_button.Debounce();
    
    uint32_t now = System::GetNow();
    
    if(nav_button.Pressed())
    {
        if(!button_was_pressed)
        {
            button_press_start = now;
            button_was_pressed = true;
        }
    }
    else
    {
        if(button_was_pressed)
        {
            uint32_t press_duration = now - button_press_start;
            
            if(press_duration >= kLongPressMs)
            {
                // Long press: toggle menu mode
                nav_mode = (nav_mode == NavMode::Effect) ? NavMode::Menu : NavMode::Effect;
                display_dirty = true;
                BlinkLed();
            }
            else
            {
                // Short press: cycle effects
                SetEffect((current_effect + 1) % kEffectCount);
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

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    patch.ProcessAllControls();
    ProcessNavigation();
    
    // Read knobs
    float k1 = patch.GetAdcValue(CV_1);
    float k2 = patch.GetAdcValue(CV_2);
    float k3 = patch.GetAdcValue(CV_3);
    float k4 = patch.GetAdcValue(CV_4);
    
    // Clock detection for delay sync (B10 gate input)
    bool clk_in = patch.gate_in_1.State();
    uint32_t now_ticks = System::GetNow();
    if(!g_clk_gate && clk_in)
    {
        g_clk_gate = true;
        if(g_clk_last_ticks != 0)
        {
            uint32_t dt_ticks = now_ticks - g_clk_last_ticks;
            float tick_hz = (float)System::GetTickFreq();
            if(dt_ticks > 0 && tick_hz > 0.0f)
                g_clk_interval_s = (float)dt_ticks / tick_hz;
        }
        g_clk_last_ticks = now_ticks;
    }
    else if(g_clk_gate && !clk_in)
    {
        g_clk_gate = false;
    }
    
    switch(current_effect)
    {
        case 0: // REVERB
        {
            float time = fmap(k1, 0.3f, 0.99f);
            float damp = fmap(k2, 1000.f, 19000.f, Mapping::LOG);
            float in_level   = k3;
            float send_level = k4;
            reverb.SetFeedback(time);
            reverb.SetLpFreq(damp);
            for(size_t i = 0; i < size; i++)
            {
                float dryl  = IN_L[i] * in_level;
                float dryr  = IN_R[i] * in_level;
                float sendl = IN_L[i] * send_level;
                float sendr = IN_R[i] * send_level;
                float wetl, wetr;
                reverb.Process(sendl, sendr, &wetl, &wetr);
                OUT_L[i] = dryl + wetl;
                OUT_R[i] = dryr + wetr;
            }
        }
        break;

        case 1: // RESONATOR
        {
            float base_freq = fmap(k1, 60.0f, 1200.0f, Mapping::LOG);
            float damping   = fmap(k2, 0.2f, 0.95f);
            float brighten  = fmap(k2, 0.2f, 1.0f);
            float in_level  = k3;
            float wet_mix   = k4;

            bp1L.SetFreq(base_freq);
            bp1L.SetRes(damping);
            bp1R.SetFreq(base_freq);
            bp1R.SetRes(damping);
            bp2L.SetFreq(base_freq * 1.5f);
            bp2L.SetRes(damping * 0.9f);
            bp2R.SetFreq(base_freq * 1.5f);
            bp2R.SetRes(damping * 0.9f);

            const float excite_att = 0.002f;
            const float excite_rel = 0.0008f;

            for(size_t i = 0; i < size; i++)
            {
                float inl = IN_L[i] * in_level;
                float inr = IN_R[i] * in_level;
                float dryl = IN_L[i] * in_level;
                float dryr = IN_R[i] * in_level;
                float magL = fabsf(inl);
                float magR = fabsf(inr);
                excite_envL += (magL - excite_envL) * (magL > excite_envL ? excite_att : excite_rel);
                excite_envR += (magR - excite_envR) * (magR > excite_envR ? excite_att : excite_rel);
                float exciteL = inl + (inl * excite_envL);
                float exciteR = inr + (inr * excite_envR);

                bp1L.Process(exciteL);
                bp2L.Process(exciteL);
                bp1R.Process(exciteR);
                bp2R.Process(exciteR);
                float w1 = 0.6f * (1.0f - 0.7f * brighten);
                float w2 = 0.6f * (0.3f + 0.7f * brighten);
                float wetL = bp1L.Band() * w1 + bp2L.Band() * w2;
                float wetR = bp1R.Band() * w1 + bp2R.Band() * w2;

                OUT_L[i] = dryl * (1.0f - wet_mix) + wetL * wet_mix;
                OUT_R[i] = dryr * (1.0f - wet_mix) + wetR * wet_mix;
            }
        }
        break;

        case 2: // DELAY + REVERB
        {
            float in_level   = k3;
            float wet_mix    = k4;

            bool have_clk = (g_clk_interval_s > 0.0f) && 
                           (System::GetNow() - g_clk_last_ticks < 2u * System::GetTickFreq());
            float d_time_sec = have_clk ? g_clk_interval_s : fmap(k1, 0.02f, 2.0f);
            float d_feedback = fmap(k2, 0.0f, 0.85f);
            size_t sr = (size_t)patch.AudioSampleRate();
            
            static float d2_target = 24000.0f;
            static float d2_smooth = 24000.0f;
            d2_target = fminf(d_time_sec * sr, (float)kMaxDelaySamples - 1);
            d2_smooth += 0.0015f * (d2_target - d2_smooth);
            delayL.SetDelay(d2_smooth);
            delayR.SetDelay(d2_smooth);
            
            reverb.SetFeedback(0.6f);
            reverb.SetLpFreq(8000.0f);

            for(size_t i = 0; i < size; i++)
            {
                float inl = IN_L[i];
                float inr = IN_R[i];
                float dryl = inl * in_level;
                float dryr = inr * in_level;

                float dl_prev_l = delayL.Read();
                float dl_prev_r = delayR.Read();
                static float fb_lp_l2 = 0.0f, fb_lp_r2 = 0.0f;
                const float fb_alpha2 = 0.2f;
                fb_lp_l2 = fb_lp_l2 + fb_alpha2 * (dl_prev_l - fb_lp_l2);
                fb_lp_r2 = fb_lp_r2 + fb_alpha2 * (dl_prev_r - fb_lp_r2);
                delayL.Write(inl + fb_lp_l2 * d_feedback);
                delayR.Write(inr + fb_lp_r2 * d_feedback);

                float wetl, wetr;
                reverb.Process(dl_prev_l, dl_prev_r, &wetl, &wetr);
                OUT_L[i] = dryl * (1.0f - wet_mix) + wetl * wet_mix;
                OUT_R[i] = dryr * (1.0f - wet_mix) + wetr * wet_mix;
            }
        }
        break;

        case 3: // GRANULAR
        {
            float pitch_st   = fmap(k1, -12.0f, 12.0f);
            float semitone   = powf(2.0f, pitch_st / 12.0f);
            float in_level   = k3;
            float wet_mix    = k4;
            float sr         = patch.AudioSampleRate();
            g_grainRate      = semitone;
            g_grainSizeS     = fmap(k2, 0.025f, 0.150f);
            g_density        = fmap(k2, 0.40f, 0.85f);
            size_t grainLen  = (size_t)fminf(g_grainSizeS * sr, (float)24000 - 4);
            if(grainLen < 64) grainLen = 64;

            for(size_t i = 0; i < size; i++)
            {
                float inl = IN_L[i] * in_level;
                float inr = IN_R[i] * in_level;
                g_bufferL[g_bufWrite] = inl;
                g_bufferR[g_bufWrite] = inr;

                float readPos = (float)g_bufWrite - g_grainPhase;
                readPos = fmodf(readPos, 24000.0f);
                if(readPos < 0.0f) readPos += 24000.0f;
                size_t idx0 = (size_t)readPos;
                size_t idx1 = (idx0 + 1) % 24000;
                float frac = readPos - (float)idx0;
                float gl = g_bufferL[idx0] * (1.0f - frac) + g_bufferL[idx1] * frac;
                float gr = g_bufferR[idx0] * (1.0f - frac) + g_bufferR[idx1] * frac;

                float phaseNorm = fmodf(g_grainPhase, (float)grainLen) / (float)grainLen;
                float w = 0.5f * (1.0f - cosf(2.0f * M_PI * phaseNorm));
                float wetl = gl * w;
                float wetr = gr * w;

                g_grainPhase += g_grainRate;
                if(g_grainPhase >= (float)grainLen)
                {
                    g_rng_state = 1664525u * g_rng_state + 1013904223u;
                    float r01 = (float)(g_rng_state & 0x00FFFFFFu) / (float)0x01000000u;
                    float respawn = r01 * g_density * (float)grainLen;
                    if(respawn >= (float)grainLen) respawn = 0.0f;
                    g_grainPhase = respawn;
                }

                OUT_L[i] = IN_L[i] * (1.0f - wet_mix) + wetl * wet_mix;
                OUT_R[i] = IN_R[i] * (1.0f - wet_mix) + wetr * wet_mix;

                g_bufWrite++;
                if(g_bufWrite >= 24000) g_bufWrite = 0;
            }
        }
        break;

        default:
        {
            for(size_t i = 0; i < size; i++)
            {
                OUT_L[i] = IN_L[i] * k3;
                OUT_R[i] = IN_R[i] * k3;
            }
        }
        break;
    }
    
    // Update display periodically
    control_ticks++;
    if(control_ticks >= kDisplayUpdateTicks)
    {
        control_ticks = 0;
        UpdateDisplay();
    }
}

int main(void)
{
    patch.Init();
    
    // B7 navigation button
    nav_button.Init(patch.B7,
                    patch.AudioSampleRate(),
                    Switch::TYPE_MOMENTARY,
                    Switch::POLARITY_INVERTED);
    
    // DAC for panel LED
    patch.StartDac();
    
    // Initialize effects
    reverb.Init(patch.AudioSampleRate());
    delayL.Init();
    delayR.Init();
    bp1L.Init(patch.AudioSampleRate());
    bp2L.Init(patch.AudioSampleRate());
    bp1R.Init(patch.AudioSampleRate());
    bp2R.Init(patch.AudioSampleRate());
    
    // Initialize OLED
    i2c.Init(patch.A2, patch.A3);
    display.Init(&i2c);
    display.Clear();
    
    // Splash screen
    display.DrawStringCentered(0, "MULTIFX", false);
    display.DrawStringLargeCentered(15, "OLED", false);
    display.DrawStringCentered(36, "v1.0", false);
    display.Update();
    System::Delay(800);
    
    // Start with first effect
    SetEffect(0);
    
    SetPanelLed(false);
    
    patch.StartAdc();
    patch.StartAudio(AudioCallback);
    
    while(1)
        System::Delay(1);
}
