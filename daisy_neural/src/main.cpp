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
 * Controls (knobs 1-4 set by David, 2026-10-19; each knob summed with the CV jack under it)
 *                        AMPS / NOT-AMPS                  FILTERS
 *   CV_1 (+ CV_5 jack)   PATCH: which capture or not-amp  PATCH: which filter
 *   CV_2 (+ CV_6 jack)   MIX, dry (0) to wet (1)          MIX
 *   CV_3 (+ CV_7 jack)   DRIVE, -20..+20 dB into the      CUTOFF: the knob 0..10 V, the jack
 *                        engine, unity at noon            adds its volts (like the Wasp's CV 1)
 *   CV_4 (+ CV_8 jack)   STEER (not-amps only)            RESONANCE, 0..10 on the Wasp's dial
 *   B7 short press       bypass on/off, to A/B the engine against the dry input
 *   B7 held 1.5 s        next bank: AMPS -> NOT-AMPS -> FILTERS (when the card has any)
 *   CV_OUT_2 LED         lit when the engine is in circuit, dark when bypassed
 *
 * MIRTH LITE (make LITE=1, for a stock patch.init with no OLED):
 *   B8 toggle            bank: up AMPS, down NOT-AMPS (checked on a stock unit, 2026-10-03)
 *   B7 short press       bypass on/off
 *   B7 held 0.6 s        re-blink the current slot number
 *   CV_OUT_2 LED         the slot number blinks when the slot or bank changes —
 *                        a long blink is five, so 12 is long-long-short-short —
 *                        then lit (engine in) or dark (bypassed). A fast flicker
 *                        that never stops: no captures on the card.
 *
 * Three banks. FILTERS: GRU networks off the card (.grnb), each a filter with its
 * cutoff and resonance as live controls (nam/gru_runtime.h; the first are the
 * Doepfer A-124 Wasp's, CLAUDE.md). A version-2 file also carries a granular layer
 * (nam/gran_layer.h): the Wasp's take-to-take randomness, phased in from Res 8 to 9,
 * at 46.7 ms of latency for that slot, wet and dry alike. Mirth Lite has no way to reach FILTERS yet: its
 * B8 toggle has two positions. AMPS: the captures on the card, played exactly as captured; the
 * steer knob does nothing there, on purpose. NOT-AMPS: twelve starter captures,
 * each with one transform chosen by measurement to leave the region where real
 * amps sit — sine or linear neurons, a feedback loop, a frozen layer, the engine
 * at a fraction of the sample rate, or a capture pushed past another — and one
 * steer control that can move while playing. Processing in src/notamp_dsp.h
 * (shared with the Mac harness tools/a2_notamp.cpp); definitions and level
 * tables generated into src/notamps.h by tools/notamp_design.py.
 *
 * Audio: IN_L -> drive -> engine slot -> wet; IN_L -> dry (delayed to match any
 * not-amp latency); dry/wet mix (CV_2) -> DC block -> OUT_L and OUT_R.
 * Bypass is the dry side alone, so an A/B compares the engine with its input.
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
// Wasp's take-to-take randomness, phased in above Res 8 (nam/gran_layer.h).
#include "nam/gran_layer.h"
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
#else
Switch        bank_toggle;  // B8
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

static bool         g_bypass        = false;
static bool         g_display_dirty = true;

// Metering, written in the audio ISR and read by the UI loop. Single writer,
// single reader, and a torn float here would only flicker one frame.
static volatile float g_in_peak = 0.f;  // block peak, |x|, for the RUN meter

// MIRTH LITE's LED: a blink request (the slot number), set from the callback or the
// main loop and rendered by the main loop.
static volatile int g_blink_request = -1;

// What the knobs are set to, for the RUN page.
static volatile float g_mix      = 0.f;
static volatile float g_drive_db = 0.f;
static volatile float g_cut      = 0.f;  // FILTERS: volts
static volatile float g_res      = 0.f;  // FILTERS: the Res dial
// FILTERS: the network's cost (and its layer's), CPU cycles per sample: the worst block,
// easing back over about a second. A layer does a third of a frame's work in some blocks
// and none in others, so the worst block, not the average, is what decides a glitch.
static volatile float g_gru_cycles = 0.f;

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

static volatile Xf  g_xf      = Xf::Run;
// A slot is a bank and an index, packed as bank*100 + index. -1 = the
// compiled-in fallback.
static volatile int g_want    = 0;   // slot CV_3 is asking for
static volatile int g_active  = -1;  // slot currently loaded
static volatile int g_bad     = -2;  // a slot that failed to load; not retried until the knob moves off it
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

// The steer control, 0..1: knob 4 plus the CV_8 jack, summed like the other
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
static constexpr uint32_t kLongPressMs = 600;   // LITE: re-blink
static constexpr uint32_t kBankPressMs = 1500;  // OLED build: change bank

static bool     g_btn_held        = false;
static uint32_t g_btn_press_start = 0;

static void ProcessNav()
{
    nav_btn.Debounce();
    const uint32_t now = System::GetNow();
#if MIRTH_LITE
    // The bank follows the toggle, live.
    bank_toggle.Debounce();
    g_bank = bank_toggle.Pressed() ? Bank::NotAmps : Bank::Amps;
#endif

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

#if MIRTH_LITE
    if(dur >= kLongPressMs)
        g_blink_request = SlotIndex(g_active) + 1;
    else
#else
    if(dur >= kBankPressMs)
        g_bank = NextBank(g_bank);
    else
#endif
    {
        g_bypass = !g_bypass;
        SetLed(!g_bypass);
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
// filter has one: its output is the network's, 46.7 ms late (the dry side is delayed to
// match in the mix). restart: the network has not run since the last call (bypass, a
// swap), so the layer drops what it was holding. Audio callback only.
static void ProcessFilter(float* buf, size_t n, float cut_to, float res_to, bool restart)
{
    static float cut = 0.f, res = 0.f;
    const uint32_t t0 = System::GetTick();
    const bool     layer = g_layer_on;
    if(layer && restart)
        g_layer.Restart();
    res += 0.05f * (res_to - res);
    float raw[gru::kMaxInputs];
    for(size_t i = 0; i < n; i++)
    {
        const float c = cut + (cut_to - cut) * (float)(i + 1) / (float)n;
        for(int k = 0; k < g_filter.inputs; k++)
            raw[k] = g_filter.kind[k] == gru::kAudio ? buf[i] : (g_filter.kind[k] == gru::kCv ? c : res);
        float y = gru::Step(g_filter, g_filter_state, raw) * g_filter_gain;
        if(!std::isfinite(y))
        {
            y = 0.f;
            g_filter_state.Reset();
        }
        buf[i] = layer ? g_layer.Process(y) : y;
    }
    if(layer)
        g_layer.Advance(g_layer_def, cut_to, res);
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
    // v1.0 bench); the jacks read -1..1 for -5..+5 V. Knobs 3 and 4 mean different
    // things by bank, so they follow what is playing, not what is asked for.
    const bool  filt = SlotBank(g_active) == Bank::Filters;
    const float k3 = patch.GetAdcValue(CV_3), j3 = patch.GetAdcValue(CV_7);
    const float k4 = patch.GetAdcValue(CV_4), j4 = patch.GetAdcValue(CV_8);
    const float mix = g_bypass ? 0.f : fclamp(patch.GetAdcValue(CV_2) + patch.GetAdcValue(CV_6), 0.f, 1.f);

    // DRIVE (AMPS, NOT-AMPS): -20..+20 dB into the engine with unity at noon, so the
    // signal can be set to the level the capture was trained at, and past it. For
    // most captures this changes the sound far more than the level (measured
    // 2026-10-19). FILTERS has no drive: the input goes in as it arrives.
    const float drive_k = fclamp(k3 + j3, 0.f, 1.f);
    const float drive   = filt ? 1.f : powf(10.f, (drive_k * 2.f - 1.f));
    // CUTOFF and RESONANCE (FILTERS), clamped to the ranges the networks learned.
    const float cut_to = fclamp(10.f * k3 + 5.f * j3, 0.f, 10.f);
    const float res_to = 10.f * fclamp(k4 + j4, 0.f, 1.f);
    g_mix      = mix;
    g_drive_db = 20.f * (drive_k * 2.f - 1.f);
    g_cut      = cut_to;
    g_res      = res_to;

    // --- slot selection ---------------------------------------------------
    // PATCH (knob 1, CV_1 + CV_5) across the current bank. Hysteresis of a quarter step past the
    // boundary, so a knob sitting on one does not reload the network every
    // block. Changing bank picks whatever the knob points at in the new bank.
    const Bank bank = g_bank;
    const int  n_bank = BankSize(bank);
    if(n_bank > 0)
    {
        const float sel  = fclamp(patch.GetAdcValue(CV_1) + patch.GetAdcValue(CV_5), 0.f, 1.f);
        const float span = 1.f / (float)n_bank;
        int         idx  = (int)(sel * (float)n_bank);
        if(idx >= n_bank)
            idx = n_bank - 1;
        const int cur = (SlotBank(g_want) == bank) ? SlotIndex(g_want) : -1;
        int want = g_want;
        if(cur < 0)
            want = Slot(bank, idx);
        else if(idx != cur && fabsf(sel - ((float)cur + 0.5f) * span) > span * 0.75f)
            want = Slot(bank, idx);
        if(want != g_bad)
            g_want = want;
        else if(SlotIndex(want) != idx)
            g_bad = -2;  // the knob has moved on; allow a retry next time
    }

    // --- steer ------------------------------------------------------------
    const float steer_raw = fclamp(k4 + j4, 0.f, 1.f);
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

        scratch[i] = x * drive;
    }

    // Not while the main loop is swapping: it is rewriting this engine's weights
    // and state (and, for a seed, probing it for auto-gain), and running the
    // network from here at the same time feeds it half-written numbers. The
    // output is muted during Swap anyway. Found 2026-09-28: a seed could go NaN
    // this way, and the DC blocker below then held the NaN forever.
    static bool filter_ran = false;  // last block, for the layer's restart
    if(!g_bypass && g_xf != Xf::Swap)
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
            scratch[i] = 0.f;  // no wet while bypassed or swapping
        filter_ran = false;
    }

    // DC blocker, ~20Hz one-pole. Needed for the seed slot, which is
    // asymmetric and parks on an offset nothing has trained out (measured: RMS
    // 20.4 of which 20.4 was DC, with perfectly good audio underneath). Even a
    // trained capture shows a small offset, and daisy_multifx_oled blocks DC in
    // its output stage for the same reason: DC into a rack is nobody's friend.
    static float dcb_x1 = 0.f, dcb_y1 = 0.f;
    constexpr float kDcR = 0.99738f;  // exp(-2*pi*20/48000)

    // Dry/wet. The dry side is the input before the trim, delayed to line up with
    // a not-amp that has latency (RATE runs 48·R samples behind), or with a filter's
    // granular layer (kDelay, 2,240 samples, bypassed or not, so A/B keeps its timing);
    // the swap fade mutes the wet side only.
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
    if(e && e->layer_bytes > 0)
    {
        size_t got = 0;
        if(!captures::LoadFilterLayer(index, g_layer_buf, kLayerCap, &got) || !gran::Parse(g_layer_buf, got, g_layer_def))
            return false;
    }
    if(!captures::LoadFilterFile(index, &data, &bytes) || !gru::Parse(data, bytes, g_filter, gain, name))
        return false;
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

    // Capture name, drawn inverted while bypassed — the panel has ten
    // characters and no room for a separate BYP flag once the capture index
    // is on screen. The LED says the same thing.
    const bool filt = SlotBank(g_active) == Bank::Filters;
    display.DrawString(0, 0, filt ? g_filter_name : engine.Name(), g_bypass);

    // Input peak meter. The point of it is setting the trim, so it reads the
    // input before the trim rather than after.
    const float peak = fclamp(g_in_peak, 0.f, 1.f);
    const uint8_t w  = (uint8_t)(peak * 62.f + 0.5f);
    display.DrawRect(0, 9, 64, 7, true);
    if(w > 0)
        display.FillRect(1, 10, w > 62 ? 62 : w, 5, true);

    // CPU load, average with the peak in brackets: "CPU 64(99)". David asked for
    // "CPU 64% (99%)", but at six pixels a character the panel holds ten, so the
    // percent signs go. Both clamped to two digits so the line never overruns.
    const int cpu_avg = (int)(cpu_meter.GetAvgCpuLoad() * 100.f + 0.5f);
    const int cpu_max = (int)(cpu_meter.GetMaxCpuLoad() * 100.f + 0.5f);
    if(filt)  // on a filter: the load, and the network's (and layer's) worst block in cycles per sample
        snprintf(line, sizeof(line), "%2d%% %5dc", cpu_avg > 99 ? 99 : cpu_avg, (int)g_gru_cycles);
    else
        snprintf(line, sizeof(line), "CPU %2d(%2d)", cpu_avg > 99 ? 99 : cpu_avg, cpu_max > 99 ? 99 : cpu_max);
    display.DrawString(0, 18, line, false);

    // The two knob lines sit as the knobs do on the panel: knobs 1 and 2 (patch, mix) on
    // the top row, so their line comes first; knobs 3 and 4 (drive and steer, or cutoff
    // and resonance) on the bottom row. Ten characters a line.
    const int mix_pc = (int)(g_mix * 100.f + 0.5f);
    const int raw_n  = captures::Count();
    const int n_caps = raw_n > 99 ? 99 : raw_n;
    const int k      = SlotIndex(g_active) + 1 > 99 ? 99 : SlotIndex(g_active) + 1;
    if(filt)
        snprintf(line, sizeof(line), "FLT%d M%d", k, mix_pc);
    else if(g_active >= 100)
        snprintf(line, sizeof(line), "NT%02d M%d", k, mix_pc);
    else if(g_bank == Bank::NotAmps && n_caps > 0)
        // Asked for the not-amps but still on an amp: a not-amp failed to load,
        // usually because a capture it is built from is missing from the card.
        snprintf(line, sizeof(line), "N need cap");
    else if(g_bank == Bank::Filters)
        snprintf(line, sizeof(line), "F bad file");  // asked for a filter, none loaded
    else if(n_caps > 0 && g_active >= 0)
        snprintf(line, sizeof(line), "AMP%d M%d", k, mix_pc);
    else if(n_caps > 0)
        // Card present but the fallback is playing: a load failed. The name line
        // already carries the trailing * that marks the compiled-in one.
        snprintf(line, sizeof(line), "AMP- M%d", mix_pc);
    else
        // With no card this shows the reason, so "mount" or "no captures" reads as a
        // card problem rather than a dead engine.
        snprintf(line, sizeof(line), "%.10s", captures::Status());
    display.DrawString(0, 26, line, false);

    const int drive = (int)lroundf(g_drive_db);
    if(filt)
    {
        bool has_res = false;
        for(int i = 0; i < g_filter.inputs; i++)
            has_res |= g_filter.kind[i] == gru::kRes;
        if(has_res)
            snprintf(line, sizeof(line), "F%.1f R%.1f", (double)g_cut, (double)g_res);
        else
            snprintf(line, sizeof(line), "F%.1f R6*", (double)g_cut);  // trained at one Res only
        if(strlen(line) > 10)
            snprintf(line, sizeof(line), "F%.1fR%.1f", (double)g_cut, (double)g_res);
    }
    else if(g_active >= 100)
        // A not-amp: where its steer sits, since the steer is its whole character.
        snprintf(line, sizeof(line), "D%+d S%.2f", drive, (double)g_steer);
    else
        snprintf(line, sizeof(line), "D%+ddB", drive);
    display.DrawString(0, 34, line, false);
}

static void UpdateDisplay()
{
    display.Clear();
    DrawRunPage();
    display.Update();
}
#else
// MIRTH LITE's only display. A blink request becomes a pattern — a long blink per
// five, then a short per one — after which the LED shows the engine state again.
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
        for(int k = 0; k < req / 5 && n < 46; k++) { pat[n++] = 600; pat[n++] = 250; }
        for(int k = 0; k < req % 5 && n < 46; k++) { pat[n++] = 140; pat[n++] = 160; }
        if(n == 0) { pat[n++] = 40; pat[n++] = 400; }  // a 0 (none loaded): one blip
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
    SetLed(!g_bypass);
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
#if MIRTH_LITE
    bank_toggle.Init(patch.B8, patch.AudioCallbackRate(), Switch::TYPE_TOGGLE, Switch::POLARITY_NORMAL);
    for(int i = 0; i < 40; i++)  // settle the debounce, then start in the toggle's bank
    {
        patch.ProcessAllControls();
        bank_toggle.Debounce();
        System::Delay(1);
    }
    g_bank = bank_toggle.Pressed() ? Bank::NotAmps : Bank::Amps;
#endif

    patch.StartDac();

    engine.InitFallback(sr);
    cpu_meter.Init(sr, patch.AudioBlockSize());
    g_layer.Init();  // its FFT tables, once
    g_layer.Reset(1);

    // Card before audio starts, so the first capture is in place by the time
    // anything is heard. A missing or unreadable card is not fatal: the
    // compiled-in fallback is already loaded and captures::Status() says why.
    captures::Init();
    if(captures::Count() > 0)
    {
        SwapSlot(Slot(Bank::Amps, 0));
        g_want = g_active;
    }

#if !MIRTH_LITE
    display.Init(patch.A2, patch.A3);
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
            if(!SwapSlot(g_want))
            {
                // Failed: keep whatever is loaded, and mark the slot so the
                // selection does not ask for it again every block. Usually a
                // not-amp whose source capture is not on the card.
                g_bad  = g_want;
                g_want = g_active;
            }
            g_xf            = Xf::FadeIn;
            g_display_dirty = true;
#if MIRTH_LITE
            g_blink_request = g_active >= 0 ? SlotIndex(g_active) + 1 : 0;  // the new slot's number
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
