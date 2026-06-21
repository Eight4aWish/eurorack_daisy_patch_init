/**
 * MultiFX — Daisy Seed (homebrew Eurorack), 4 banks, unified multifx-core.
 *
 * Sibling of daisy_multifx_oled (Patch.Init): same shared DSP/UI core, a different
 * hardware shell. This board is a bare Daisy Seed with a home-built front end:
 * 3 pots + 2 CV jacks + 1 button, and a 128x64 yellow/blue SSD1306 on hardware I2C.
 *
 * Banks (mfx::NavModel, 4 banks of 4):
 *   A REVERB  Classic / Plate / Tank / Shimmer        (mfx::ReverbBank)
 *   B DELAY   Ping / Tape / MultiTap / EchoVerb        (mfx::DelayBank)
 *   C TONE    Ladder / SVF morph / Comb / WF+Chorus    (mfx::ToneBank)
 *   D MISC    Resonator / Pitch / Drive / Crush        (mfx::MiscBank)
 *
 * Control mapping (the Seed has only 2 free pots after Mix, vs Patch.Init's 4 knobs):
 *   P1 = global dry/wet Mix
 *   P2 -> effect p1   | CV1 takes it over (hysteresis)
 *   P3 -> effect p2   | CV2 takes it over, or arms tap-tempo on the Delay bank
 *   effect p3 = fixed musical default per patch (kFixedP3)
 *
 * Pin map (homebrew Arduino names -> libDaisy seed pins, via STM32 pin):
 *   P1 A5/PC1 -> D20 | P2 A3/PA7 -> D18 | P3 A2/PB1 -> D17
 *   CV1 A1/PA3 -> D16 | CV2 A0/PC0 -> D15
 *   BTN D1/PC11 -> D1 | OLED SCL D11/PB8 -> D11 | SDA D12/PB9 -> D12 | LED D13/PB6 -> D13
 */

#include "daisy_seed.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include <cmath>
#include <cstdio>
#include <cstring>

#include "multifx_core/voicing.h"
#include "multifx_core/ui_model.h"
#include "multifx_core/reverb_bank.h"
#include "multifx_core/delay_bank.h"
#include "multifx_core/tone_bank.h"
#include "multifx_core/misc_bank.h"

using namespace daisy;
using namespace daisysp;

using MfxOled = OledDisplay<SSD130xI2c128x64Driver>;

// ================================================================
// Patch catalogue (names + the two live-knob labels: P2 -> p1, P3 -> p2)
// ================================================================
struct PatchDef { const char* name; const char* l2; const char* l3; };
struct BankDef  { const char* tag; const PatchDef* patches; };

static const PatchDef kReverb[4] = {
    {"CLASSIC", "Decy", "Tone"}, {"PLATE", "PreD", "Tone"},
    {"TANK",    "PreD", "Tone"}, {"SHIMMER", "Decy", "Tone"},
};
static const PatchDef kDelay[4] = {
    {"PING", "Time", "Fdbk"}, {"TAPE", "Time", "Fdbk"},
    {"MULTITAP", "Time", "Sprd"}, {"ECHOVERB", "Time", "Fdbk"},
};
static const PatchDef kTone[4] = {
    {"LADDER", "Cut", "Reso"}, {"SVF", "Cut", "Reso"},
    {"COMB", "Freq", "Fdbk"}, {"WF+CHR", "Fold", "Rate"},
};
static const PatchDef kMisc[4] = {
    {"RESON", "Freq", "Damp"}, {"PITCH", "Ptch", "Size"},
    {"DRIVE", "Driv", "Tone"}, {"CRUSH", "Bits", "Rate"},
};
static const BankDef kBanks[4] = {
    {"RVB", kReverb}, {"DLY", kDelay}, {"TON", kTone}, {"MSC", kMisc},
};

// Fixed effect-p3 per patch (the Seed has no free pot for it). [bank][patch].
static const float kFixedP3[4][4] = {
    {0.50f, 0.50f, 0.60f, 0.50f},  // Reverb: classic(ign)/plate size/tank size/shimmer amt
    {0.40f, 0.35f, 0.30f, 0.45f},  // Delay : ping damp/tape wow/multitap fb/echoverb blend
    {0.30f, 0.00f, 0.40f, 0.50f},  // Tone  : ladder drive/svf morph(LP)/comb damp/wc depth
    {0.70f, 0.50f, 0.80f, 0.50f},  // Misc  : reson level/pitch fun/drive level/crush tone
};

// ================================================================
// Hardware
// ================================================================
DaisySeed hw;
MfxOled   display;
Switch    nav_btn;
static GPIO led;

// ADC channel order
enum { ADC_MIX = 0, ADC_P2, ADC_P3, ADC_CV1, ADC_CV2, ADC_NUM };

// ================================================================
// Shared MultiFX core
// ================================================================
static mfx::OutputStage g_out;
static mfx::NavModel     g_nav;

DSY_SDRAM_BSS static mfx::ReverbBank g_reverb; // A
DSY_SDRAM_BSS static mfx::DelayBank  g_delay;  // B
static mfx::ToneBank                 g_tone;   // C (SRAM: LadderFilter ctor writes pre-main)
DSY_SDRAM_BSS static mfx::MiscBank   g_misc;   // D

// ---- Patch-change crossfade (mirrors daisy_multifx_oled) ----
enum class Xf { Run, FadeOut, WaitReset, FadeIn };
static Xf    xf_state        = Xf::Run;
static float xf_gain         = 1.0f;
static bool  xf_pending_long = false;
static constexpr float kXfOutInc = 1.0f / 240.0f;   // ~5 ms @ 48 k
static constexpr float kXfInInc  = 1.0f / 1440.0f;  // ~30 ms @ 48 k
static volatile bool   g_reset_pending = false;

// ================================================================
// Control state (homebrew front end: pots inverted, CV via takeover)
// ================================================================
static float samplerate = 48000.f;
static float P1 = 0.f, P2 = 0.f, P3 = 0.f;       // smoothed pots (P1 = Mix)
static float CV1_volts = 0.f, CV2_volts = 0.f, CV2_raw = 0.f;

static inline float Adc01ToVin(float a01) { return (3.3f * a01 - 1.68f) / -0.33f; }
static inline float cv_uni01(float v)     { return fclamp((v + 5.f) * 0.1f, 0.f, 1.f); }

struct CvTakeover {
    float eps_on, eps_off; bool cv_mode;
    bool update(float pot01) {
        if(!cv_mode && pot01 <= eps_on)       cv_mode = true;
        else if(cv_mode && pot01 >= eps_off)  cv_mode = false;
        return cv_mode;
    }
};
static CvTakeover toP2 = {0.015f, 0.030f, false};
static CvTakeover toP3 = {0.015f, 0.030f, false};

// Tap tempo on CV2 (Delay bank only)
static uint32_t last_tap_ms = 0;
static float    tap_delay_samps = 24000.f;
static bool     tap_gate = false, g_have_tap = false;
static constexpr float TAP_HIGH = 1.5f, TAP_LOW = 1.0f;

// Live effect params handed to the banks
static float g_k1 = 0.f, g_k2 = 0.f, g_k3 = 0.5f, g_mix = 0.f;
static bool  g_tap_active = false; static float g_tap_samps = 0.f;

// ================================================================
// Display
// ================================================================
static bool     display_dirty   = true;
static uint32_t btn_press_start  = 0;
static bool     btn_held         = false;
static float    p2_drawn = -1.f, p3_drawn = -1.f, mix_drawn = -1.f;
static constexpr uint32_t kLongPressMs           = 500;

// OLED hibernate: the I2C frame refresh is a long blocking burst that couples noise
// into the audio. After idle we blank the panel once and then stop refreshing
// entirely — so the I2C bus goes quiet — and any pot/button activity wakes it.
// (libDaisy's SSD130x driver doesn't expose the raw 0xAE/0xAF sleep command, so we
//  can't power the panel's charge pump down without patching the submodule; halting
//  the refresh bursts is what removes the dynamic, audio-coupled noise.)
static volatile uint32_t g_last_active_ms = 0;
static bool              g_oled_sleeping  = false;
static constexpr uint32_t kHibernateMs       = 20000; // idle -> screen off
static constexpr uint32_t kDisplayMinFrameMs = 33;    // ~30 fps cap when awake
static inline void MarkActive() { g_last_active_ms = System::GetNow(); }

static void ResetActiveBank() {
    switch(g_nav.bank) {
        case 0: g_reverb.Reset((mfx::ReverbMode)g_nav.patch); break;
        case 1: g_delay.Reset((mfx::DelayMode)g_nav.patch);   break;
        case 2: g_tone.Reset((mfx::ToneMode)g_nav.patch);     break;
        case 3: g_misc.Reset((mfx::MiscMode)g_nav.patch);     break;
        default: break;
    }
}

// Clip a label to N chars (6px font) into a small buffer.
static void DrawBar(int x, int y, int w, int h, float v, bool cv) {
    v = fclamp(v, 0.f, 1.f);
    display.DrawRect(x, y, x + w, y + h, true, false);          // outline
    int fill = (int)(v * (w - 2));
    if(fill > 0) display.DrawRect(x + 1, y + 1, x + 1 + fill, y + h - 1, true, true);
    if(cv) display.DrawLine(x, y - 2, x + w, y - 2, true);      // CV-active marker above bar
}

static void UpdateDisplay() {
    if(!display_dirty) return;
    display.Fill(false);

    if(g_nav.level == mfx::NavLevel::Bank) {
        // Header band (yellow zone) + title
        display.DrawRect(0, 0, 127, 15, true, true);
        display.SetCursor(28, 3);
        display.WriteString("BANK SEL", Font_7x10, false);
        // 2x2 grid of bank tags below the band
        for(int idx = 0; idx < g_nav.num_banks; idx++) {
            int  row = idx / 2, col = idx % 2;
            int  x = col * 64, y = 16 + row * 24;
            bool sel = (idx == g_nav.preview_bank);
            if(sel) display.DrawRect(x, y, x + 63, y + 23, true, true);
            else    display.DrawRect(x, y, x + 63, y + 23, true, false);
            display.SetCursor(x + 18, y + 7);
            display.WriteString(kBanks[idx].tag, Font_7x10, !sel);
        }
    } else {
        const PatchDef& p = kBanks[g_nav.bank].patches[g_nav.patch];
        // Header band + patch name
        display.DrawRect(0, 0, 127, 15, true, true);
        display.SetCursor(2, 3);
        display.WriteString(p.name, Font_7x10, false);
        // Three labelled bars, ordered to match the physical trimmer layout:
        // Mix (top trimmer) on top, then P2 -> p1, then P3 -> p2.
        const int bx = 40, bw = 84, bh = 11;
        display.SetCursor(2, 19);  display.WriteString("Mix", Font_6x8, true);
        DrawBar(bx, 18, bw, bh, P1, false);
        display.SetCursor(2, 35);  display.WriteString(p.l2,  Font_6x8, true);
        DrawBar(bx, 34, bw, bh, toP2.cv_mode ? cv_uni01(CV1_volts) : P2, toP2.cv_mode);
        display.SetCursor(2, 51);  display.WriteString(p.l3,  Font_6x8, true);
        DrawBar(bx, 50, bw, bh, toP3.cv_mode ? cv_uni01(CV2_volts) : P3, toP3.cv_mode);
    }

    display.Update();
    display_dirty = false;
}

// ================================================================
// Controls + navigation (once per audio block)
// ================================================================
static void ProcessControls() {
    // Pots: homebrew wiring is inverted; smooth like the old firmware.
    P1 = 0.98f * P1 + 0.02f * (1.f - hw.adc.GetFloat(ADC_MIX));
    P2 = 0.98f * P2 + 0.02f * (1.f - hw.adc.GetFloat(ADC_P2));
    P3 = 0.98f * P3 + 0.02f * (1.f - hw.adc.GetFloat(ADC_P3));

    float cv1 = Adc01ToVin(hw.adc.GetFloat(ADC_CV1));
    float cv2 = Adc01ToVin(hw.adc.GetFloat(ADC_CV2));
    CV1_volts = 0.95f * CV1_volts + 0.05f * cv1;
    CV2_volts = 0.90f * CV2_volts + 0.10f * cv2;
    CV2_raw   = cv2;

    g_k1  = toP2.update(P2) ? cv_uni01(CV1_volts) : P2;
    g_k2  = toP3.update(P3) ? cv_uni01(CV2_volts) : P3;
    g_k3  = kFixedP3[g_nav.bank][g_nav.patch];
    g_mix = P1;

    // Tap tempo on CV2 (Delay bank only)
    uint32_t now = System::GetNow();
    if(g_nav.bank == 1) {
        if(!tap_gate && CV2_raw >= TAP_HIGH) {
            tap_gate = true;
            uint32_t dt = now - last_tap_ms;
            if(dt > 50 && dt < 2000) { tap_delay_samps = (dt / 1000.f) * samplerate; g_have_tap = true; }
            last_tap_ms = now;
        } else if(tap_gate && CV2_raw <= TAP_LOW) {
            tap_gate = false;
        }
        if(g_have_tap && (now - last_tap_ms) > 1800) g_have_tap = false;
    } else {
        g_have_tap = false;
    }
    g_tap_active = g_have_tap;
    g_tap_samps  = tap_delay_samps;

    // Redraw + count as activity (wakes the OLED) when a bar moves enough.
    if(fabsf(P1 - mix_drawn) > 0.01f || fabsf(P2 - p2_drawn) > 0.01f ||
       fabsf(P3 - p3_drawn) > 0.01f) {
        mix_drawn = P1; p2_drawn = P2; p3_drawn = P3; display_dirty = true; MarkActive();
    }
}

static void ProcessNav() {
    nav_btn.Debounce();
    uint32_t now = System::GetNow();
    if(nav_btn.Pressed()) {
        if(!btn_held) { btn_press_start = now; btn_held = true; MarkActive(); }
    } else if(btn_held) {
        uint32_t dur = now - btn_press_start; btn_held = false;
        if(xf_state == Xf::Run) {
            bool is_long = (dur >= kLongPressMs);
            bool audio_change =
                (g_nav.level == mfx::NavLevel::Patch && !is_long) ||
                (g_nav.level == mfx::NavLevel::Bank  &&  is_long);
            if(audio_change) { xf_pending_long = is_long; xf_state = Xf::FadeOut; }
            else { if(is_long) g_nav.OnLongPress(); else g_nav.OnShortPress(); display_dirty = true; }
        }
    }
}

static inline float NextXfGain() {
    switch(xf_state) {
        case Xf::FadeOut:
            xf_gain -= kXfOutInc;
            if(xf_gain <= 0.f) {
                xf_gain = 0.f;
                if(xf_pending_long) g_nav.OnLongPress(); else g_nav.OnShortPress();
                g_reset_pending = true; display_dirty = true; xf_state = Xf::WaitReset;
            }
            break;
        case Xf::WaitReset:
            xf_gain = 0.f;
            if(!g_reset_pending) xf_state = Xf::FadeIn;
            break;
        case Xf::FadeIn:
            xf_gain += kXfInInc;
            if(xf_gain >= 1.f) { xf_gain = 1.f; xf_state = Xf::Run; }
            break;
        default: break;
    }
    return xf_gain;
}

static inline void MixOut(float dryL, float dryR, float wetL, float wetR,
                          float& outL, float& outR) {
    float oL = (1.f - g_mix) * dryL + g_mix * wetL;
    float oR = (1.f - g_mix) * dryR + g_mix * wetR;
    g_out.Process(oL, oR);
    float g = NextXfGain();
    outL = oL * g; outR = oR * g;
}

// ================================================================
// Audio
// ================================================================
// NOTE: A faint block-rate comb (harmonics at the audio block rate) was chased here
// and turned out NOT to be a firmware bug — per-sample control smoothing made no
// difference, the same DSP on Patch.Init is clean to -92 dB, and the comb scaled
// with each effect's signal gain (Ladder worst, Crush best). Root cause was hardware:
// the Daisy's bursty per-block digital current on the *shared* +5 V rail modulated
// the codec's voltage reference (multiplicative -> gain-scaled). Fixed on the board by
// a dedicated buck supply for the Daisy +5 V (-66 -> -90 dB). Full bisection in NOISE.md.
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    ProcessControls();
    ProcessNav();

    const float k1 = g_k1, k2 = g_k2, k3 = g_k3;
    switch(g_nav.bank) {
        case 0: {
            mfx::ReverbMode m = (mfx::ReverbMode)g_nav.patch;
            for(size_t i = 0; i < size; i++) {
                float wl, wr; g_reverb.Process(m, in[0][i], in[1][i], k1, k2, k3, wl, wr);
                MixOut(in[0][i], in[1][i], wl, wr, out[0][i], out[1][i]);
            }
        } break;
        case 1: {
            mfx::DelayMode m = (mfx::DelayMode)g_nav.patch;
            for(size_t i = 0; i < size; i++) {
                float wl, wr; g_delay.Process(m, in[0][i], in[1][i], k1, k2, k3,
                                              g_tap_active, g_tap_samps, wl, wr);
                MixOut(in[0][i], in[1][i], wl, wr, out[0][i], out[1][i]);
            }
        } break;
        case 2: {
            mfx::ToneMode m = (mfx::ToneMode)g_nav.patch;
            for(size_t i = 0; i < size; i++) {
                float wl, wr; g_tone.Process(m, in[0][i], in[1][i], k1, k2, k3, wl, wr);
                MixOut(in[0][i], in[1][i], wl, wr, out[0][i], out[1][i]);
            }
        } break;
        case 3: {
            mfx::MiscMode m = (mfx::MiscMode)g_nav.patch;
            for(size_t i = 0; i < size; i++) {
                float wl, wr; g_misc.Process(m, in[0][i], in[1][i], k1, k2, k3, wl, wr);
                MixOut(in[0][i], in[1][i], wl, wr, out[0][i], out[1][i]);
            }
        } break;
        default: break;
    }
    // NB: no display I/O here — the blocking I2C frame refresh runs in the main
    // loop so it never stalls the audio block.
}

// ================================================================
// main
// ================================================================
int main(void) {
    hw.Init();
    hw.SetAudioBlockSize(48);
    samplerate = hw.AudioSampleRate();

    // ADC: 5 single-ended channels (homebrew Seed pins).
    AdcChannelConfig adc[ADC_NUM];
    adc[ADC_MIX].InitSingle(seed::D20); // P1 (A5/PC1)
    adc[ADC_P2 ].InitSingle(seed::D18); // P2 (A3/PA7)
    adc[ADC_P3 ].InitSingle(seed::D17); // P3 (A2/PB1)
    adc[ADC_CV1].InitSingle(seed::D16); // CV1 (A1/PA3)
    adc[ADC_CV2].InitSingle(seed::D15); // CV2 (A0/PC0)
    hw.adc.Init(adc, ADC_NUM);

    nav_btn.Init(seed::D1, samplerate, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);
    led.Init(seed::D13, GPIO::Mode::OUTPUT);

    // Shared core + banks. LPF disabled (the user voices the analog front end in HW),
    // leaving DC-block + soft clamp.
    g_out.Init(samplerate, 0.f);
    g_reverb.Init(samplerate);
    g_delay.Init(samplerate);
    g_tone.Init(samplerate);
    g_misc.Init(samplerate);

    g_nav.num_banks = 4;
    for(int i = 0; i < 4; i++) g_nav.patches[i] = 4;
    g_nav.level = mfx::NavLevel::Patch; g_nav.bank = 0; g_nav.patch = 0; g_nav.preview_bank = 0;
    ResetActiveBank();

    // OLED: libDaisy native 128x64 SSD1306 over hardware I2C1 (default cfg = PB8/PB9).
    MfxOled::Config cfg;
    display.Init(cfg);
    display.Fill(false);
    display.DrawRect(0, 0, 127, 15, true, true);
    display.SetCursor(20, 3); display.WriteString("MULTIFX SEED", Font_7x10, false);
    display.Update();
    System::Delay(700);
    display_dirty = true;

    hw.adc.Start();
    hw.StartAudio(AudioCallback);

    g_last_active_ms = System::GetNow();
    uint32_t last_draw = 0;
    while(1) {
        // Heavy bank Reset() runs here (off the audio ISR) while the crossfade mutes.
        if(g_reset_pending) { ResetActiveBank(); g_reset_pending = false; }

        uint32_t now = System::GetNow();

        // Hibernate: power the OLED down after idle so its blocking I2C refresh
        // stops injecting noise into the audio; wake on any pot/button activity.
        bool want_sleep = (now - g_last_active_ms) >= kHibernateMs;
        if(want_sleep && !g_oled_sleeping) {
            g_oled_sleeping = true;
            display.Fill(false); display.Update(); // blank once, then go quiet
        } else if(!want_sleep && g_oled_sleeping) {
            g_oled_sleeping = false;
            display_dirty = true;                  // wake -> redraw on next frame
        }

        // The only place the (blocking) I2C frame refresh runs. Skipped while asleep.
        if(!g_oled_sleeping && display_dirty && (now - last_draw) >= kDisplayMinFrameMs) {
            last_draw = now;
            UpdateDisplay();
        }

        led.Write((now / 500) & 1); // heartbeat
        System::Delay(1);
    }
}
