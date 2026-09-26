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
 *   CV_3 (+ CV_7 jack)   select capture off the card
 *   CV_4 (+ CV_8 jack)   weight depth, 16 bits (transparent) down to 4
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
#include "capture_store.h"
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
// Memory placement. Under BOOT_SRAM (the default) the runtime's own pinning
// applies: hot weights and work buffers to .dtcmram_bss, the ~76KB history to
// .sram_d2_bss in the otherwise-empty RAM_D2, both on-chip and both far faster
// than leaving them to land wherever. libDaisy's SRAM script provides
// .dtcmram_bss; nam/nam_a2_sections.lds adds .sram_d2_bss (see the Makefile).
//
// Under BOOT_NONE neither section exists, so the macros are neutralised and
// everything falls into ordinary .bss — correct, just slower.
#ifdef NEURAL_NO_TCM_PLACEMENT
#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA
#endif
#include "nam/nam_a2_runtime.h"
#include "nam/model_data_nam_a2.h"

// One capture stays compiled in as a fallback, so a missing or unreadable card
// still gives you a working module rather than silence. The other four are
// unreferenced, so -fdata-sections and --gc-sections drop them; all five live
// on the card instead (tools/export_captures.py).
// outputGain is bkshepherd's hand-tuned loudness match, carried over as-is.
static constexpr const char* kFallbackName    = "JCM800*";
static const float* const    kFallbackWeights = nam_a2_models::kWeightsJcm800;
static constexpr float       kFallbackGain    = 1.1f;

// The engine's fixed block size. Anything else and we pass through rather than
// feed it a block it cannot handle.
static constexpr size_t kA2BlockSize = 48;

class EngineSlot
{
  public:
    /** Load the compiled-in fallback. */
    void InitFallback(float sample_rate)
    {
        sample_rate_ = sample_rate;
        Load(kFallbackWeights, nam_a2_daisy::kA2WeightCount, kFallbackGain, kFallbackName);
    }

    /** Not real-time safe: load_weights() runs prewarm(), which walks the whole
     *  network. Call from the main loop with the output muted, never from the
     *  audio callback. */
    bool Load(const float* weights, size_t count, float gain, const char* name)
    {
        loaded_ = player_.load_weights(weights, count);
        if(loaded_)
        {
            gain_ = gain;
            snprintf(name_, sizeof(name_), "%s", name);
        }
        return loaded_;
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
            out[i] *= gain_;
    }

    bool        Loaded() const { return loaded_; }
    const char* Name() const { return loaded_ ? name_ : "NO CAP"; }

  private:
    nam_a2_daisy::A2Player player_;
    float                  sample_rate_ = 48000.f;
    float                  gain_        = 1.f;
    char                   name_[16]    = "NO CAP";
    bool                   loaded_      = false;
};

// ================================================================
// Hardware and state
// ================================================================
DaisyPatchSM  patch;
oled::SSD1306 display;
Switch        nav_btn;
CpuLoadMeter  cpu_meter;

// NAM_A2_STATE_DATA goes on the *instance*, not inside the runtime header —
// the header only defines the macro and leaves placement to the user, exactly
// as bkshepherd's own module does (`NAM_A2_STATE_DATA static A2Player ...`).
// EngineSlot's only large member is the A2Player, whose only per-instance
// member is A2State and its ~76 KB history, so placing this one object is what
// puts the history in RAM_D2.
//
// Miss this and nothing complains: it links, it boots, and the history quietly
// occupies 80% of DTCMRAM instead. The build's region table is the only tell.
NAM_A2_STATE_DATA EngineSlot engine;

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

// ---------------------------------------------------------------------------
// Capture switching
// ---------------------------------------------------------------------------
// CV_3 picks a capture off the card. Swapping one in calls load_weights(),
// which runs prewarm() over the whole network — far too slow for the audio
// callback — so the callback only fades out and raises a flag, the main loop
// does the load, and the callback fades back in. Same shape as the crossfade
// in daisy_multifx_oled, and for the same reason.
enum class Xf
{
    Run = 0,   // playing
    FadeOut,   // ramping to silence ahead of a swap
    Swap,      // muted; the main loop is loading
    FadeIn,    // ramping back up
};

static volatile Xf  g_xf        = Xf::Run;
static volatile int g_want      = 0;  // index CV_3 is asking for
static volatile int g_active    = -1; // index currently loaded, -1 = fallback
static float        g_xf_gain   = 1.f;

// ~5ms at 48kHz/48, which is long enough not to click and short enough not to
// feel like a gap when auditioning captures back to back.
static constexpr float kXfStep = 0.1f;

// Two buffers: the capture as read off the card, and the version handed to the
// engine. Keeping the original means the depth knob can be turned back up
// without rereading the card, and stops repeated quantisation compounding —
// requantising an already-quantised array would ratchet the damage.
// ~7.3 KB each in ordinary .bss (DTCMRAM under BOOT_SRAM, which has room), and
// both are only touched while muted.
static float g_weight_raw[nam_a2_daisy::kA2WeightCount];
static float g_weight_buf[nam_a2_daisy::kA2WeightCount];

// ---------------------------------------------------------------------------
// Weight depth — the parameter, not the defect
// ---------------------------------------------------------------------------
// Rounding the weights to fewer bits does not add noise to the signal, it
// moves the model: the learned transfer curve itself gets coarser, so you get
// a different nonlinearity rather than a degraded one. In the guitar world
// that is pure loss, because the whole product is fidelity to one amp. Here
// there is no target, so it is a timbre control.
//
// Range set by measurement, not taste (tools/quantisation_study.py, JCM800):
//   16..12  transparent, -63 to -40 dB ESR. Nothing to hear.
//   10..8   audibly a different amp, level and shape intact. The useful part.
//   7..6    clearly different, still coherent. -15 to -13 dB.
//   5       marginal; collapses at chunk 64 and only half survives at chunk 8.
//   4       dead at every chunk size — output goes to silence.
// So the floor is 6. A knob that can reach silence is a trap, and the flat
// transparent region at the top is a feature: "off" wants to be easy to find,
// especially while the pot scaling is still unconfirmed.
static constexpr int kBitsMax = 16;
static constexpr int kBitsMin = 6;

// Scale granularity. CHUNKED gives every run of this many weights its own
// scale; see the README for why that is worth 1.5-2 bits. Smaller chunks buy a
// little more at the bottom (8 bits goes from -18.7 to -22 dB at chunk 8) at
// the cost of more scales to store. Set to 0 for a single global scale, which
// sounds coarser sooner and collapses to silence below 8 bits — a destructive
// variant worth exposing once the panel has a control free for it.
static constexpr int kQuantChunk = 64;

static volatile int g_want_bits   = kBitsMax;
static volatile int g_active_bits = kBitsMax;

/** Symmetric uniform quantisation, src -> dst. Not real-time safe (it walks
 *  the whole array); main loop only, with the output muted. */
static void QuantiseWeights(const float* src, float* dst, size_t n, int bits, int chunk)
{
    if(bits >= kBitsMax)
    {
        memcpy(dst, src, n * sizeof(float));
        return;
    }

    const float qmax = (float)((1 << (bits - 1)) - 1);
    const size_t step = (chunk > 0) ? (size_t)chunk : n;

    for(size_t start = 0; start < n; start += step)
    {
        const size_t end = (start + step < n) ? start + step : n;

        float peak = 0.f;
        for(size_t i = start; i < end; i++)
        {
            const float a = fabsf(src[i]);
            if(a > peak)
                peak = a;
        }
        if(peak <= 0.f)
        {
            for(size_t i = start; i < end; i++)
                dst[i] = 0.f;
            continue;
        }

        const float scale = peak / qmax;
        const float inv   = 1.f / scale;
        for(size_t i = start; i < end; i++)
        {
            float q = roundf(src[i] * inv);
            if(q > qmax)
                q = qmax;
            if(q < -qmax)
                q = -qmax;
            dst[i] = q * scale;
        }
    }
}

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

    // --- capture selection ------------------------------------------------
    // CV_3 across however many captures the card turned up. Hysteresis of half
    // a step, so a knob sitting on a boundary does not sit there reloading the
    // network every block.
    const int n_caps = captures::Count();
    if(n_caps > 1)
    {
        const float sel  = fclamp(patch.GetAdcValue(CV_3) + patch.GetAdcValue(CV_7), 0.f, 1.f);
        const float span = 1.f / (float)n_caps;
        int         idx  = (int)(sel * (float)n_caps);
        if(idx >= n_caps)
            idx = n_caps - 1;

        const float centre = ((float)g_want + 0.5f) * span;
        if(idx != g_want && fabsf(sel - centre) > span * 0.75f)
            g_want = idx;
    }

    // --- weight depth -----------------------------------------------------
    // CV_4 from transparent down to coarse. Stepped, with the same half-step
    // hysteresis as the capture selector so a knob on a boundary does not sit
    // reloading. 16 bits means "leave the weights alone".
    {
        const float d = fclamp(patch.GetAdcValue(CV_4) + patch.GetAdcValue(CV_8), 0.f, 1.f);
        const int   steps = kBitsMax - kBitsMin + 1;
        const float span  = 1.f / (float)steps;
        int         k     = (int)((1.f - d) * (float)steps);  // knob up = transparent
        if(k >= steps)
            k = steps - 1;
        const int bits = kBitsMax - k;

        const int   cur    = kBitsMax - g_want_bits;
        const float centre = ((float)cur + 0.5f) * span;
        if(bits != g_want_bits && fabsf((1.f - d) - centre) > span * 0.75f)
            g_want_bits = bits;
    }

    // Drive the crossfade. The load itself happens in the main loop; all the
    // callback does is get the output to silence first and pick it up after.
    switch(g_xf)
    {
        case Xf::Run:
            if(n_caps > 0 && (g_want != g_active || g_want_bits != g_active_bits))
                g_xf = Xf::FadeOut;
            break;
        case Xf::FadeOut:
            g_xf_gain -= kXfStep;
            if(g_xf_gain <= 0.f)
            {
                g_xf_gain = 0.f;
                g_xf      = Xf::Swap;  // main loop takes it from here
            }
            break;
        case Xf::Swap: break;  // waiting on the main loop
        case Xf::FadeIn:
            g_xf_gain += kXfStep;
            if(g_xf_gain >= 1.f)
            {
                g_xf_gain = 1.f;
                g_xf      = Xf::Run;
            }
            break;
    }

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

    const float out_gain = level * g_xf_gain;
    for(size_t i = 0; i < n; i++)
    {
        const float y = scratch[i] * out_gain;
        out[0][i]     = y;
        out[1][i]     = y;
    }

    // Only reachable if the block size is ever configured above 48; the engine
    // cannot help there, so the tail passes through dry rather than going quiet.
    for(size_t i = n; i < size; i++)
    {
        out[0][i] = in[0][i] * out_gain;
        out[1][i] = in[0][i] * out_gain;
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
// Capture swapping (main loop only)
// ================================================================
// Reads a capture off the card and hands it to the engine. Both halves are
// slow — a file read, then prewarm() over the whole network — so this must
// only ever run with the output muted, which is what Xf::Swap guarantees.
static bool SwapCapture(int index, int bits)
{
    const captures::Entry* e = captures::Get(index);
    if(!e)
        return false;

    // Reread only when the capture itself changes; a depth change requantises
    // the copy already in RAM, which keeps the knob responsive and spares the
    // card a read per step.
    if(index != g_active)
    {
        if(!captures::Load(index, g_weight_raw, nam_a2_daisy::kA2WeightCount))
            return false;
    }

    QuantiseWeights(g_weight_raw, g_weight_buf, (size_t)e->weight_count, bits, kQuantChunk);

    if(!engine.Load(g_weight_buf, (size_t)e->weight_count, e->gain, e->name))
        return false;

    g_active      = index;
    g_active_bits = bits;
    return true;
}

// ================================================================
// Display
// ================================================================
static void DrawRunPage()
{
    char line[32];

    // Capture name, drawn inverted while bypassed — the panel has ten
    // characters and no room for a separate BYP flag once the capture index
    // is on screen. The LED says the same thing.
    display.DrawString(0, 0, engine.Name(), g_bypass);

    // Input peak meter. The point of it is setting the trim, so it reads the
    // input before the trim rather than after.
    const float peak = fclamp(g_in_peak, 0.f, 1.f);
    const uint8_t w  = (uint8_t)(peak * 62.f + 0.5f);
    display.DrawRect(0, 9, 64, 7, true);
    if(w > 0)
        display.FillRect(1, 10, w > 62 ? 62 : w, 5, true);

    snprintf(line, sizeof(line), "CPU %2d%%",
             (int)(cpu_meter.GetAvgCpuLoad() * 100.f + 0.5f));
    display.DrawString(0, 18, line, false);

    snprintf(line, sizeof(line), "MAX %2d%%",
             (int)(cpu_meter.GetMaxCpuLoad() * 100.f + 0.5f));
    display.DrawString(0, 26, line, false);

    // Which capture, out of how many the card turned up. With no card this
    // shows the reason instead, so "no captures" or "mount" reads as a card
    // problem rather than looking like a dead engine.
    // Clamped before formatting: both are bounded by kMaxFiles in practice, but
    // the compiler cannot see that across a translation unit and warns that a
    // ten-digit int could overrun the line.
    const int raw_n  = captures::Count();
    const int n_caps = raw_n > 99 ? 99 : raw_n;
    const int shown  = (g_active >= 0 && g_active < 99) ? g_active + 1 : 0;
    const int bits = g_active_bits < 4 ? 4 : (g_active_bits > 16 ? 16 : g_active_bits);
    if(n_caps > 0 && shown > 0)
        snprintf(line, sizeof(line), "C%d/%d B%d", shown, n_caps, bits);
    else if(n_caps > 0)
        // Card present but the fallback is playing — a load failed. The name
        // line already carries the trailing * that marks the compiled-in one.
        snprintf(line, sizeof(line), "C-/%d B%d", n_caps, bits);
    else
        snprintf(line, sizeof(line), "%.10s", captures::Status());
    display.DrawString(0, 34, line, false);

    // Raw knob reads. Ten characters is the panel budget, so they share a line.
    snprintf(line, sizeof(line), "T%+.1fL%+.1f",
             (double)g_trim_raw, (double)g_level_raw);
    display.DrawString(0, 42, line, false);
}

static void DrawDcPage()
{
    char line[24];

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

    engine.InitFallback(sr);
    cpu_meter.Init(sr, patch.AudioBlockSize());

    // Card before audio starts, so the first capture is in place by the time
    // anything is heard. A missing or unreadable card is not fatal: the
    // compiled-in fallback is already loaded and captures::Status() says why.
    captures::Init();
    if(captures::Count() > 0)
    {
        SwapCapture(0, kBitsMax);
        g_want = g_active = 0;
    }

    // ~50 ms one-pole for the DC reading: slow enough to be readable, fast
    // enough to follow a sub-1Hz LFO without lagging it.
    g_dc_coeff = 1.f - expf(-1.f / (0.05f * sr));

    display.Init(patch.A2, patch.A3);
    display.Clear();
    display.DrawStringCentered(4, "NEURAL", false);
    display.DrawStringCentered(18, engine.Name(), false);
    {
        char line[24];
        snprintf(line, sizeof(line), "SD %d", captures::Count());
        display.DrawStringCentered(32, line, false);
    }
    display.Update();
    System::Delay(1000);

    SetLed(!g_bypass);

    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    uint32_t next_frame = 0;
    while(1)
    {
        // The callback has faded to silence and is waiting on us. Do the slow
        // work here — file read plus prewarm() — then hand it back to fade in.
        if(g_xf == Xf::Swap)
        {
            if(!SwapCapture(g_want, g_want_bits))
            {
                // Failed: keep whatever is loaded and stop asking for this
                // index, or we would sit here retrying it every block.
                // captures::Status() carries the reason to the display.
                g_want      = g_active;
                g_want_bits = g_active_bits;
            }
            g_xf            = Xf::FadeIn;
            g_display_dirty = true;
        }

        const uint32_t now = System::GetNow();
        if(now >= next_frame || g_display_dirty)
        {
            next_frame      = now + 50;  // 20 fps; the soft I2C write is slow
            g_display_dirty = false;
            UpdateDisplay();
        }
    }
}
