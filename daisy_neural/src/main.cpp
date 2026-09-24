/**
 * NEURAL — neural audio networks on the Patch SM
 *
 * Phase 1 skeleton: the signal chain, controls, metering and display that the
 * NAM A2 engine drops into, with the engine slot running as a straight
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
 *   CV_1 (+ CV_5 jack)   input trim into the engine, -20..+20 dB, unity at noon
 *   CV_2 (+ CV_6 jack)   output level, 0..1
 *   B7 short press       bypass on/off, to A/B the engine against the dry input
 *   B7 long press        change page, RUN <-> DC
 *   CV_OUT_2 LED         lit when the engine is in circuit, dark when bypassed
 *
 * Audio: IN_L -> trim -> engine slot -> level -> OUT_L and OUT_R.
 * Bypass takes the dry input to the same output level, so an A/B compares the
 * engine against the input rather than against a level change.
 *
 * Vocabulary follows CLAUDE.md: "engine" is the inference code, "capture" is one
 * trained weights file, and "patch" is never used for either.
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
// Engine slot — NAM A2
// ================================================================
//
// The engine is bkshepherd's single-header A2 runtime rather than nam-pedal's
// nam_model.c, because bkshepherd ships the whole path: the runtime, the
// .nam -> C array converter, and five already-converted captures. nam-pedal's
// converter (nam2c.py) is not in that repo, so its engine cannot be fed without
// writing one first. Both are MIT; this is the one that gets to a first trial.
//
// A2's API is a fixed 48-sample block, not one sample at a time — so this seam
// is block-shaped. The Patch SM defaults to 48kHz with 48-sample blocks, which
// is exactly what the engine expects, so the two line up with no buffering.
//
// Memory placement: the runtime pins its hot data to .dtcmram_bss and the ~76KB
// history to .sram_d2_bss, which need nam/nam_a2_sections.lds and the
// bootloader. For a first BOOT_NONE trial those macros are neutralised below and
// everything lands in ordinary .bss. Revisit under BOOT_SRAM if the measured CPU
// load says the placement matters.
#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA
#include "nam/nam_a2_runtime.h"
#include "nam/model_data_nam_a2.h"

// One capture compiled in, the fewest moving parts phase 1 step 4 asks for.
// The other four in model_data_nam_a2.h stay unreferenced so -fdata-sections
// and --gc-sections drop them, which matters because flash is tight here.
// outputGain is bkshepherd's hand-tuned loudness match, carried over as-is.
static constexpr const char*  kCaptureName   = "JCM800";
static const float* const     kCaptureWeights = nam_a2_models::kWeightsJcm800;
static constexpr float        kCaptureGain   = 1.1f;

// The engine's fixed block size. Anything else and we pass through rather than
// feed it a block it cannot handle.
static constexpr size_t kA2BlockSize = 48;

class EngineSlot
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        loaded_      = player_.load_weights(kCaptureWeights,
                                            nam_a2_daisy::kA2WeightCount);
    }

    // in and out may alias. Checked against the runtime rather than assumed:
    // process_block_48() does all its work in hot.bufA/bufB and writes `output`
    // only in the final process_head() call, after every layer has finished
    // reading `input`. If that ever changes upstream, this needs a second buffer.
    inline void ProcessBlock(const float* in, float* out, size_t size)
    {
        if(!loaded_ || size != kA2BlockSize)
        {
            for(size_t i = 0; i < size; i++)
                out[i] = in[i];
            return;
        }
        player_.process_block_48(in, out);
        for(size_t i = 0; i < size; i++)
            out[i] *= kCaptureGain;
    }

    bool        Loaded() const { return loaded_; }
    const char* Name() const { return loaded_ ? kCaptureName : "NO CAP"; }

  private:
    nam_a2_daisy::A2Player player_;
    float                  sample_rate_ = 48000.f;
    bool                   loaded_      = false;
};

// ================================================================
// Hardware and state
// ================================================================
DaisyPatchSM  patch;
oled::SSD1306 display;
Switch        nav_btn;
CpuLoadMeter  cpu_meter;
EngineSlot    engine;

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

// Reference leg of the HPF measurement: the same LFO into CV_5, which is a
// DC-coupled ±5V jack, so it shows what the signal is actually doing. Without
// it a collapsed audio span is ambiguous — AC coupling and an unplugged cable
// look identical. Read at block rate, which is plenty for a sub-1Hz LFO.
static volatile float g_cv_dc  = 0.f;
static volatile float g_cv_min = 0.f;
static volatile float g_cv_max = 0.f;

// One-pole coefficient for the DC reading, set in main() for ~50 ms.
static float g_dc_coeff = 0.f;

// Raw knob reads, shown on the RUN page so unconfirmed pot scaling is visible
// rather than mysterious. See the note in the audio callback.
static volatile float g_trim_raw  = 0.f;
static volatile float g_level_raw = 0.f;

static constexpr float kLedVolts = 2.0f;

static inline void SetLed(bool on)
{
    patch.WriteCvOut(CV_OUT_2, on ? kLedVolts : 0.0f);
}

static void ResetDcHold()
{
    g_dc_min  = 0.f;
    g_dc_max  = 0.f;
    g_cv_min  = 0.f;
    g_cv_max  = 0.f;
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
    // Raw reads kept for the display. libDaisy inits CV_1–CV_8 alike as bipolar
    // while the pots are wired 0–5V, so what a knob actually spans has not been
    // confirmed on hardware. The summing below is exactly what
    // daisy_multifx_oled does and is known to work on this unit, so it stays —
    // but the raw values go on screen so the first bench minute settles it
    // instead of guessing. If LVL reads 0 with the knob up, that is why there
    // is no sound, and the fix is here rather than in the engine.
    const float trim_raw  = patch.GetAdcValue(CV_1);
    const float level_raw = patch.GetAdcValue(CV_2);
    g_trim_raw            = trim_raw;
    g_level_raw           = level_raw;

    const float trim_k  = fclamp(trim_raw + patch.GetAdcValue(CV_5), 0.f, 1.f);
    const float level_k = fclamp(level_raw + patch.GetAdcValue(CV_6), 0.f, 1.f);

    // Trim spans -20..+20 dB with unity at noon, so the signal can be set to
    // the level the capture was trained at. Computed per block, not per sample.
    const float trim  = powf(10.f, (trim_k * 2.f - 1.f));
    const float level = level_k;

    float peak = 0.f;
    float dc   = g_in_dc;

    // Metering and trim first, over the whole block, because the engine wants
    // a contiguous 48 samples rather than one at a time.
    // 32-byte aligned to match the engine's own buffers (NAM_A2_ALIGN32).
    // It reads input scalar-wise so this is belt-and-braces, but a misaligned
    // access fault is a miserable thing to diagnose at the bench.
    alignas(32) static float scratch[kA2BlockSize];
    const size_t n = (size > kA2BlockSize) ? kA2BlockSize : size;

    for(size_t i = 0; i < n; i++)
    {
        const float x = in[0][i];

        const float a = fabsf(x);
        if(a > peak)
            peak = a;

        // Slow one-pole, for watching a sub-1Hz LFO on the DC page.
        dc += g_dc_coeff * (x - dc);

        scratch[i] = g_bypass ? x : (x * trim);
    }

    if(!g_bypass)
        engine.ProcessBlock(scratch, scratch, n);

    for(size_t i = 0; i < n; i++)
    {
        const float y = scratch[i] * level;
        out[0][i]     = y;
        out[1][i]     = y;
    }

    // Only reachable if the block size is ever configured above 48; the engine
    // cannot help there, so the tail passes through dry rather than going quiet.
    for(size_t i = n; i < size; i++)
    {
        out[0][i] = in[0][i] * level;
        out[1][i] = in[0][i] * level;
    }

    g_in_peak = peak;
    g_in_dc   = dc;

    if(dc < g_dc_min)
        g_dc_min = dc;
    if(dc > g_dc_max)
        g_dc_max = dc;

    // Reference leg: CV_5, a DC-coupled bipolar jack, block-rate.
    const float cv = patch.GetAdcValue(CV_5);
    g_cv_dc        = cv;
    if(cv < g_cv_min)
        g_cv_min = cv;
    if(cv > g_cv_max)
        g_cv_max = cv;

    cpu_meter.OnBlockEnd();
}

// ================================================================
// Display
// ================================================================
static void DrawRunPage()
{
    char line[16];

    display.DrawString(0, 0, engine.Name(), false);

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
    display.DrawString(0, 28, line, false);

    snprintf(line, sizeof(line), "MAX %2d%%",
             (int)(cpu_meter.GetMaxCpuLoad() * 100.f + 0.5f));
    display.DrawString(0, 35, line, false);

    // Raw knob reads. Ten characters is the panel budget, so they share a line.
    snprintf(line, sizeof(line), "T%+.1fL%+.1f",
             (double)g_trim_raw, (double)g_level_raw);
    display.DrawString(0, 42, line, false);

    if(g_bypass)
        display.DrawString(46, 0, "BYP", true);
}

static void DrawDcPage()
{
    char line[16];

    // The audio input is AC-coupled inside the Patch SM (the carrier wires the
    // jacks straight through — see patch_init_schematic.pdf), so this page is
    // not asking "is it AC?" but "where is the corner?".
    //
    // Send the same LFO to IN_L and to CV_5, sweep its frequency, and read RAT:
    // the audio span divided by the CV span. CV_5 is DC-coupled so its span is
    // the truth. RAT near 1.00 means the audio input is passing that frequency
    // intact; RAT near 0.71 is the -3dB corner. That frequency is what sets the
    // crossover for phase 5's split-path CV.
    display.DrawString(0, 0, "HPF TEST", false);

    snprintf(line, sizeof(line), "IN %+5.2f", (double)g_in_dc);
    display.DrawString(0, 9, line, false);

    const float in_span = g_dc_max - g_dc_min;
    snprintf(line, sizeof(line), "SPN %4.2f", (double)in_span);
    display.DrawString(0, 17, line, false);

    snprintf(line, sizeof(line), "CV %+5.2f", (double)g_cv_dc);
    display.DrawString(0, 25, line, false);

    const float cv_span = g_cv_max - g_cv_min;
    snprintf(line, sizeof(line), "CVS %4.2f", (double)cv_span);
    display.DrawString(0, 33, line, false);

    // Guarded: with no LFO on CV_5 the reference span is zero, and a ratio
    // against nothing is worse than no reading at all.
    if(cv_span > 0.02f)
        snprintf(line, sizeof(line), "RAT %4.2f", (double)(in_span / cv_span));
    else
        snprintf(line, sizeof(line), "RAT  --");
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

    engine.Init(sr);
    cpu_meter.Init(sr, patch.AudioBlockSize());

    // ~50 ms one-pole for the DC reading: slow enough to be readable, fast
    // enough to follow a sub-1Hz LFO without lagging it.
    g_dc_coeff = 1.f - expf(-1.f / (0.05f * sr));

    display.Init(patch.A2, patch.A3);
    display.Clear();
    display.DrawStringCentered(8, "NEURAL", false);
    display.DrawStringCentered(24, kCaptureName, false);
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
