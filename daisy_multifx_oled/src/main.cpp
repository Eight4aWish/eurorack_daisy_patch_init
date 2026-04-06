/**
 * MultiFX OLED — 8 patches
 *
 * Hardware: Daisy Patch SM, 64x48 SSD1306 OLED on soft I2C (A2=SDA, A3=SCL)
 * Navigation: B7 short press = cycle patch, long press = 4x2 menu grid
 * Clock sync: gate_in_1 (B10) syncs delay time in patch 2
 *
 * Patches:
 *   0  REVERB    – ReverbSc send topology
 *   1  RESONATR  – 2-partial SVF modal resonator
 *   2  DLY+REV   – Delay into reverb (clock-syncable)
 *   3  GRANULAR  – Dual-grain overlap-add pitch shifter
 *   4  LADDER    – Moog 4-pole ladder filter (LP24)
 *   5  SVF MRF   – SVF LP→BP→HP→Notch morph
 *   6  COMB      – Comb filter / Karplus-Strong
 *   7  WF+CHR    – Wavefolder into stereo chorus
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "oled_soft_i2c.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// ================================================================
// Constants
// ================================================================
static constexpr int    kNumPatches      = 8;
static constexpr size_t kMaxDelaySamples = 96000; // 2.0 s @ 48 k
static constexpr size_t kGrainBufSize    = 24000; // ~0.5 s @ 48 k
static constexpr float  kTwoPi           = 6.2831853f;

// ================================================================
// Effect metadata (name shown in effect view, 2-char label for menu)
// ================================================================
struct EffectDef {
    const char* name;
    const char* menu_label; // 2 chars — fits 16 px cell (5x7 font)
    const char* p1; const char* p2; const char* p3; const char* p4;
};

static const EffectDef kEffects[kNumPatches] = {
    {"REVERB",   "RV", "Time", "Damp", "InLv", "Send"},
    {"RESONATR", "RS", "Freq", "Damp", "InLv", "Mix "},
    {"DLY+REV",  "DR", "DTim", "Fdbk", "DWet", "RWet"},
    {"PITCH",    "PT", "Semi", "Size", "Fun ", "Mix "},
    {"LADDER",   "LD", "Cut ", "Res ", "Drv ", "Mix "},
    {"SVF MRF",  "SF", "Cut ", "Res ", "Typ ", "Mix "},
    {"COMB",     "CB", "Freq", "Fdbk", "Brt ", "Mix "},
    {"WF+CHR",   "WF", "Fold", "Rate", "Dpt ", "Mix "},
};

// ================================================================
// Hardware
// ================================================================
DaisyPatchSM patch;
Switch       nav_btn; // B7 only — B8 removed for OLED

// ================================================================
// Shared FX
// ================================================================
ReverbSc reverb;
using daisysp::DelayLine;
using daisysp::Svf;
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayL;
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayR;

// Patch 1 – Resonator
static Svf   bp1L, bp2L, bp1R, bp2R;
static float excite_envL = 0.0f, excite_envR = 0.0f;

// Patch 2 – Delay+Reverb (file-scope to avoid static-in-switch)
static float fb_lp_l2  = 0.0f, fb_lp_r2  = 0.0f;
static float d2_target = 24000.0f, d2_smooth = 24000.0f;

// Patch 3 – Pitch shifter (time-domain OLA, two crossfading delay lines)
// Large internal buffers placed in SDRAM to avoid exhausting SRAM
DSY_SDRAM_BSS static PitchShifter pitch_l, pitch_r;

// Patch 4 – Moog ladder filter
static LadderFilter ladder_l, ladder_r;

// Patch 5 – SVF multi-mode morph
static Svf svf_l, svf_r;

// Patch 6 – Comb filter (reuses delayL/R)
static float comb_lp_l = 0.0f, comb_lp_r = 0.0f;

// Patch 7 – Wavefolder + stereo Chorus
static Wavefolder wfold_l, wfold_r;
static Chorus     chorus;

// Clock detection (gate_in_1 = B10 jack)
static bool     g_clk_gate       = false;
static uint32_t g_clk_last_ticks = 0;
static float    g_clk_interval_s = 0.0f;

// ================================================================
// OLED
// ================================================================
oled::SSD1306 display;

// ================================================================
// Navigation state
// ================================================================
enum class NavMode { Effect, Menu };
static NavMode  nav_mode        = NavMode::Effect;
static int      current_patch   = 0;
static bool     display_dirty   = true;
static uint32_t btn_press_start = 0;
static bool     btn_held        = false;
static size_t   ctrl_ticks      = 0;
static constexpr size_t   kDisplayUpdateInterval = 16;
static constexpr uint32_t kLongPressMs           = 500;

// LED (panel LED via CV_OUT_2)
static bool   led_on           = false;
static size_t led_off_ticks    = 0;
static constexpr float kLedVolts = 2.0f;

static inline void SetLed(bool on)
{
    patch.WriteCvOut(CV_OUT_2, on ? kLedVolts : 0.0f);
}

static inline void BlinkLed()
{
    led_on = true;
    SetLed(true);
    led_off_ticks = 10;
}

// ================================================================
// Display rendering
// ================================================================
static void UpdateDisplay()
{
    if(!display_dirty) return;
    display.Clear();

    if(nav_mode == NavMode::Menu)
    {
        // 4x2 grid: 16 px wide × 24 px tall per cell
        for(int row = 0; row < 2; row++)
        {
            for(int col = 0; col < 4; col++)
            {
                int     idx = row * 4 + col;
                uint8_t x   = (uint8_t)(col * 16);
                uint8_t y   = (uint8_t)(row * 24);
                bool    sel = (idx == current_patch);
                if(sel)
                    display.FillRect(x, y, 16, 24);
                else
                    display.DrawRect(x, y, 16, 24);
                display.DrawString(x + 2, y + 8, kEffects[idx].menu_label, sel);
            }
        }
    }
    else
    {
        // Effect view: name, divider, param grid
        display.DrawStringCentered(0, kEffects[current_patch].name, false);
        display.DrawHLine(0, 9, 64);
        char row1[14], row2[14];
        snprintf(row1, sizeof(row1), "%-5s%-5s",
                 kEffects[current_patch].p1, kEffects[current_patch].p2);
        snprintf(row2, sizeof(row2), "%-5s%-5s",
                 kEffects[current_patch].p3, kEffects[current_patch].p4);
        display.DrawString(0, 14, row1, false);
        display.DrawString(0, 24, row2, false);
    }

    display.Update();
    display_dirty = false;
}

// ================================================================
// Navigation (called once per audio block)
// ================================================================
static void ProcessNav()
{
    nav_btn.Debounce();
    uint32_t now = System::GetNow();

    if(nav_btn.Pressed())
    {
        if(!btn_held)
        {
            btn_press_start = now;
            btn_held = true;
        }
    }
    else if(btn_held)
    {
        uint32_t dur = now - btn_press_start;
        if(dur >= kLongPressMs)
        {
            nav_mode = (nav_mode == NavMode::Effect) ? NavMode::Menu : NavMode::Effect;
            display_dirty = true;
        }
        else if(nav_mode == NavMode::Menu)
        {
            // Short press in menu: select highlighted patch and exit
            nav_mode = NavMode::Effect;
            display_dirty = true;
        }
        else
        {
            // Short press in effect view: cycle to next patch
            current_patch = (current_patch + 1) % kNumPatches;
            display_dirty = true;
        }
        BlinkLed();
        btn_held = false;
    }

    // LED timeout
    if(led_on && led_off_ticks > 0)
    {
        if(--led_off_ticks == 0)
        {
            led_on = false;
            SetLed(false);
        }
    }
}

// ================================================================
// Audio callback
// ================================================================
void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    patch.ProcessAllControls();
    ProcessNav();

    // Pot + CV jack summed and clamped — standard Eurorack attenuverter behaviour
    float k1 = fclamp(patch.GetAdcValue(CV_1) + patch.GetAdcValue(CV_5), 0.f, 1.f);
    float k2 = fclamp(patch.GetAdcValue(CV_2) + patch.GetAdcValue(CV_6), 0.f, 1.f);
    float k3 = fclamp(patch.GetAdcValue(CV_3) + patch.GetAdcValue(CV_7), 0.f, 1.f);
    float k4 = fclamp(patch.GetAdcValue(CV_4) + patch.GetAdcValue(CV_8), 0.f, 1.f);

    // Clock detection via dedicated gate jack (B10)
    bool     clk_in    = patch.gate_in_1.State();
    uint32_t now_ticks = System::GetNow();
    if(!g_clk_gate && clk_in)
    {
        g_clk_gate = true;
        if(g_clk_last_ticks != 0)
        {
            uint32_t dt_ms = now_ticks - g_clk_last_ticks;
            if(dt_ms > 0)
                g_clk_interval_s = (float)dt_ms * 0.001f;
        }
        g_clk_last_ticks = now_ticks;
    }
    else if(g_clk_gate && !clk_in)
    {
        g_clk_gate = false;
    }

    switch(current_patch)
    {
        // ============================================================
        case 0: // REVERB – CV1=decay CV2=damp CV3=dry CV4=send
        // ============================================================
        {
            reverb.SetFeedback(fmap(k1, 0.3f, 0.99f));
            reverb.SetLpFreq(fmap(k2, 1000.f, 19000.f, Mapping::LOG));
            float in_level   = k3;
            float send_level = k4;
            for(size_t i = 0; i < size; i++)
            {
                float wetl, wetr;
                reverb.Process(IN_L[i] * send_level, IN_R[i] * send_level,
                               &wetl, &wetr);
                OUT_L[i] = IN_L[i] * in_level + wetl;
                OUT_R[i] = IN_R[i] * in_level + wetr;
            }
        }
        break;

        // ============================================================
        case 1: // RESONATOR – CV1=freq CV2=damp+bright CV3=in CV4=wet
        // ============================================================
        {
            float base_freq = fmap(k1, 60.0f, 1200.0f, Mapping::LOG);
            float damping   = fmap(k2, 0.2f, 0.95f);
            float brighten  = fmap(k2, 0.2f, 1.0f);
            float in_level  = k3;
            float wet_mix   = k4;
            const float w1  = 0.6f * (1.0f - 0.7f * brighten);
            const float w2  = 0.6f * (0.3f + 0.7f * brighten);

            bp1L.SetFreq(base_freq);        bp1L.SetRes(damping);
            bp1R.SetFreq(base_freq);        bp1R.SetRes(damping);
            bp2L.SetFreq(base_freq * 1.5f); bp2L.SetRes(damping * 0.9f);
            bp2R.SetFreq(base_freq * 1.5f); bp2R.SetRes(damping * 0.9f);

            const float att = 0.002f, rel = 0.0008f;
            for(size_t i = 0; i < size; i++)
            {
                float inl  = IN_L[i] * in_level;
                float inr  = IN_R[i] * in_level;
                float magL = fabsf(inl), magR = fabsf(inr);
                excite_envL += (magL - excite_envL) * (magL > excite_envL ? att : rel);
                excite_envR += (magR - excite_envR) * (magR > excite_envR ? att : rel);
                bp1L.Process(inl * (1.0f + excite_envL));
                bp2L.Process(inl * (1.0f + excite_envL));
                bp1R.Process(inr * (1.0f + excite_envR));
                bp2R.Process(inr * (1.0f + excite_envR));
                float wetL = bp1L.Band() * w1 + bp2L.Band() * w2;
                float wetR = bp1R.Band() * w1 + bp2R.Band() * w2;
                OUT_L[i] = inl * (1.0f - wet_mix) + wetL * wet_mix;
                OUT_R[i] = inr * (1.0f - wet_mix) + wetR * wet_mix;
            }
        }
        break;

        // ============================================================
        case 2: // DLY+REV – CV1=time CV2=fdbk CV3=delay wet CV4=reverb wet
        //        Delay and reverb independently mixable. Dry always passes.
        //        gate_in_1 syncs delay time when clock present < 2 s.
        // ============================================================
        {
            bool  have_clk   = (g_clk_interval_s > 0.0f)
                            && (now_ticks - g_clk_last_ticks < 2000u);
            float d_time_sec = have_clk
                ? g_clk_interval_s
                : fmap(k1, 0.02f, 2.0f);
            float d_feedback = fmap(k2, 0.0f, 0.85f);
            float delay_wet  = k3; // how much delay repeat you hear
            float reverb_wet = k4; // how much reverb on top
            float sr         = patch.AudioSampleRate();

            d2_target  = fminf(d_time_sec * sr, (float)kMaxDelaySamples - 1.0f);
            d2_smooth += 0.0015f * (d2_target - d2_smooth);
            delayL.SetDelay(d2_smooth);
            delayR.SetDelay(d2_smooth);
            reverb.SetFeedback(0.72f);
            reverb.SetLpFreq(6000.0f);

            const float fb_alpha = 0.55f;
            for(size_t i = 0; i < size; i++)
            {
                float dl = delayL.Read();
                float dr = delayR.Read();
                fb_lp_l2 += fb_alpha * (dl - fb_lp_l2);
                fb_lp_r2 += fb_alpha * (dr - fb_lp_r2);
                delayL.Write(IN_L[i] + fb_lp_l2 * d_feedback);
                delayR.Write(IN_R[i] + fb_lp_r2 * d_feedback);
                // Feed dry + delay into reverb so both contribute to the tail
                float rev_inl = IN_L[i] + dl * delay_wet;
                float rev_inr = IN_R[i] + dr * delay_wet;
                float wetl, wetr;
                reverb.Process(rev_inl * reverb_wet, rev_inr * reverb_wet,
                               &wetl, &wetr);
                OUT_L[i] = IN_L[i] + dl * delay_wet + wetl;
                OUT_R[i] = IN_R[i] + dr * delay_wet + wetr;
            }
        }
        break;

        // ============================================================
        case 3: // PITCH – CV1=semitones CV2=buf size CV3=flutter CV4=wet
        //        DaisySP time-domain OLA pitch shifter (two crossfading delay lines)
        // ============================================================
        {
            // Quantise to integer semitones (-12..+12)
            float semitones = floorf(fmap(k1, -12.0f, 13.0f));
            // Buffer size: larger = smoother shift, more latency (1024..16384)
            uint32_t buf_sz = (uint32_t)fmap(k2, 1024.0f, 16384.0f);
            float    fun    = k3; // tape-flutter randomness 0..1
            float    wet    = k4;
            pitch_l.SetTransposition(semitones);
            pitch_r.SetTransposition(semitones);
            pitch_l.SetDelSize(buf_sz);
            pitch_r.SetDelSize(buf_sz);
            pitch_l.SetFun(fun);
            pitch_r.SetFun(fun);
            for(size_t i = 0; i < size; i++)
            {
                float inl = IN_L[i], inr = IN_R[i];
                float pl  = pitch_l.Process(inl);
                float pr  = pitch_r.Process(inr);
                OUT_L[i]  = inl * (1.0f - wet) + pl * wet;
                OUT_R[i]  = inr * (1.0f - wet) + pr * wet;
            }
        }
        break;

        // ============================================================
        case 4: // LADDER – CV1=cut CV2=res CV3=drive CV4=wet
        //        Huovilainen 4-pole LP24, 4x oversampled, self-oscillates
        // ============================================================
        {
            float cutoff = fmap(k1, 20.0f, 18000.0f, Mapping::LOG);
            float res    = fmap(k2, 0.0f, 1.8f);
            float drive  = fmap(k3, 0.0f, 4.0f);
            float wet    = k4;
            ladder_l.SetFreq(cutoff); ladder_l.SetRes(res); ladder_l.SetInputDrive(drive);
            ladder_r.SetFreq(cutoff); ladder_r.SetRes(res); ladder_r.SetInputDrive(drive);
            for(size_t i = 0; i < size; i++)
            {
                OUT_L[i] = IN_L[i] * (1.0f - wet) + ladder_l.Process(IN_L[i]) * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + ladder_r.Process(IN_R[i]) * wet;
            }
        }
        break;

        // ============================================================
        case 5: // SVF MORPH – CV1=cut CV2=res CV3=LP→BP→HP→Notch CV4=wet
        // ============================================================
        {
            float cutoff = fmap(k1, 20.0f, 16000.0f, Mapping::LOG);
            float res    = fmap(k2, 0.0f, 1.0f);
            float morph  = k3;
            float wet    = k4;
            svf_l.SetFreq(cutoff); svf_l.SetRes(res);
            svf_r.SetFreq(cutoff); svf_r.SetRes(res);
            for(size_t i = 0; i < size; i++)
            {
                svf_l.Process(IN_L[i]);
                svf_r.Process(IN_R[i]);
                float fl, fr;
                if(morph < 0.333f)
                {
                    float t = morph / 0.333f;
                    fl = svf_l.Low()  * (1.0f - t) + svf_l.Band()  * t;
                    fr = svf_r.Low()  * (1.0f - t) + svf_r.Band()  * t;
                }
                else if(morph < 0.667f)
                {
                    float t = (morph - 0.333f) / 0.334f;
                    fl = svf_l.Band() * (1.0f - t) + svf_l.High()  * t;
                    fr = svf_r.Band() * (1.0f - t) + svf_r.High()  * t;
                }
                else
                {
                    float t = (morph - 0.667f) / 0.333f;
                    fl = svf_l.High() * (1.0f - t) + svf_l.Notch() * t;
                    fr = svf_r.High() * (1.0f - t) + svf_r.Notch() * t;
                }
                OUT_L[i] = IN_L[i] * (1.0f - wet) + fl * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + fr * wet;
            }
        }
        break;

        // ============================================================
        case 6: // COMB – CV1=freq CV2=feedback CV3=brightness CV4=wet
        // ============================================================
        {
            float freq     = fmap(k1, 40.0f, 1200.0f, Mapping::LOG);
            float feedback = fmap(k2, 0.0f, 0.99f);
            float bright   = k3;
            float wet      = k4;
            float delay_s  = fminf(patch.AudioSampleRate() / freq,
                                   (float)kMaxDelaySamples - 1.0f);
            delayL.SetDelay(delay_s);
            delayR.SetDelay(delay_s);
            float lp_alpha = fmap(bright, 0.08f, 0.92f);
            for(size_t i = 0; i < size; i++)
            {
                float dl = delayL.Read();
                float dr = delayR.Read();
                comb_lp_l += lp_alpha * (dl - comb_lp_l);
                comb_lp_r += lp_alpha * (dr - comb_lp_r);
                delayL.Write(IN_L[i] + comb_lp_l * feedback);
                delayR.Write(IN_R[i] + comb_lp_r * feedback);
                OUT_L[i] = IN_L[i] * (1.0f - wet) + dl * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + dr * wet;
            }
        }
        break;

        // ============================================================
        case 7: // WF+CHR – CV1=fold CV2=chorus rate CV3=depth CV4=wet
        // ============================================================
        {
            float fold  = fmap(k1, 1.0f, 8.0f);
            float wet   = k4;
            wfold_l.SetGain(fold);
            wfold_r.SetGain(fold);
            chorus.SetLfoFreq(fmap(k2, 0.1f, 5.0f));
            chorus.SetLfoDepth(k3);
            for(size_t i = 0; i < size; i++)
            {
                float fl = wfold_l.Process(IN_L[i]);
                float fr = wfold_r.Process(IN_R[i]);
                chorus.Process((fl + fr) * 0.5f);
                OUT_L[i] = IN_L[i] * (1.0f - wet) + chorus.GetLeft()  * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + chorus.GetRight() * wet;
            }
        }
        break;

        default:
        {
            for(size_t i = 0; i < size; i++)
            {
                OUT_L[i] = IN_L[i];
                OUT_R[i] = IN_R[i];
            }
        }
        break;
    }

    // Update display at reduced rate to avoid blocking audio
    if(++ctrl_ticks >= kDisplayUpdateInterval)
    {
        ctrl_ticks = 0;
        UpdateDisplay();
    }
}

// ================================================================
// main
// ================================================================
int main(void)
{
    patch.Init();
    float sr = patch.AudioSampleRate();

    nav_btn.Init(patch.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);

    patch.StartDac();

    // Effects
    reverb.Init(sr);
    delayL.Init();
    delayR.Init();
    bp1L.Init(sr); bp2L.Init(sr);
    bp1R.Init(sr); bp2R.Init(sr);
    pitch_l.Init(sr);  pitch_r.Init(sr);
    ladder_l.Init(sr); ladder_r.Init(sr);
    svf_l.Init(sr);    svf_r.Init(sr);
    wfold_l.Init();    wfold_r.Init();
    chorus.Init(sr);
    chorus.SetDelay(0.4f);
    chorus.SetFeedback(0.1f);

    // OLED
    display.Init(patch.A2, patch.A3);
    display.Clear();
    display.DrawStringCentered(0,  "MULTIFX", false);
    display.DrawStringCentered(16, "8 PATCH", false);
    display.Update();
    System::Delay(800);

    display_dirty = true;
    SetLed(false);

    patch.StartAdc();
    patch.StartAudio(AudioCallback);
    while(1) { System::Delay(1); }
}
