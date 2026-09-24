/**
 * NEURAL — neural audio models on the Patch SM
 *
 * Phase 1 skeleton: the signal chain, controls, metering and display that the
 * NAM A2 engine drops into, with the model slot running as a straight
 * passthrough until the engine is lifted (CLAUDE.md, phase 1 step 3).
 *
 * The passthrough is not a placeholder for its own sake. It is exactly the
 * firmware phase 0 bench check 2 asks for: feed a sub-1Hz LFO into IN_L and
 * watch whether the reading holds its level or decays toward zero, which
 * answers whether the Patch SM's audio input is AC- or DC-coupled. That is what
 * the DC page is for, and it is why this builds BOOT_NONE for now — see the
 * Makefile.
 *
 * Hardware: Electrosmith Patch.Init(), 64x48 SSD1306 on soft I2C (A2=SDA,
 * A3=SCL). IN_R is normalled to IN_L on the carrier, so only IN_L is read.
 *
 * Controls
 *   CV_1 (+ CV_5 jack)   input trim into the model, -20..+20 dB, unity at noon
 *   CV_2 (+ CV_6 jack)   output level, 0..1
 *   B7 short press       bypass on/off, to A/B the model against the dry input
 *   B7 long press        change page, RUN <-> DC
 *   CV_OUT_2 LED         lit when the model is in circuit, dark when bypassed
 *
 * Audio: IN_L -> trim -> model slot -> level -> OUT_L and OUT_R.
 * Bypass takes the dry input to the same output level, so an A/B compares the
 * model against the input rather than against a level change.
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "oled_soft_i2c.h"
#include <cmath>
#include <cstdio>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// ================================================================
// Model slot
// ================================================================
//
// Phase 1 step 3 replaces the body of Process() with the A2 engine lifted from
// tone-3000/nam-pedal branch `t3k-pedal` @ 6dc47a4 (nam_model.c/.h, keeping its
// NAM_DTCM placement), with bkshepherd's nam_a2_runtime.h kept open alongside
// as the readable version of the same maths. Both are MIT; per the working
// rules their licence text lands in daisy_neural/LICENSE-<project>.txt and the
// source commit goes in a comment at the top of each lifted file.
//
// The seam is deliberately one sample in, one sample out. A2 is causal and
// sample-by-sample, so nothing here needs to change shape when it arrives —
// only Init(), Process() and Name().
class ModelSlot
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
    }

    inline float Process(float x)
    {
        // Passthrough. The engine goes here.
        return x;
    }

    // Shown on the display, truncated to the panel's ten characters.
    const char* Name() const { return "PASSTHRU"; }

  private:
    float sample_rate_ = 48000.f;
};

// ================================================================
// Hardware and state
// ================================================================
DaisyPatchSM  patch;
oled::SSD1306 display;
Switch        nav_btn;
CpuLoadMeter  cpu_meter;
ModelSlot     model;

enum class Page
{
    Run = 0,
    Dc,
};

static Page         g_page          = Page::Run;
static bool         g_bypass        = false;
static bool         g_display_dirty = true;

// Metering, written in the audio ISR and read by the UI loop. Single writer,
// single reader, and a torn float here would only flicker one frame.
static volatile float g_in_peak = 0.f;  // block peak, |x|, for the RUN meter
static volatile float g_in_dc   = 0.f;  // heavily smoothed signed input, DC page
static volatile float g_dc_min  = 0.f;
static volatile float g_dc_max  = 0.f;

// One-pole coefficient for the DC reading, set in main() for ~50 ms.
static float g_dc_coeff = 0.f;

static constexpr float kLedVolts = 2.0f;

static inline void SetLed(bool on)
{
    patch.WriteCvOut(CV_OUT_2, on ? kLedVolts : 0.0f);
}

static void ResetDcHold()
{
    g_dc_min = 0.f;
    g_dc_max = 0.f;
}

// ================================================================
// Controls
// ================================================================
static constexpr uint32_t kLongPressMs = 600;

static bool     g_btn_held        = false;
static uint32_t g_btn_press_start = 0;

static void ProcessNav()
{
    nav_btn.Debounce();
    const uint32_t now = System::GetNow();

    if(nav_btn.Pressed())
    {
        if(!g_btn_held)
        {
            g_btn_press_start = now;
            g_btn_held        = true;
        }
        return;
    }

    if(!g_btn_held)
        return;

    // Released. Switch::TimeHeldMs() reads zero once the switch is no longer
    // Pressed(), so the duration is timed here rather than read back from it —
    // the same reason daisy_multifx_oled keeps its own press timer.
    const uint32_t dur = now - g_btn_press_start;
    g_btn_held         = false;

    if(dur >= kLongPressMs)
    {
        g_page = (g_page == Page::Run) ? Page::Dc : Page::Run;
        if(g_page == Page::Dc)
            ResetDcHold();
    }
    else
    {
        g_bypass = !g_bypass;
        SetLed(!g_bypass);
    }
    g_display_dirty = true;
}

// ================================================================
// Audio
// ================================================================
void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();

    patch.ProcessAllControls();
    ProcessNav();

    // Pot + CV jack summed and clamped, the house attenuverter behaviour.
    const float trim_k  = fclamp(patch.GetAdcValue(CV_1) + patch.GetAdcValue(CV_5), 0.f, 1.f);
    const float level_k = fclamp(patch.GetAdcValue(CV_2) + patch.GetAdcValue(CV_6), 0.f, 1.f);

    // Trim spans -20..+20 dB with unity at noon, so the signal can be set to
    // the level the model was trained at. Computed per block, not per sample.
    const float trim  = powf(10.f, (trim_k * 2.f - 1.f));
    const float level = level_k;

    float peak = 0.f;
    float dc   = g_in_dc;

    for(size_t i = 0; i < size; i++)
    {
        const float x = in[0][i];

        const float a = fabsf(x);
        if(a > peak)
            peak = a;

        // Slow one-pole, for watching a sub-1Hz LFO on the DC page.
        dc += g_dc_coeff * (x - dc);

        float y;
        if(g_bypass)
        {
            y = x;
        }
        else
        {
            y = model.Process(x * trim);
        }
        y *= level;

        out[0][i] = y;
        out[1][i] = y;
    }

    g_in_peak = peak;
    g_in_dc   = dc;

    if(dc < g_dc_min)
        g_dc_min = dc;
    if(dc > g_dc_max)
        g_dc_max = dc;

    cpu_meter.OnBlockEnd();
}

// ================================================================
// Display
// ================================================================
static void DrawRunPage()
{
    char line[16];

    display.DrawString(0, 0, model.Name(), false);

    // Input peak meter. The point of it is setting the trim, so it reads the
    // input before the trim rather than after.
    const float peak = fclamp(g_in_peak, 0.f, 1.f);
    const uint8_t w  = (uint8_t)(peak * 62.f + 0.5f);
    display.DrawString(0, 10, "IN", false);
    display.DrawRect(0, 19, 64, 7, true);
    if(w > 0)
        display.FillRect(1, 20, w > 62 ? 62 : w, 5, true);

    snprintf(line, sizeof(line), "CPU %2d%%",
             (int)(cpu_meter.GetAvgCpuLoad() * 100.f + 0.5f));
    display.DrawString(0, 29, line, false);

    snprintf(line, sizeof(line), "MAX %2d%%",
             (int)(cpu_meter.GetMaxCpuLoad() * 100.f + 0.5f));
    display.DrawString(0, 38, line, false);

    if(g_bypass)
        display.DrawString(46, 0, "BYP", true);
}

static void DrawDcPage()
{
    char line[16];

    // Bench check 2: with a sub-1Hz LFO on IN_L, a DC-coupled input tracks it
    // and the min/max hold the LFO's excursion. An AC-coupled input sags back
    // toward zero and the hold collapses.
    display.DrawString(0, 0, "DC TEST", false);

    snprintf(line, sizeof(line), "IN %+5.2f", (double)g_in_dc);
    display.DrawString(0, 12, line, false);

    snprintf(line, sizeof(line), "LO %+5.2f", (double)g_dc_min);
    display.DrawString(0, 22, line, false);

    snprintf(line, sizeof(line), "HI %+5.2f", (double)g_dc_max);
    display.DrawString(0, 32, line, false);

    snprintf(line, sizeof(line), "SPN %4.2f", (double)(g_dc_max - g_dc_min));
    display.DrawString(0, 41, line, false);
}

static void UpdateDisplay()
{
    display.Clear();
    if(g_page == Page::Run)
        DrawRunPage();
    else
        DrawDcPage();
    display.Update();
}

// ================================================================
// main
// ================================================================
int main(void)
{
    patch.Init();
    const float sr = patch.AudioSampleRate();

    nav_btn.Init(patch.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);

    patch.StartDac();

    model.Init(sr);
    cpu_meter.Init(sr, patch.AudioBlockSize());

    // ~50 ms one-pole for the DC reading: slow enough to be readable, fast
    // enough to follow a sub-1Hz LFO without lagging it.
    g_dc_coeff = 1.f - expf(-1.f / (0.05f * sr));

    display.Init(patch.A2, patch.A3);
    display.Clear();
    display.DrawStringCentered(8, "NEURAL", false);
    display.DrawStringCentered(24, "PASSTHRU", false);
    display.Update();
    System::Delay(800);

    SetLed(!g_bypass);

    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    uint32_t next_frame = 0;
    while(1)
    {
        const uint32_t now = System::GetNow();
        if(now >= next_frame || g_display_dirty)
        {
            next_frame      = now + 50;  // 20 fps; the soft I2C write is slow
            g_display_dirty = false;
            UpdateDisplay();
        }
    }
}
