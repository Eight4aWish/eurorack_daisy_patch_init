/**
 * MIRTH — neural amp captures, and not-amps, on the Patch SM (folder: daisy_neural)
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
 * Controls (David, 2026-10-21; each knob summed with the CV jack under it)
 *                        AMPS / NOT-AMPS                  FILTERS
 *   CV_1 (+ CV_5 jack)   MIX, dry (0) to wet (1)          MIX
 *   CV_2 (+ CV_6 jack)   STEER (not-amps only)            FREQ: the knob 0..10 V, the jack
 *                                                         adds its volts (like the Wasp's CV 1)
 *   CV_3 (+ CV_7 jack)   -                                RES, the Wasp's dial to 11 (see
 *                                                         ProcessFilter: lifted from 7, over-Res)
 *   CV_4 (+ CV_8 jack)   DRIVE, -20..+20 dB into the network, unity at noon, every bank
 *   B7 short press       next patch in the bank, wrapping; each bank keeps its own place
 *   B7 held 1.5 s        next bank: AMPS -> NOT-AMPS -> FILTERS (when the card has any)
 *   CV_OUT_2 LED         lit
 *
 * MIRTH LITE (make LITE=1, for a stock patch.init with no OLED): knobs and B7 as above;
 * the B8 toggle is unused (it was the bank until 2026-10-21).
 *   CV_OUT_2 LED         on every change of patch or bank, the bank as one, two or
 *                        three quick flickers (AMPS, NOT-AMPS, FILTERS), a pause,
 *                        then the patch's number — a long blink is five, so 12 is
 *                        long-long-short-short — then lit. A fast flicker that
 *                        never stops: no captures on the card.
 *
 * Three banks. FILTERS: GRU networks off the card (.grnb), each a filter with its
 * cutoff and resonance as live controls (nam/gru_runtime.h; the first are the
 * Doepfer A-124 Wasp's, CLAUDE.md). A version-2 file also carries a granular layer
 * (nam/gran_layer.h): the Wasp's take-to-take randomness, phased in from Res 8 to 9,
 * at 53.7 ms of latency for that slot, wet and dry alike. A file whose controls section is
 * version 2 carries an oscillator layer (nam/osc_layer.h): Swords' self-oscillation, added to
 * its network past the dial's 10 (FLT6). AMPS: the captures on the card,
 * played exactly as captured; the steer knob does nothing there, on purpose. NOT-AMPS:
 * twelve starter captures,
 * each with one transform chosen by measurement to leave the region where real
 * amps sit — sine or linear neurons, a feedback loop, a frozen layer, the engine
 * at a fraction of the sample rate, or a capture pushed past another — and one
 * steer control that can move while playing; and three random-weight captures whose
 * steer scales every weight (2026-10-23). Processing in src/notamp_dsp.h
 * (shared with the Mac harness tools/a2_notamp.cpp); definitions and level
 * tables generated into src/notamps.h by tools/notamp_design.py.
 *
 * Audio: IN_L -> drive -> engine slot -> wet; IN_L -> dry (delayed to match any
 * not-amp latency); dry/wet mix (CV_1) -> DC block -> OUT_L and OUT_R.
 * MIX fully down is the dry side alone, so an A/B compares the engine with its input.
 *
 * Vocabulary follows CLAUDE.md: "engine" is the inference code, "capture" is one
 * trained weights file, and "patch" is never used for either.
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#if !MIRTH_LITE
#include "oled_soft_i2c.h"
#endif
#include "capture_store.h"
#include "nam_import.h"
#include "notamps.h"
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef MIRTH_BUILD
#define MIRTH_BUILD "unknown"
#endif

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
// One capture is compiled in as a fallback, so a missing or unreadable card still
// gives a working module: the Orange TH100 from the starter set, CC BY, generated into
// nam/fallback_capture.h by tools/export_fallback.py, which carries the credit.
#include "nam/fallback_capture.h"
// The FILTERS bank's engine: a GRU, one sample at a time. Sized for GRU-24 at most, and
// its weights and tanh table pinned to DTCMRAM like A2's hot data, as inline objects:
// GCC refuses a named section that holds both inline and ordinary objects.
#define GRU_MAX_HIDDEN 24
#ifndef NEURAL_NO_TCM_PLACEMENT
#define GRU_HOT_DATA __attribute__((section(".dtcmram_bss")))
#endif
#include "nam/gru_runtime.h"
// A FILTERS file may carry a granular layer after its network (.grnb version 2): the
// Wasp's take-to-take randomness, phased in above Res 8 (nam/gran_layer.h). Its FFT's
// working arrays and twiddles (12 KB) go in DTCMRAM with the GRU's hot data; the rest
// of the engine is placed below, in RAM_D2.
#ifndef NEURAL_NO_TCM_PLACEMENT
#define GRAN_FAST_DATA __attribute__((section(".dtcmram_bss")))
#endif
#include "nam/gran_layer.h"
// A FILTERS file's controls may carry an oscillator layer (the section's version 2): Swords'
// self-oscillation, added to its network's output (nam/osc_layer.h; FLT6). A few hundred
// bytes, so ordinary RAM.
#include "nam/osc_layer.h"
static_assert(osc::kParams == captures::kOscFloats, "the .grnb oscillator block and the layer disagree");
static constexpr const char* kFallbackName    = fallback::kName;
static const float* const    kFallbackWeights = fallback::kWeights;
static constexpr float       kFallbackGain    = fallback::kGain;

// The engine's fixed block size. Anything else and we pass through rather than
// feed it a block it cannot handle.
static constexpr size_t kA2BlockSize = 48;

// The loaded not-amp's processing (feedback, rate, the bend, the level table);
// inactive while a real amp plays. Touched by the main loop only while muted.
static notamps::Processor g_proc;

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
        if(g_proc.Active())
            g_proc.Process(in, out, [this](const float* x, float* y) { player_.process_block_48(x, y); });
        else
            player_.process_block_48(in, out);
        for(size_t i = 0; i < size; i++)
            out[i] *= gain_;
    }

    /** Rewrite the weights while playing, with no prewarm. Used when a morph
     *  or mutation not-amp's steer moves. Audio-callback only: it must not
     *  overlap ProcessBlock, and in the callback it cannot. Rewriting every
     *  block matches the real engine exactly (tools/a2_host_steer). */
    void RewriteWeights(const float* weights)
    {
        if(loaded_)
            nam_a2_daisy::load_weights(player_.weights(), weights, nam_a2_daisy::kA2WeightCount);
    }

    void SetGain(float g) { gain_ = g; }

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
#if !MIRTH_LITE
oled::SSD1306 display;
#endif
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

// The FILTERS network, its history and its name. Loaded by the main loop while muted.
GRU_HOT_DATA inline gru::Net   g_filter;
GRU_HOT_DATA inline gru::State g_filter_state;
static float                   g_filter_gain = 1.f;
static char                    g_filter_name[gru::kGrnbNameLen] = "";

// The filter's granular layer, when its file has one: the engine's tables and rings
// (~95 KB) in RAM_D2 beside A2's history, the bank it points into (1.9 MB for the Wasp)
// in SDRAM. g_layer_on is cleared before a load and set only once one has parsed.
NAM_A2_STATE_DATA static gran::Engine g_layer;
static constexpr size_t                 kLayerCap = 4u << 20;
alignas(32) static uint8_t DSY_SDRAM_BSS g_layer_buf[kLayerCap];
static gran::Layer                      g_layer_def;
static volatile bool                    g_layer_on = false;

// The filter's oscillator layer, when its controls carry one (FLT6). Like g_layer_on,
// g_osc_on is cleared before a load and set once the layer is set up.
static osc::Layer    g_osc;
static volatile bool g_osc_on = false;

static bool         g_display_dirty = true;

// Metering, written in the audio ISR and read by the UI loop. Single writer,
// single reader, and a torn float here would only flicker one frame.
static volatile float g_in_peak = 0.f;  // block peak, |x|, for the RUN meter

// MIRTH LITE's LED: a blink request, set by the main loop and rendered by it: the slot
// playing plus one (0 = the fallback), or -1 for none.
static volatile int g_blink_request = -1;

// What the knobs are set to, for the RUN page.
static volatile float g_mix      = 0.f;
static volatile float g_drive_db = 0.f;
static volatile float g_cut      = 0.f;  // FILTERS: the FREQ position, 0-10
static volatile float g_res      = 0.f;  // FILTERS: the Res dial
// FILTERS: the network's cost (and its layer's), CPU cycles per sample: the worst block,
// easing back over about a second. A layer does a third of a frame's work in some blocks
// and none in others, so the worst block, not the average, is what decides a glitch.
static volatile float g_gru_cycles = 0.f;

// ---------------------------------------------------------------------------
// Capture switching
// ---------------------------------------------------------------------------
// B7 steps to the next capture. Swapping one in calls load_weights(),
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

static volatile Xf  g_xf      = Xf::Run;
// A slot is a bank and an index, packed as bank*100 + index. -1 = the
// compiled-in fallback.
static volatile int g_want    = -1;  // slot B7 is asking for
static volatile int g_active  = -1;  // slot currently loaded
static float        g_xf_gain = 1.f;

enum class Bank
{
    Amps    = 0,  // the card's captures, as captured
    NotAmps = 1,  // captures bent inside the network, with a steer control
    Filters = 2,  // GRU filters off the card, with cutoff and resonance
};
static volatile Bank g_bank = Bank::Amps;

static int  Slot(Bank b, int i) { return (int)b * 100 + i; }
static Bank SlotBank(int slot)
{
    return slot >= 200 ? Bank::Filters : (slot >= 100 ? Bank::NotAmps : Bank::Amps);
}
static int  SlotIndex(int slot) { return slot % 100; }
// Not-amps are built from the card's captures, so with no card there are none.
static int BankSize(Bank b)
{
    if(b == Bank::Filters)
        return captures::FilterCount();
    return b == Bank::Amps ? captures::Count() : (captures::Count() > 0 ? notamps::kCount : 0);
}

// B7's long press: AMPS -> NOT-AMPS -> FILTERS, skipping FILTERS on a card with none.
static Bank NextBank(Bank b)
{
    if(b == Bank::Amps)
        return Bank::NotAmps;
    if(b == Bank::NotAmps && captures::FilterCount() > 0)
        return Bank::Filters;
    return Bank::Amps;
}

// Where B7 has stepped to in each bank, so going round the banks comes back to the same
// patch. Ask for that patch; nothing to ask for in an empty bank.
static int g_cursor[3] = {0, 0, 0};
static void Select(Bank b)
{
    const int n = BankSize(b);
    if(n <= 0)
        return;
    int& c = g_cursor[(int)b];
    if(c >= n)
        c = 0;
    g_want = Slot(b, c);
}

// The steer control, 0..1: knob 2 plus the CV_6 jack, summed like the other
// pairs. Smoothed over ~20 ms so a knob or CV does not step audibly.
static volatile float g_steer = 0.f;

// The loaded not-amp: its source weights (A, and B for a morph), the weights
// being played, and the capture gain its level table is relative to. Ordinary
// .bss, DTCMRAM under BOOT_SRAM. Touched by the main loop only while muted
// (Swap), and by the callback otherwise.
static float g_na_a[nam_a2_daisy::kA2WeightCount];
static float g_na_b[nam_a2_daisy::kA2WeightCount];
static float g_na_w[nam_a2_daisy::kA2WeightCount];
static int   g_na      = -1;   // which not-amp is loaded, -1 = none
static float g_na_gain = 1.f;  // the reference capture's header gain

// ~5ms at 48kHz/48, which is long enough not to click and short enough not to
// feel like a gap when auditioning captures back to back.
static constexpr float kXfStep = 0.1f;

// Weights land here on the way from the card to the engine. ~7.3 KB in
// ordinary .bss (DTCMRAM under BOOT_SRAM, which has room), touched only while
// muted.
static float g_weight_buf[nam_a2_daisy::kA2WeightCount];


static constexpr float kLedVolts = 2.0f;

static inline void SetLed(bool on)
{
    patch.WriteCvOut(CV_OUT_2, on ? kLedVolts : 0.0f);
}


// ================================================================
// Controls
// ================================================================
static constexpr uint32_t kBankPressMs = 1500;  // B7 held: change bank, both builds

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

    if(dur >= kBankPressMs)
    {
        g_bank = NextBank(g_bank);
        Select(g_bank);
    }
    else
    {
        // The next patch in the bank. From a bank whose patch failed to load (the
        // display says so), this moves past the one that failed.
        const int n = BankSize(g_bank);
        if(n > 0)
        {
            int& c = g_cursor[(int)g_bank];
            c      = (c + 1) % n;
            Select(g_bank);
        }
    }
    g_display_dirty = true;
}

// ================================================================
// Not-amps
// ================================================================
// Set the loaded not-amp's bend and level for steer u. Audio callback only, so
// it never overlaps the engine running.
static void ApplySteer(float u)
{
    const float lg = g_proc.Apply(u, g_na_w, [](const float* w) { engine.RewriteWeights(w); });
    engine.SetGain(g_na_gain * lg);
}

// One block through the FILTERS network, in place: the cutoff ramped across the block
// from where the last one ended, Res smoothed over about 20 ms. Then the layer, if the
// filter has one: its output is the network's, 53.7 ms late (the dry side is delayed to
// match in the mix). restart: the network has not run since the last call (a swap, or
// another bank playing), so the layer drops what it was holding. Audio callback only.
//
// The Res dial goes to 11 (David, 2026-10-22). The Wasp's networks under-learned its
// resonance from about 7 up, so the dial is mapped, measured against the real Wasp's marks
// (the resonant band's rise from Res 6 on its test take: 7 +0.7, 8 +2.0, 9 +4.5, 10 +6.4
// dB; the network unlifted +0.6, +1.1, +1.6, +1.9):
//   - to 7 as marked; 7 to 9 the network's Res lifted 7 to 12, past which its resonance
//     falls away instead of rising (+2.5 dB at 12, +1.0 at 16, -1.9 at 18);
//   - from 9 its own output fed back into its input, 0.15 at 10 and kFb's 0.5 at 11: the
//     loop gain it under-learned. Mapped, the rise is +0.6, +1.8, +2.5, +4.6 dB at 7 to 10.
// The feedback makes it ring, then oscillate, first at the highest cutoffs and further
// down as the dial rises (at 10 from about 4 V, mostly within a tenth of where the Wasp
// rings up to 8 V, lower than it above). The real Wasp never oscillates: from about 9.5 the
// network does, at some cutoffs. 10 to 11 is over-Res. A GRU's output is bounded, so the
// loop cannot run away. A network trained at one Res (no Res input) gets the feedback
// alone. Measured with tools/notamp_spectra.py and the Res calibration of 2026-10-22.
//
// That table is the Wasp LP's. Each filter file may carry its own, with its cutoff range
// (the controls section, tools/grnb_controls.py; the Wasp HP's cutoff runs -10 to 0 V,
// Swords' Res is volts 0-4), calibrated against its own module with
// tools/filter_dial_calibrate.py; a file without one gets the Wasp LP's below. A version-2
// section also maps the dial to an oscillator layer's Res CV (FLT6: 10-11 is 4-6 V, past
// the network's 4).
static constexpr float kResDial = 11.f;
static constexpr captures::FilterControls kLpControls = {
    0.f, 10.f, 5, {0.f, 7.f, 9.f, 10.f, 11.f}, {0.f, 7.f, 12.f, 12.f, 12.f}, {0.f, 0.f, 0.f, 0.15f, 0.5f},
    false, {}, {}};
// The loaded filter's: written by the main loop only while the filter is silent (Swap).
static captures::FilterControls g_ctl = kLpControls;

static float DialMap(const float* table, float dial)
{
    const captures::FilterControls& c = g_ctl;
    if(dial <= c.dial[0])
        return table[0];
    for(int i = 1; i < c.points; i++)
        if(dial <= c.dial[i])
            return table[i - 1] + (table[i] - table[i - 1]) * (dial - c.dial[i - 1]) / (c.dial[i] - c.dial[i - 1]);
    return table[c.points - 1];
}

static void ProcessFilter(float* buf, size_t n, float cut_to, float res_to, bool restart)
{
    static float cut = 0.f, res = 0.f, prev = 0.f;
    const uint32_t t0 = System::GetTick();
    const bool     layer = g_layer_on;
    const bool     osc_on = g_osc_on;
    if(restart)
        prev = 0.f;
    if(layer && restart)
        g_layer.Restart();
    if(osc_on && restart)
        g_osc.Reset(t0 | 1u);
    res += 0.05f * (res_to - res);
    const float res_net = DialMap(g_ctl.res, res);
    // The feedback in the network's own units, as calibrated: the file's audio scale (the
    // level match, 0.666) multiplies the whole audio input, so the fed-back part is divided
    // by it first.
    float a_scale = 1.f;
    for(int k = 0; k < g_filter.inputs; k++)
        if(g_filter.kind[k] == gru::kAudio && g_filter.scale[k] > 0.f)
            a_scale = g_filter.scale[k];
    const float fb = DialMap(g_ctl.fb, res) / a_scale;
    const float res_lay = res < 10.f ? res : 10.f;  // the layer follows the Wasp's own marks
    const float res_osc = osc_on ? DialMap(g_ctl.osc_res, res) : 0.f;  // volts of Res CV
    float raw[gru::kMaxInputs];
    for(size_t i = 0; i < n; i++)
    {
        const float c = cut + (cut_to - cut) * (float)(i + 1) / (float)n;
        const float a = buf[i] + fb * prev;
        for(int k = 0; k < g_filter.inputs; k++)
            raw[k] = g_filter.kind[k] == gru::kAudio ? a : (g_filter.kind[k] == gru::kCv ? c : res_net);
        float s = gru::Step(g_filter, g_filter_state, raw);
        if(!std::isfinite(s))
        {
            s = 0.f;
            g_filter_state.Reset();
        }
        prev        = s;  // fed back before the file's output gain, as on the desktop
        if(osc_on)  // in the network's units: its audio input after the file's scale, as on the desktop
            s += g_osc.Process(a * a_scale, c, res_osc);
        const float y = s * g_filter_gain;
        buf[i] = layer ? g_layer.Process(y) : y;
    }
    if(layer)
        g_layer.Advance(g_layer_def, cut_to, res_lay);
    cut = cut_to;
    // The cost, measured around it rather than estimated: the worst block, easing back.
    const float cycles = (float)(System::GetTick() - t0) * (480e6f / (float)System::GetTickFreq()) / (float)n;
    g_gru_cycles = cycles > g_gru_cycles ? cycles : g_gru_cycles + 0.001f * (cycles - g_gru_cycles);
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

    // Pot + CV jack summed and clamped, the house attenuverter behaviour, as in
    // daisy_multifx_oled. The knobs read 0..1 across their travel (confirmed on the
    // v1.0 bench); the jacks read -1..1 for -5..+5 V. Knobs 2 and 3 mean different
    // things by bank, so they follow what is playing, not what is asked for.
    const bool  filt = SlotBank(g_active) == Bank::Filters;
    const float k2 = patch.GetAdcValue(CV_2), j2 = patch.GetAdcValue(CV_6);
    const float k3 = patch.GetAdcValue(CV_3), j3 = patch.GetAdcValue(CV_7);
    const float k4 = patch.GetAdcValue(CV_4), j4 = patch.GetAdcValue(CV_8);
    const float mix = fclamp(patch.GetAdcValue(CV_1) + patch.GetAdcValue(CV_5), 0.f, 1.f);

    // DRIVE, every bank: -20..+20 dB into the network with unity at noon, so the signal
    // can be set to the level the capture was trained at, and past it. For most
    // captures this changes the sound far more than the level (measured 2026-10-19),
    // and for the filters too: the Wasp's GRU-16 goes from clean (third harmonic -71 dB
    // at 0.5 V peak) to saturating (-16 dB at 9 V) as the input rises (2026-10-21).
    const float drive_k = fclamp(k4 + j4, 0.f, 1.f);
    const float drive   = powf(10.f, (drive_k * 2.f - 1.f));
    // FREQ and RES (FILTERS), clamped to the ranges the networks learned.
    // The knob across the filter's own cutoff range, the jack adding its volts; clamped to
    // the range the network learned.
    const float lo = g_ctl.cut_lo, hi = g_ctl.cut_hi;
    const float cut_to = fclamp(lo + (hi - lo) * k2 + 5.f * j2, lo < hi ? lo : hi, lo < hi ? hi : lo);
    const float res_to = kResDial * fclamp(k3 + j3, 0.f, 1.f);  // 0..11: past 10 is over-Res
    g_mix      = mix;
    g_drive_db = 20.f * (drive_k * 2.f - 1.f);
    g_cut      = 10.f * (cut_to - lo) / (hi - lo);  // shown as the knob's 0-10, whatever the volts
    g_res      = res_to;

    // --- steer ------------------------------------------------------------
    const float steer_raw = fclamp(k2 + j2, 0.f, 1.f);
    g_steer += 0.05f * (steer_raw - g_steer);
    if(g_na >= 0 && g_xf != Xf::Swap)
        ApplySteer(g_steer);

    // Drive the crossfade. The load itself happens in the main loop; all the
    // callback does is get the output to silence first and pick it up after.
    switch(g_xf)
    {
        case Xf::Run:
            if(g_want != g_active)
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

    // Metering and drive first, over the whole block, because the engine wants
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

        scratch[i] = x * drive;
    }

    // Not while the main loop is swapping: it is rewriting this engine's weights
    // and state (and, for a seed, probing it for auto-gain), and running the
    // network from here at the same time feeds it half-written numbers. The
    // output is muted during Swap anyway. Found 2026-09-28: a seed could go NaN
    // this way, and the DC blocker below then held the NaN forever.
    static bool filter_ran = false;  // last block, for the layer's restart
    if(g_xf != Xf::Swap)
    {
        if(filt)
            ProcessFilter(scratch, n, cut_to, res_to, !filter_ran);
        else
            engine.ProcessBlock(scratch, scratch, n);
        filter_ran = filt;
    }
    else
    {
        for(size_t i = 0; i < n; i++)
            scratch[i] = 0.f;  // no wet while swapping
        filter_ran = false;
    }

    // DC blocker, ~20Hz one-pole. Needed for the seed slot, which is
    // asymmetric and parks on an offset nothing has trained out (measured: RMS
    // 20.4 of which 20.4 was DC, with perfectly good audio underneath). Even a
    // trained capture shows a small offset, and daisy_multifx_oled blocks DC in
    // its output stage for the same reason: DC into a rack is nobody's friend.
    static float dcb_x1 = 0.f, dcb_y1 = 0.f;
    constexpr float kDcR = 0.99738f;  // exp(-2*pi*20/48000)

    // Dry/wet. The dry side is the input before the drive, delayed to line up with
    // a not-amp that has latency (RATE runs 48·R samples behind), or with a filter's
    // granular layer (kDelay, 2,576 samples, at any mix, so A/B on the mix keeps its
    // timing); the swap fade mutes the wet side only.
    static notamps::DryDelay dry_delay;
    const int   lat     = g_na >= 0 ? g_proc.Latency() : 0;
    const bool  lay_dry = filt && g_layer_on;
    const float wetg = mix * g_xf_gain, dryg = 1.f - mix;
    for(size_t i = 0; i < n; i++)
    {
        const float d   = lay_dry ? g_layer.DelayDry(in[0][i]) : dry_delay.Process(in[0][i], lat);
        const float raw = scratch[i] * wetg + d * dryg;
        float       y   = raw - dcb_x1 + kDcR * dcb_y1;
        // A recursive filter keeps a NaN or Inf forever, so one bad block from
        // an untrained network would silence the module until power-off.
        if(!std::isfinite(y))
        {
            y = 0.f;
            dcb_x1 = dcb_y1 = 0.f;
        }
        else
        {
            dcb_x1 = raw;
            dcb_y1 = y;
        }
        out[0][i]       = y;
        out[1][i]       = y;
    }

    // Only reachable if the block size is ever configured above 48; the engine
    // cannot help there, so the tail passes through dry rather than going quiet.
    for(size_t i = n; i < size; i++)
    {
        out[0][i] = in[0][i];
        out[1][i] = in[0][i];
    }

    g_in_peak = peak;

    cpu_meter.OnBlockEnd();
}

// ================================================================
// Capture swapping (main loop only)
// ================================================================
// Reads a capture off the card and hands it to the engine. Both halves are
// slow — a file read, then prewarm() over the whole network — so this must
// only ever run with the output muted, which is what Xf::Swap guarantees.
static bool SwapCapture(int index)
{
    const captures::Entry* e = captures::Get(index);
    if(!e)
        return false;

    if(!captures::Load(index, g_weight_buf, nam_a2_daisy::kA2WeightCount))
        return false;
    g_na = -1;
    g_proc.End();                  // a real amp is played exactly as captured
    if(!engine.Load(g_weight_buf, (size_t)e->weight_count, e->gain, e->name))
        return false;
    return true;
}

// Build a not-amp from the captures it names, then load and prewarm it at the
// current steer position. Main loop only, while muted.
static bool SwapNotAmp(int i)
{
    using namespace nam_a2_daisy;
    const notamps::Def& d = notamps::kDefs[i];
    const int ia = captures::Find(d.cap_a);
    const int ir = captures::Find(d.cap_ref);
    if(ia < 0 || ir < 0)
        return false;
    if(!captures::Load(ia, g_na_a, kA2WeightCount))
        return false;
    if(d.cap_b != nullptr)
    {
        const int ib = captures::Find(d.cap_b);
        if(ib < 0 || !captures::Load(ib, g_na_b, kA2WeightCount))
            return false;
    }

    g_na = -1;  // nothing in the callback touches the engine until this is set
    g_proc.Begin(d, g_na_a, d.cap_b != nullptr ? g_na_b : nullptr);
    g_na_gain = captures::Get(ir)->gain;
    if(!engine.Load(g_proc.InitialWeights(g_steer, g_na_w), kA2WeightCount, g_na_gain, d.name))
    {
        g_proc.End();
        return false;
    }
    g_na = i;
    return true;
}

// Load a FILTERS network off the card. gru::Parse checks the magic, the sizes and the
// CRC before anything is copied, so a bad file leaves the loaded network alone. Main
// loop only, while muted.
static bool SwapFilter(int index)
{
    const uint8_t* data  = nullptr;
    size_t         bytes = 0;
    float          gain  = 1.f;
    char           name[gru::kGrnbNameLen];
    // A version-2 file's layer first, into SDRAM (about half a second for the Wasp's
    // 1.9 MB). The old filter's layer is switched off before its bank is overwritten; a
    // file whose layer will not load or parse is refused whole.
    const captures::FilterEntry* e = captures::GetFilter(index);
    g_layer_on = false;
    g_osc_on   = false;
    if(e && e->layer_bytes > 0)
    {
        size_t got = 0;
        if(!captures::LoadFilterLayer(index, g_layer_buf, kLayerCap, &got) || !gran::Parse(g_layer_buf, got, g_layer_def))
            return false;
    }
    if(!captures::LoadFilterFile(index, &data, &bytes) || !gru::Parse(data, bytes, g_filter, gain, name))
        return false;
    // Its controls (after the parse: both reads use the card code's one buffer); the Wasp
    // LP's when it has none.
    captures::FilterControls c;
    g_ctl = captures::LoadFilterControls(index, c) ? c : kLpControls;
    if(g_ctl.osc)
    {
        osc::Params p;
        osc::FromFloats(g_ctl.osc_p, p);
        g_osc.Init(p, patch.AudioSampleRate());
        g_osc.Reset(System::GetTick() | 1u);
        g_osc_on = true;
    }
    g_na = -1;
    g_proc.End();
    g_filter_gain = gain;
    snprintf(g_filter_name, sizeof(g_filter_name), "%s", name);
    g_filter_state.Reset();
    if(e && e->layer_bytes > 0)
    {
        g_layer.Reset(System::GetTick() | 1u);
        g_layer_on = true;
    }
    return true;
}

static bool SwapSlot(int slot)
{
    const Bank b  = SlotBank(slot);
    const bool ok = b == Bank::Amps      ? SwapCapture(SlotIndex(slot))
                    : b == Bank::NotAmps ? SwapNotAmp(SlotIndex(slot))
                                         : SwapFilter(SlotIndex(slot));
    if(ok)
        g_active = slot;
    return ok;
}

// ================================================================
// Display
// ================================================================
#if !MIRTH_LITE
static void DrawRunPage()
{
    char line[32];

    // Six rows of 8 px (the font is 7 high): name, meter, CPU, patch, then the two knob
    // lines. Ten characters a row.
    const bool filt = SlotBank(g_active) == Bank::Filters;
    display.DrawString(0, 0, filt ? g_filter_name : engine.Name(), false);

    // Input peak meter. The point of it is setting the drive, so it reads the
    // input before the drive rather than after.
    const float peak = fclamp(g_in_peak, 0.f, 1.f);
    const uint8_t w  = (uint8_t)(peak * 62.f + 0.5f);
    display.DrawRect(0, 9, 64, 6, true);
    if(w > 0)
        display.FillRect(1, 10, w > 62 ? 62 : w, 4, true);

    // CPU load, average with the peak in brackets: "CPU 64(99)". David asked for
    // "CPU 64% (99%)", but at six pixels a character the panel holds ten, so the
    // percent signs go. Both clamped to two digits so the line never overruns.
    const int cpu_avg = (int)(cpu_meter.GetAvgCpuLoad() * 100.f + 0.5f);
    const int cpu_max = (int)(cpu_meter.GetMaxCpuLoad() * 100.f + 0.5f);
    if(filt)  // on a filter: the load, and the network's (and layer's) worst block in cycles per sample
        snprintf(line, sizeof(line), "%2d%% %5dc", cpu_avg > 99 ? 99 : cpu_avg, (int)g_gru_cycles);
    else
        snprintf(line, sizeof(line), "CPU %2d(%2d)", cpu_avg > 99 ? 99 : cpu_avg, cpu_max > 99 ? 99 : cpu_max);
    display.DrawString(0, 16, line, false);

    // Which patch, and how many in its bank, since B7 steps round them: "FLT 3/3".
    const int raw_n  = captures::Count();
    const int n_caps = raw_n > 99 ? 99 : raw_n;
    const int k      = SlotIndex(g_active) + 1 > 99 ? 99 : SlotIndex(g_active) + 1;
    const int n_act  = BankSize(SlotBank(g_active)) > 99 ? 99 : BankSize(SlotBank(g_active));
    if(filt)
        snprintf(line, sizeof(line), "FLT %d/%d", k, n_act);
    else if(g_active >= 100)
        snprintf(line, sizeof(line), "NT %d/%d", k, n_act);
    else if(g_bank == Bank::NotAmps && n_caps > 0)
        // Asked for the not-amps but still on an amp: a not-amp failed to load,
        // usually because a capture it is built from is missing from the card.
        snprintf(line, sizeof(line), "N need cap");
    else if(g_bank == Bank::Filters)
        snprintf(line, sizeof(line), "F bad file");  // asked for a filter, none loaded
    else if(n_caps > 0 && g_active >= 0)
        snprintf(line, sizeof(line), "AMP %d/%d", k, n_act);
    else if(n_caps > 0)
        // Card present but the fallback is playing: a load failed. The name line
        // already carries the trailing * that marks the compiled-in one.
        snprintf(line, sizeof(line), "AMP -");
    else
        // With no card this shows the reason, so "mount" or "no captures" reads as a
        // card problem rather than a dead engine.
        snprintf(line, sizeof(line), "%.10s", captures::Status());
    display.DrawString(0, 24, line, false);

    // The knobs as they sit on the panel, two rows of two: knob 1 (mix) and 2 (freq or
    // steer) on the top line, knob 3 (res) and 4 (drive) on the bottom, the right-hand
    // knob's value at x = 34 so the two columns line up. A knob that does nothing in
    // this bank shows nothing.
    char right[12];
    snprintf(line, sizeof(line), "M%d", (int)(g_mix * 100.f + 0.5f));
    if(filt)
        snprintf(right, sizeof(right), "F%.1f", (double)g_cut);
    else if(g_active >= 100)
        // A not-amp: where its steer sits, since the steer is its whole character.
        snprintf(right, sizeof(right), "S%.2f", (double)g_steer);
    else
        right[0] = '\0';
    display.DrawString(0, 32, line, false);
    display.DrawString(34, 32, right, false);

    line[0] = '\0';
    if(filt)
    {
        bool has_res = false;
        for(int i = 0; i < g_filter.inputs; i++)
            has_res |= g_filter.kind[i] == gru::kRes;
        if(has_res || DialMap(g_ctl.fb, g_res) > 0.f)  // the dial, to 11; with feedback even FLT1
            snprintf(line, sizeof(line), "R%.1f", (double)g_res);
        else
            snprintf(line, sizeof(line), "R6*");  // trained at one Res only
    }
    snprintf(right, sizeof(right), "D%+d", (int)lroundf(g_drive_db));
    display.DrawString(0, 40, line, false);
    display.DrawString(34, 40, right, false);
}

static void UpdateDisplay()
{
    display.Clear();
    DrawRunPage();
    display.Update();
}
#else
// MIRTH LITE's only display. A blink request becomes a pattern — the bank as one to
// three quick flickers (AMPS, NOT-AMPS, FILTERS), a pause, then the patch's number, a
// long blink per five and a short per one — after which the LED is lit again.
static void RenderLed(uint32_t now)
{
    static uint16_t pat[48];  // on/off durations, ms
    static int      n = 0, i = 0;
    static uint32_t next = 0;

    if(captures::Count() == 0)  // nothing to play: flicker, and never stop
    {
        SetLed((now / 100) & 1);
        return;
    }
    const int req = g_blink_request;
    if(req >= 0)
    {
        g_blink_request = -1;
        n = 0;
        if(req == 0)  // the fallback (none loaded off the card): one blip
        {
            pat[n++] = 40;
            pat[n++] = 400;
        }
        else
        {
            const int bank = (req - 1) / 100, num = (req - 1) % 100 + 1;
            for(int k = 0; k <= bank; k++) { pat[n++] = 50; pat[n++] = k < bank ? 120 : 600; }
            for(int k = 0; k < num / 5 && n < 46; k++) { pat[n++] = 600; pat[n++] = 250; }
            for(int k = 0; k < num % 5 && n < 46; k++) { pat[n++] = 140; pat[n++] = 160; }
        }
        pat[n++] = 0;  // end marker pair: pause before the steady state
        pat[n++] = 500;
        i    = 0;
        next = now;
        SetLed(false);
    }
    if(i < n)
    {
        if((int32_t)(now - next) >= 0)
        {
            SetLed((i % 2) == 0 && pat[i] > 0);
            next = now + pat[i];
            i++;
        }
        return;
    }
    SetLed(true);
}
#endif

// ================================================================
// .nam import at boot (captures::ImportNam, nam_import.h)
// ================================================================
// A .nam converted at boot is levelled as tools/level_captures.py levels it: the
// not-amps' phrase through the engine itself, A-weighted. The engine is free then, before
// audio starts; main() reloads the fallback afterwards. The phrase (768 KB) is made on
// the first import only.
alignas(32) static float DSY_SDRAM_BSS g_phrase[nam_import::kPhraseLen];
static double DSY_SDRAM_BSS            g_phrase_table[nam_import::kTableLen];

static float LevelCapture(const float* weights)
{
    static bool made = false;
    if(!made)
    {
        nam_import::MakePhrase(g_phrase, g_phrase_table);
        made = true;
    }
    if(!engine.Load(weights, nam_a2_daisy::kA2WeightCount, 1.f, "LEVEL"))
        return NAN;
    nam_import::ALevel level;
    alignas(32) float  out[kA2BlockSize];
    for(size_t i = 0; i < nam_import::kPhraseLen; i += kA2BlockSize)
    {
        engine.ProcessBlock(g_phrase + i, out, kA2BlockSize);
        level.Add(out, kA2BlockSize);
    }
    return (float)level.Db();
}

#if !MIRTH_LITE
static void ImportProgress(int done, int total, const char* file)
{
    char line[24];
    display.Clear();
    display.DrawStringCentered(4, "IMPORT", false);
    snprintf(line, sizeof(line), "%d/%d", done < total ? done + 1 : total, total);
    display.DrawStringCentered(18, line, false);
    snprintf(line, sizeof(line), "%.10s", file);  // the file's name, as far as it fits
    display.DrawStringCentered(32, line, false);
    display.Update();
}
#endif

// ================================================================
// main
// ================================================================
int main(void)
{
    patch.Init();
    const float sr = patch.AudioSampleRate();

    nav_btn.Init(patch.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);
    // B8, the toggle, is unused: Lite took its bank from it until 2026-10-21.

    patch.StartDac();

    engine.InitFallback(sr);
    cpu_meter.Init(sr, patch.AudioBlockSize());
    g_layer.Init();  // its FFT tables, once
    g_layer.Reset(1);

#if !MIRTH_LITE
    display.Init(patch.A2, patch.A3);  // first, for the import's progress
    constexpr captures::ProgressFn kProgress = ImportProgress;
#else
    constexpr captures::ProgressFn kProgress = nullptr;
#endif

    // Card before audio starts, so the first capture is in place by the time
    // anything is heard. A missing or unreadable card is not fatal: the
    // compiled-in fallback is already loaded and captures::Status() says why.
    // Any .nam on the card is imported first, using the engine to level it, so
    // the fallback is reloaded after. Both start on the first amp; Lite blinks it.
    captures::ImportResult imported;
    captures::Init(LevelCapture, kProgress, &imported);
    if(imported.added + imported.refused > 0)
        engine.InitFallback(sr);
#if !MIRTH_LITE
    if(imported.added + imported.refused > 0)
    {
        // What the import did, for long enough to read: how many added and refused,
        // and the last refusal's file and reason. The .nam files are in nam_done/ and
        // nam_refused/ on the card.
        char line[24];
        display.Clear();
        snprintf(line, sizeof(line), "NAM +%d", imported.added);
        display.DrawStringCentered(4, line, false);
        if(imported.refused > 0)
        {
            snprintf(line, sizeof(line), "REFUSED %d", imported.refused);
            display.DrawStringCentered(16, line, false);
            snprintf(line, sizeof(line), "%.10s", imported.last_refused);
            display.DrawStringCentered(28, line, false);
            snprintf(line, sizeof(line), "%.10s", imported.last_why);
            display.DrawStringCentered(40, line, false);
        }
        display.Update();
        System::Delay(imported.refused > 0 ? 4000 : 1500);
    }
#endif
    if(captures::Count() > 0)
    {
        Select(g_bank);
        SwapSlot(g_want);
        g_want = g_active;
    }
#if MIRTH_LITE
    g_blink_request = g_active + 1;
#endif

#if !MIRTH_LITE
    display.Clear();
    display.DrawStringCentered(4, "MIRTH", false);
    display.DrawStringCentered(40, MIRTH_BUILD, false);  // the git commit this was built from
    display.DrawStringCentered(18, engine.Name(), false);
    {
        char line[24];
        snprintf(line, sizeof(line), "SD %d F%d", captures::Count(), captures::FilterCount());
        display.DrawStringCentered(32, line, false);
    }
    display.Update();
    System::Delay(1000);
#endif

    SetLed(true);

    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    uint32_t next_frame = 0;
    while(1)
    {
        // The callback has faded to silence and is waiting on us. Do the slow
        // work here — file read plus prewarm() — then hand it back to fade in.
        if(g_xf == Xf::Swap)
        {
            // Failed: keep whatever is loaded, and stop asking, unless B7 has
            // already asked for another. Usually a not-amp whose source capture
            // is not on the card.
            const int want = g_want;
            if(!SwapSlot(want) && g_want == want)
                g_want = g_active;
            g_xf            = Xf::FadeIn;
            g_display_dirty = true;
#if MIRTH_LITE
            g_blink_request = g_active + 1;  // the bank and number now playing
#endif
        }

        const uint32_t now = System::GetNow();
#if MIRTH_LITE
        (void)next_frame;
        RenderLed(now);
#else
        if(now >= next_frame || g_display_dirty)
        {
            next_frame      = now + 50;  // 20 fps; the soft I2C write is slow
            g_display_dirty = false;
            UpdateDisplay();
        }
#endif
    }
}
