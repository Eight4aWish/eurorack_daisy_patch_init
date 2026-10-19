/**
 * MIRTH GRU TEST — the Doepfer A-124 Wasp GRUs on the Patch SM, for a first listen and a
 * CPU measurement before GRUs go into Mirth proper. `make GRU_TEST=1`.
 *
 * Two networks are compiled in (nam/gru_test_nets.h, written by tools/gru_to_header.py),
 * both from the Wasp's LP series: the cutoff + Res GRU-24, and the cutoff-only GRU-24
 * trained at Res 6. No card is needed. The engine is nam/gru_runtime.h, checked against
 * PyTorch on the desktop by tools/gru_check.py.
 *
 * Controls: Mirth's FILTERS layout, as David set it 2026-10-19 (knobs 1-4 = patch, mix,
 * cutoff, resonance; AMPS and NOT-AMPS are patch, mix, drive, steer)
 *   CV_1 (+ CV_5 jack)   which network: the lower half of the knob the first, the upper
 *                        the second
 *   CV_2 (+ CV_6 jack)   MIX, dry (0) to wet (1)
 *   CV_3 (+ CV_7 jack)   cutoff: the knob 0..10 V, like the Wasp's Frq; the jack adds its
 *                        volts, like the Wasp's CV 1 (the jack reads +-5 V)
 *   CV_4 (+ CV_8 jack)   Res, 0..10 on the Wasp's dial (the cutoff-only network ignores it)
 *   B7                   bypass on/off, to A/B the network against the dry input
 *   CV_OUT_2 LED         lit when the network is in circuit, dark when bypassed
 *
 * There is no drive knob in FILTERS: the input goes to the network as it arrives. The
 * networks learned the Wasp with 11 V as full scale (the ES-10); the patch.init's own
 * input full scale is not yet measured, so volts do not yet map to volts.
 *
 * The cutoff is clamped to 0..10 V and Res to 0..10: the ranges the networks were
 * trained on. The cutoff is read once a block and ramped across it; Res is smoothed
 * over about 20 ms. Audio: IN_L -> network -> wet; IN_L -> dry; dry/wet mix -> DC block
 * -> OUT_L and OUT_R, as in Mirth.
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "oled_soft_i2c.h"
#include <cmath>
#include <cstdio>

// The weights and the tanh table in DTCMRAM, the fastest memory the core has. Under
// BOOT_NONE the section does not exist and everything lands in ordinary .bss.
#ifndef NEURAL_NO_TCM_PLACEMENT
#define GRU_HOT_DATA __attribute__((section(".dtcmram_bss")))
#endif
#include "nam/gru_runtime.h"
#include "nam/gru_test_nets.h"

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

DaisyPatchSM  patch;
oled::SSD1306 display;
Switch        nav_btn;
CpuLoadMeter  cpu_meter;

// inline, like the runtime's tanh table: GCC refuses a named section holding both
// inline (COMDAT) and ordinary objects ("section type conflict").
GRU_HOT_DATA inline gru::Net   g_nets[gru_nets::kCount];
GRU_HOT_DATA inline gru::State g_state;
static bool                    g_ok[gru_nets::kCount];
static float                   g_gain[gru_nets::kCount];
static char                    g_name[gru_nets::kCount][gru::kGrnbNameLen];

static volatile int  g_net    = 0;  // the network playing
static volatile bool g_bypass = false;
static float         g_fade   = 1.f;
static constexpr float kFadeStep = 0.1f;  // per 48-sample block: 10 ms to silence and back

// For the display. Single writer (the callback), single reader (the main loop).
static volatile float g_in_peak = 0.f, g_cut = 0.f, g_res = 6.f, g_mix = 0.f;
static volatile int   g_want = 0;

static constexpr float kLedVolts = 2.0f;

static bool HasRes(int i)
{
    for(int k = 0; k < g_nets[i].inputs; k++)
        if(g_nets[i].kind[k] == gru::kRes)
            return true;
    return false;
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    cpu_meter.OnBlockStart();
    patch.ProcessAllControls();
    nav_btn.Debounce();
    if(nav_btn.RisingEdge())
    {
        g_bypass = !g_bypass;
        patch.WriteCvOut(CV_OUT_2, g_bypass ? 0.f : kLedVolts);
    }

    // Pot + CV jack summed and clamped, the house behaviour (see Mirth's main.cpp). The
    // knobs read 0..1 across their travel; the jacks read -1..1 for -5..+5 V.
    const float sel    = fclamp(patch.GetAdcValue(CV_1) + patch.GetAdcValue(CV_5), 0.f, 1.f);
    const float mix    = fclamp(patch.GetAdcValue(CV_2) + patch.GetAdcValue(CV_6), 0.f, 1.f);
    const float cut_to = fclamp(10.f * patch.GetAdcValue(CV_3) + 5.f * patch.GetAdcValue(CV_7), 0.f, 10.f);
    const float res_to = 10.f * fclamp(patch.GetAdcValue(CV_4) + patch.GetAdcValue(CV_8), 0.f, 1.f);

    // Which network: the knob in equal parts, with a quarter-step of hysteresis so a knob
    // sitting on a boundary does not flip back and forth.
    const int   n    = gru_nets::kCount;
    const float span = 1.f / (float)n;
    int         idx  = (int)(sel * (float)n);
    if(idx >= n)
        idx = n - 1;
    if(idx != g_want && fabsf(sel - ((float)g_want + 0.5f) * span) > span * 0.75f)
        g_want = idx;

    static float res = 6.f, cut = 0.f;
    res += 0.05f * (res_to - res);

    // Switching networks: fade the wet side out, change and start the new one from rest,
    // fade back in.
    if(g_want != g_net)
    {
        g_fade -= kFadeStep;
        if(g_fade <= 0.f)
        {
            g_fade = 0.f;
            g_net  = g_want;
            g_state.Reset();
        }
    }
    else if(g_fade < 1.f)
        g_fade = fminf(1.f, g_fade + kFadeStep);

    const int       ni   = g_net;
    const gru::Net& net  = g_nets[ni];
    const bool      run  = g_ok[ni] && !g_bypass;
    const float     gain = g_gain[ni];

    static float dcb_x1 = 0.f, dcb_y1 = 0.f;
    constexpr float kDcR = 0.99738f;  // ~20 Hz one-pole, exp(-2*pi*20/48000)
    // Bypass is the dry side alone, as in Mirth, so an A/B compares the network with its input.
    const float wetg = g_bypass ? 0.f : mix * g_fade, dryg = g_bypass ? 1.f : 1.f - mix;
    float       peak = 0.f;
    float       raw[gru::kMaxInputs];
    for(size_t i = 0; i < size; i++)
    {
        const float x = in[0][i];
        peak          = fmaxf(peak, fabsf(x));
        const float c = cut + (cut_to - cut) * (float)(i + 1) / (float)size;
        float       wet = 0.f;
        if(run)
        {
            for(int k = 0; k < net.inputs; k++)
                raw[k] = net.kind[k] == gru::kAudio ? x : net.kind[k] == gru::kCv ? c : res;
            wet = gru::Step(net, g_state, raw) * gain;
            if(!std::isfinite(wet))
            {
                wet = 0.f;
                g_state.Reset();
            }
        }
        const float s = wet * wetg + x * dryg;
        float       y = s - dcb_x1 + kDcR * dcb_y1;
        if(!std::isfinite(y))
        {
            y      = 0.f;
            dcb_x1 = dcb_y1 = 0.f;
        }
        else
        {
            dcb_x1 = s;
            dcb_y1 = y;
        }
        out[0][i] = y;
        out[1][i] = y;
    }
    cut = cut_to;

    g_in_peak = peak;
    g_cut     = cut;
    g_res     = res;
    g_mix     = mix;
    cpu_meter.OnBlockEnd();
}

static void UpdateDisplay()
{
    char line[32];
    display.Clear();
    const int ni = g_net;
    // The name, inverted while bypassed, as in Mirth.
    display.DrawString(0, 0, g_ok[ni] ? g_name[ni] : "NET BAD", g_bypass || !g_ok[ni]);
    const float   peak = fclamp(g_in_peak, 0.f, 1.f);
    const uint8_t w    = (uint8_t)(peak * 62.f + 0.5f);
    display.DrawRect(0, 9, 64, 7, true);
    if(w > 0)
        display.FillRect(1, 10, w > 62 ? 62 : w, 5, true);
    const int cpu_avg = (int)(cpu_meter.GetAvgCpuLoad() * 100.f + 0.5f);
    const int cpu_max = (int)(cpu_meter.GetMaxCpuLoad() * 100.f + 0.5f);
    snprintf(line, sizeof(line), "CPU %2d(%2d)", cpu_avg > 99 ? 99 : cpu_avg, cpu_max > 99 ? 99 : cpu_max);
    display.DrawString(0, 18, line, false);
    // The knob lines sit as the knobs do on the panel: 1 and 2 on the top row (which
    // network, mix), 3 and 4 on the bottom (cutoff volts, Res). Ten characters a line.
    snprintf(line, sizeof(line), "N%d M%.2f", ni + 1, (double)g_mix);
    display.DrawString(0, 26, line, false);
    if(HasRes(ni))
        snprintf(line, sizeof(line), "F%.1fV R%.1f", (double)g_cut, (double)g_res);
    else
        snprintf(line, sizeof(line), "F%.1fV R6*", (double)g_cut);  // trained at Res 6 only
    display.DrawString(0, 34, line, false);
    display.Update();
}

int main(void)
{
    patch.Init();
    const float sr = patch.AudioSampleRate();
    nav_btn.Init(patch.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);
    patch.StartDac();

    for(int i = 0; i < gru_nets::kCount; i++)
    {
        g_ok[i] = gru::Parse(gru_nets::kNets[i].data, gru_nets::kNets[i].size, g_nets[i], g_gain[i], g_name[i]);
        if(!g_ok[i])
            snprintf(g_name[i], sizeof(g_name[i]), "NET %d BAD", i);
    }
    g_state.Reset();
    cpu_meter.Init(sr, patch.AudioBlockSize());

    display.Init(patch.A2, patch.A3);
    display.Clear();
    display.DrawStringCentered(4, "GRU TEST", false);
    display.DrawStringCentered(18, "FILTERS", false);
    display.DrawStringCentered(32, "WASP LP", false);
    display.Update();
    System::Delay(1000);

    patch.WriteCvOut(CV_OUT_2, kLedVolts);
    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    uint32_t next_frame = 0;
    while(1)
    {
        const uint32_t now = System::GetNow();
        if(now >= next_frame)
        {
            next_frame = now + 50;  // 20 fps; the soft I2C write is slow
            UpdateDisplay();
        }
    }
}
