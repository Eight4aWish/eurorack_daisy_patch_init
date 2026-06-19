/**
 * MultiFX OLED — 4 banks (unified multifx-core)
 *
 * Hardware: Daisy Patch SM, 64x48 SSD1306 OLED on soft I2C (A2=SDA, A3=SCL)
 * Navigation (B7): short press = next patch in bank, long press = bank menu.
 * Clock sync: gate_in_1 (B10) sets delay time in Bank B (Ping/EchoVerb).
 *
 * Banks (mfx::NavModel, 4 banks):
 *   A REVERB  – Classic / Plate / Tank / Shimmer        (mfx::ReverbBank)
 *   B DELAY   – Ping / Tape / MultiTap / EchoVerb        (mfx::DelayBank)
 *   C TONE    – Ladder / SVF morph / Comb / WF+Chorus    (mfx::ToneBank)
 *   D MISC    – Resonator / Pitch / Drive / Crush         (mfx::MiscBank)
 *
 * Every patch produces a fully-wet stereo signal; the shared final stage applies
 * the global CV4 dry/wet mix and the output voicing. Patch changes fade the whole
 * output out, run the buffer-clearing bank Reset() while muted, then fade in.
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "oled_soft_i2c.h"
#include <cmath>

// Shared, hardware-agnostic MultiFX core (DaisySP-only).
#include "multifx_core/voicing.h"       // OutputStage: DC-block -> LPF -> soft clamp
#include "multifx_core/ui_model.h"      // NavModel: two-level Bank/Patch navigation
#include "multifx_core/reverb_bank.h"   // Bank A
#include "multifx_core/delay_bank.h"    // Bank B
#include "multifx_core/tone_bank.h"     // Bank C
#include "multifx_core/misc_bank.h"     // Bank D

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// ================================================================
// Patch catalogue (names + per-knob labels for the OLED)
// ================================================================
struct PatchDef {
    const char* name;       // shown on the patch view
    const char* l1;         // CV1 label
    const char* l2;         // CV2 label
    const char* l3;         // CV3 label ("" when the patch only uses 2 params)
};

struct BankDef {
    const char*     name;   // shown on the patch view / bank menu
    const char*     tag;    // 3-char menu label
    int             count;
    const PatchDef* patches;
};

static const PatchDef kReverbPatches[4] = {
    {"CLASSIC",  "Fbk", "Tone", ""},
    {"PLATE",    "Pre", "Tone", "Size"},
    {"TANK",     "Pre", "Damp", "Size"},
    {"SHIMMER",  "Fbk", "Tone", "Shim"},
};
static const PatchDef kDelayPatches[4] = {
    {"PING",     "Time", "Fbk", "Damp"},
    {"TAPE",     "Time", "Fbk", "Wow"},
    {"MULTITAP", "Time", "Pan", "Fbk"},
    {"ECHOVERB", "Time", "Fbk", "RvMx"},
};
static const PatchDef kTonePatches[4] = {
    {"LADDER",   "Cut", "Res",  "Drv"},
    {"SVF MRF",  "Cut", "Res",  "Typ"},
    {"COMB",     "Frq", "Fbk",  "Brt"},
    {"WF+CHR",   "Fld", "Rate", "Dpt"},
};
static const PatchDef kMiscPatches[4] = {
    {"RESONATR", "Freq", "Damp", "InLv"},
    {"PITCH",    "Semi", "Size", "Fun"},
    {"DRIVE",    "Drv",  "Tone", "Lvl"},
    {"CRUSH",    "Bits", "Rate", "Tone"},
};

static const BankDef kBanks[] = {
    {"REVERB", "RVB", 4, kReverbPatches},
    {"DELAY",  "DLY", 4, kDelayPatches},
    {"TONE",   "TON", 4, kTonePatches},
    {"MISC",   "MSC", 4, kMiscPatches},
};
static constexpr int kNumBanks = 4;

// ================================================================
// Hardware
// ================================================================
DaisyPatchSM patch;
Switch       nav_btn; // B7 only — B8 removed for OLED
oled::SSD1306 display;

// ================================================================
// Shared MultiFX core
// ================================================================
static mfx::OutputStage  g_out;    // DC-block -> soft clamp (LPF disabled here)
static mfx::NavModel     g_nav;    // two-level Bank/Patch navigation

// Patch-change crossfade. An effect change fades the whole output out, runs the
// (heavy, buffer-clearing) bank Reset() while fully muted, then fades back in —
// so no reverb tail is cut and no buffer-clear race is audible.
enum class Xf { Run, FadeOut, WaitReset, FadeIn };
static Xf    xf_state        = Xf::Run;
static float xf_gain         = 1.0f;
static bool  xf_pending_long = false; // press type committed at the fade-out floor
static constexpr float kXfOutInc = 1.0f / 240.0f;  // ~5 ms fade-out @ 48 k
static constexpr float kXfInInc  = 1.0f / 1440.0f; // ~30 ms fade-in  @ 48 k

// Reverb/Delay/Misc hold large ReverbSc + DelayLine + pitch buffers and are
// trivially constructible, so they live in SDRAM. ToneBank stays in SRAM: its
// buffers are small, and LadderFilter's constructor writes to its members, which
// would fault if it ran (pre-main) before SDRAM is initialized.
DSY_SDRAM_BSS static mfx::ReverbBank g_reverb; // Bank A
DSY_SDRAM_BSS static mfx::DelayBank  g_delay;  // Bank B
static mfx::ToneBank                 g_tone;   // Bank C (SRAM)
DSY_SDRAM_BSS static mfx::MiscBank   g_misc;   // Bank D

// Clock detection (gate_in_1 = B10 jack) — feeds the Delay bank's tap input.
static bool     g_clk_gate       = false;
static uint32_t g_clk_last_ticks = 0;
static float    g_clk_interval_s = 0.0f;

// ================================================================
// Navigation / display state
// ================================================================
static bool     display_dirty   = true;
static uint32_t btn_press_start = 0;
static bool     btn_held        = false;
static size_t   ctrl_ticks      = 0;
// Set in the audio ISR on a patch change; the heavy bank Reset() (which can zero
// >100 kB of SDRAM for the pitch shifter) runs from the main loop, not the ISR.
static volatile bool g_reset_pending = false;
static constexpr size_t   kDisplayUpdateInterval = 16;
static constexpr uint32_t kLongPressMs           = 500;

// LED (panel LED via CV_OUT_2)
static bool   led_on        = false;
static size_t led_off_ticks = 0;
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

// Re-init the active bank's effect on patch/bank change so it does not inherit
// the previous patch's tail/buffer state.
static void ResetActiveBank()
{
    switch(g_nav.bank)
    {
        case 0: g_reverb.Reset((mfx::ReverbMode)g_nav.patch); break;
        case 1: g_delay.Reset((mfx::DelayMode)g_nav.patch);   break;
        case 2: g_tone.Reset((mfx::ToneMode)g_nav.patch);     break;
        case 3: g_misc.Reset((mfx::MiscMode)g_nav.patch);     break;
        default: break;
    }
}

// ================================================================
// Display rendering
// ================================================================
static void UpdateDisplay()
{
    if(!display_dirty) return;
    display.Clear();

    if(g_nav.level == mfx::NavLevel::Bank)
    {
        // Bank menu: 2x2 grid of bank tags, previewed bank highlighted.
        for(int idx = 0; idx < g_nav.num_banks; idx++)
        {
            int     row = idx / 2, col = idx % 2;
            uint8_t x   = (uint8_t)(col * 32);
            uint8_t y   = (uint8_t)(row * 24);
            bool    sel = (idx == g_nav.preview_bank);
            if(sel)
                display.FillRect(x, y, 32, 24);
            else
                display.DrawRect(x, y, 32, 24);
            display.DrawString(x + 7, y + 8, kBanks[idx].tag, sel);
        }
    }
    else
    {
        const BankDef&  b = kBanks[g_nav.bank];
        const PatchDef& p = b.patches[g_nav.patch];

        display.DrawStringCentered(0, p.name, false);
        display.DrawHLine(0, 9, 64);

        // 2x2 label grid: CV1 CV2 / CV3 Mix
        char row1[14], row2[14];
        snprintf(row1, sizeof(row1), "%-5s%-5s", p.l1, p.l2);
        snprintf(row2, sizeof(row2), "%-5s%-5s", p.l3[0] ? p.l3 : "", "Mix");
        display.DrawString(0, 13, row1, false);
        display.DrawString(0, 23, row2, false);
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
        btn_held = false;

        // Ignore presses while a crossfade is already in flight.
        if(xf_state == Xf::Run)
        {
            bool is_long = (dur >= kLongPressMs);
            // Does this press change the playing effect, or is it just menu nav?
            bool audio_change =
                (g_nav.level == mfx::NavLevel::Patch && !is_long) ||
                (g_nav.level == mfx::NavLevel::Bank  &&  is_long);
            if(audio_change)
            {
                // Defer the nav commit + bank reset to the fade-out floor.
                xf_pending_long = is_long;
                xf_state        = Xf::FadeOut;
            }
            else
            {
                // Menu navigation (no audio change): apply immediately.
                if(is_long) g_nav.OnLongPress(); else g_nav.OnShortPress();
                display_dirty = true;
            }
            BlinkLed();
        }
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

// Advances the crossfade state machine once per output sample and returns the
// whole-output gain. At the fade-out floor it commits the deferred patch change
// and requests the (muted) bank reset; it holds mute until the reset completes.
static inline float NextXfGain()
{
    switch(xf_state)
    {
        case Xf::FadeOut:
            xf_gain -= kXfOutInc;
            if(xf_gain <= 0.0f)
            {
                xf_gain = 0.0f;
                if(xf_pending_long) g_nav.OnLongPress(); else g_nav.OnShortPress();
                g_reset_pending = true;
                display_dirty   = true;
                xf_state        = Xf::WaitReset;
            }
            break;
        case Xf::WaitReset:
            xf_gain = 0.0f;
            if(!g_reset_pending) xf_state = Xf::FadeIn; // main loop finished Reset()
            break;
        case Xf::FadeIn:
            xf_gain += kXfInInc;
            if(xf_gain >= 1.0f) { xf_gain = 1.0f; xf_state = Xf::Run; }
            break;
        case Xf::Run:
        default:
            break;
    }
    return xf_gain;
}

// ================================================================
// Shared final stage for every patch:
//   global dry/wet (CV4) -> output voicing -> patch-change crossfade
// ================================================================
static inline void MixOut(float dryL, float dryR, float wetL, float wetR,
                          float mix, float& outL, float& outR)
{
    float oL = (1.0f - mix) * dryL + mix * wetL;
    float oR = (1.0f - mix) * dryR + mix * wetR;
    g_out.Process(oL, oR);
    float g = NextXfGain();
    outL = oL * g;
    outR = oR * g;
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

    // Pot + CV jack summed and clamped — standard Eurorack attenuverter behaviour.
    // CV1..CV3 are the effect parameters; CV4 is the global dry/wet mix.
    float k1  = fclamp(patch.GetAdcValue(CV_1) + patch.GetAdcValue(CV_5), 0.f, 1.f);
    float k2  = fclamp(patch.GetAdcValue(CV_2) + patch.GetAdcValue(CV_6), 0.f, 1.f);
    float k3  = fclamp(patch.GetAdcValue(CV_3) + patch.GetAdcValue(CV_7), 0.f, 1.f);
    float mix = fclamp(patch.GetAdcValue(CV_4) + patch.GetAdcValue(CV_8), 0.f, 1.f);

    // Clock detection via dedicated gate jack (B10) — drives the Delay bank tap.
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

    bool  tap_active = (g_clk_interval_s > 0.0f)
                    && (now_ticks - g_clk_last_ticks < 2000u);
    float tap_samps  = tap_active ? g_clk_interval_s * patch.AudioSampleRate() : 0.f;

    switch(g_nav.bank)
    {
        case 0: // Bank A — Reverb (k1,k2,k3 = effect params)
        {
            mfx::ReverbMode m = (mfx::ReverbMode)g_nav.patch;
            for(size_t i = 0; i < size; i++)
            {
                float wl, wr;
                g_reverb.Process(m, IN_L[i], IN_R[i], k1, k2, k3, wl, wr);
                MixOut(IN_L[i], IN_R[i], wl, wr, mix, OUT_L[i], OUT_R[i]);
            }
        }
        break;

        case 1: // Bank B — Delay (k1=time k2=fdbk k3=mode param; gate-in clock sync via tap)
        {
            mfx::DelayMode m = (mfx::DelayMode)g_nav.patch;
            for(size_t i = 0; i < size; i++)
            {
                float wl, wr;
                g_delay.Process(m, IN_L[i], IN_R[i], k1, k2, k3,
                                tap_active, tap_samps, wl, wr);
                MixOut(IN_L[i], IN_R[i], wl, wr, mix, OUT_L[i], OUT_R[i]);
            }
        }
        break;

        case 2: // Bank C — Tone (k1,k2,k3 = effect params)
        {
            mfx::ToneMode m = (mfx::ToneMode)g_nav.patch;
            for(size_t i = 0; i < size; i++)
            {
                float wl, wr;
                g_tone.Process(m, IN_L[i], IN_R[i], k1, k2, k3, wl, wr);
                MixOut(IN_L[i], IN_R[i], wl, wr, mix, OUT_L[i], OUT_R[i]);
            }
        }
        break;

        case 3: // Bank D — Misc (k1,k2,k3 = effect params)
        {
            mfx::MiscMode m = (mfx::MiscMode)g_nav.patch;
            for(size_t i = 0; i < size; i++)
            {
                float wl, wr;
                g_misc.Process(m, IN_L[i], IN_R[i], k1, k2, k3, wl, wr);
                MixOut(IN_L[i], IN_R[i], wl, wr, mix, OUT_L[i], OUT_R[i]);
            }
        }
        break;

        default:
        {
            for(size_t i = 0; i < size; i++)
            {
                float oL = IN_L[i], oR = IN_R[i];
                g_out.Process(oL, oR);
                OUT_L[i] = oL;
                OUT_R[i] = oR;
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

    // Shared core + banks. LPF disabled (Patch SM has a clean output) — the
    // output stage keeps only the DC blocker and the soft clamp.
    g_out.Init(sr, 0.f);
    g_reverb.Init(sr);
    g_delay.Init(sr);
    g_tone.Init(sr);
    g_misc.Init(sr);

    // Navigation layout: 4 banks, Misc holds only 2 patches.
    g_nav.num_banks  = kNumBanks;
    g_nav.patches[0] = kBanks[0].count;
    g_nav.patches[1] = kBanks[1].count;
    g_nav.patches[2] = kBanks[2].count;
    g_nav.patches[3] = kBanks[3].count;

    // OLED splash
    display.Init(patch.A2, patch.A3);
    display.Clear();
    display.DrawStringCentered(0,  "MULTIFX", false);
    display.DrawStringCentered(16, "4 BANKS", false);
    display.Update();
    System::Delay(800);

    display_dirty = true;
    SetLed(false);

    patch.StartAdc();
    patch.StartAudio(AudioCallback);
    while(1)
    {
        // Run the heavy bank Reset() here, off the audio ISR. Clear the flag
        // only after it completes so the crossfade holds mute over the memset.
        if(g_reset_pending)
        {
            ResetActiveBank();
            g_reset_pending = false;
        }
        System::Delay(1);
    }
}
