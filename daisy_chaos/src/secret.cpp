// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Secret -- a chaotic-attractor oscillator for the Hermetic Modular Alchemy Lab V2.
//
// The DSP is common/chaos_core, unchanged: six RK4 attractors in a Voice, played
// through Voice::setPitch() so V/Oct plays notes and TAME runs from free chaos to
// a locked tone (docs/SECRET.md). This file is only the platform layer: panel,
// jacks, the audio callback, and a load governor.
//
// First-flash scope. Deliberately left for later: presets, EXT DRIVE (J1), SYNC
// in (J2), FREEZE, a two-point V/Oct calibration of our own, and LED display of
// the attractor's state. The rings show the knobs, CV included.
//
//   P1 TUNE   27.5-880 Hz, exponential; + V/OCT (J3)
//   P2 CHAOS  the bifurcation parameter; + CV (J5)
//   P3 CHAR   the secondary parameter
//   P4 TAME   free chaos (0) to a locked note (1); + CV (J6)
//   P5 AD     envelope attack + decay
//   P6 SR     envelope sustain + release
//
//   B1        banks of six models, two menu levels. A short press steps the
//             current level; a long press (0.8 s) switches between the patch level
//             and the bank level. B1's top LED shows the bank, its bottom LED the
//             patch, both in the same six colours; at the bank level the top LED
//             blinks. Bank 1: Rossler, Van der Pol, Lorenz, Chua, Duffing, Coupled
//             Rossler. Bank 2: Pendulum, Lorenz-Lu-Chen, Moore-Spiegel, Brusselator,
//             Colpitts, Hindmarsh-Rose.
//   B2        TAME mode: Auto, Force, Sync (Auto = the model's own choice)
//   B3        envelope: Drone (VCA open) or Gated by J4
// Each button sits between the knobs it belongs with: B2 beside TAME, B3 between
// AD and SR. Swapped 2026-10-02, when the panel layout made the old order look wrong.
//
//   J3 V/OCT in   J4 GATE in       J5 CHAOS CV in  J6 TAME CV in
//   J7 X CV out   J8 Y CV out      J9 L (X) audio  J10 R (Y) audio
//
// In Gated mode a rising edge on GATE re-seeds the attractor, as on the Teensy:
// the transient back onto the attractor is the percussive part of a hit. Drone
// ignores GATE entirely.
//
// Boot gestures belong to the board, not this firmware: hold B3 at power-on for
// DFU (flashing), B1 + B2 for the factory CV calibration.

#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"
#include "util/scopedirqblocker.h"

#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/host_link/diagnostics.h"
#include "alchemy/host_link/host.h"
#include "alchemy/surface/button_bank.h"
#include "alchemy/surface/control_loop.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/virtual_button.h"
#include "alchemy/surface/virtual_knob.h"

#include "chaos_core/Registry.h"
#include "chaos_core/Voice.h"

#include <math.h>
#include <string.h>
#include <type_traits>

using namespace alchemy;
using namespace chaos_core;

static_assert(std::is_same_v<AlchemyLab, AlchemyLabV2>,
              "Secret needs the Alchemy Lab V2 (-DALCHEMY_BOARD_V2)");

/* ── Jacks: indices into hw.cv[] / hw.cv_jacks[], which start at J3 ─────── */
// V/OCT and GATE side by side, the pair a sequencer drives; then the two
// modulation inputs in knob order.
static constexpr uint8_t kJackVoct  = 0;   // J3
static constexpr uint8_t kJackGate  = 1;   // J4
static constexpr uint8_t kJackChaos = 2;   // J5
static constexpr uint8_t kJackTame  = 3;   // J6
static constexpr uint8_t kJackX     = 4;   // J7, STM32 DAC: fast
static constexpr uint8_t kJackY     = 5;   // J8, STM32 DAC: fast

// Knob CV depth. Value() spans +-10 V across 0..1, so 2.0 makes +-5 V sweep a
// knob from its centre to either end.
static constexpr float kCvDepth = 2.0f;

// GATE thresholds, volts, with hysteresis.
static constexpr float kGateOn  = 1.2f;
static constexpr float kGateOff = 0.6f;

// Envelope macro ranges, as on the Teensy build.
static constexpr float kAtkMinMs = 0.5f, kAtkMaxMs = 1000.0f;
static constexpr float kDecMinMs = 2.0f, kDecMaxMs = 2000.0f;
static constexpr float kRelMinMs = 2.0f, kRelMaxMs = 4000.0f;

/* ── Knobs ──────────────────────────────────────────────────────────────── */
static VirtualKnob tune = VirtualKnob(kPotTopLeft, "Tune")
    .Exp(27.5f, 880.0f)
    .Ring(Level({0xFF, 0xB0, 0x40}));

static VirtualKnob chaos = VirtualKnob(kPotTopRight, "Chaos")
    .Cv(kJackChaos, kCvDepth)
    .Ring(Level({0xFF, 0x30, 0x60}, FillAnim::Pulse));

static VirtualKnob character = VirtualKnob(kPotMiddleLeft, "Char")
    .Ring(Level({0x40, 0xA0, 0xFF}));

static VirtualKnob tame = VirtualKnob(kPotMiddleRight, "Tame")
    .Cv(kJackTame, kCvDepth)
    .Ring(Level({0x40, 0xFF, 0x80}, FillAnim::Ripple));

static VirtualKnob attackDecay = VirtualKnob(kPotBottomLeft, "AD")
    .Ring(Level({0xC0, 0xC0, 0xC0}));

static VirtualKnob sustainRelease = VirtualKnob(kPotBottomRight, "SR")
    .Ring(Level({0x80, 0x80, 0x80}));

/* ── Buttons ────────────────────────────────────────────────────────────── */
static uint8_t s_env = 0, s_mode = 0;   // kept in sync by Bind()

// Banks of six (chaos_core/Registry.h: bank 1, then bank 2), heading for up to six
// banks. B1 has two menu levels, David's design (2026-10-09): a short press steps
// whichever level is active, a long press switches level. The active model is
// bank * 6 + patch. Changing bank keeps the patch number.
static constexpr uint8_t kNumBanks  = N_ALGOS / kBankSize;   // kBankSize: chaos_core/Registry.h
static constexpr uint32_t kLevelHoldMs = 800;
static_assert(N_ALGOS % kBankSize == 0, "whole banks of six");
static_assert(N_ALGOS == 12, "one name per model");
static uint8_t s_bank = 0, s_patch = 0, s_model = 0;
static bool    s_bankLevel = false;   // false: patch level (the power-on level)
static const char* kModelNames[N_ALGOS] = {
    "Rossler", "Van der Pol", "Lorenz", "Chua", "Duffing", "Coupled Rossler",
    "Pendulum", "Lorenz-Lu-Chen", "Moore-Spiegel", "Brusselator", "Colpitts",
    "Hindmarsh-Rose"};
// One sequence of six colours, used for both the bank (B1's top LED) and the
// patch within it (the bottom LED): bank 1's colours from the twelve-in-a-list build.
static constexpr LedPanel::Rgb kSix[kBankSize] = {
    {0xFF, 0x60, 0x00},   // 1 orange
    {0xFF, 0xFF, 0x40},   // 2 yellow
    {0x40, 0x80, 0xFF},   // 3 blue
    {0xFF, 0x00, 0xC0},   // 4 magenta
    {0x00, 0xFF, 0x60},   // 5 green
    {0xFF, 0xFF, 0xFF}};  // 6 white

static void SelectModel() { s_model = static_cast<uint8_t>(s_bank * kBankSize + s_patch); }
static void OnB1Tap(void*)
{
    if (s_bankLevel) s_bank  = static_cast<uint8_t>((s_bank + 1) % kNumBanks);
    else             s_patch = static_cast<uint8_t>((s_patch + 1) % kBankSize);
    SelectModel();
}
static void OnB1Hold(void*) { s_bankLevel = !s_bankLevel; }

static const char* kEnvNames[2] = {"Drone", "Gated"};
static constexpr LedPanel::Rgb kEnvColors[2] = {{0x20, 0x20, 0x20}, {0xFF, 0xA0, 0x20}};

// Order matches Voice::TameMode: AUTO, FORCE, SYNC.
static const char* kModeNames[3] = {"Auto", "Force", "Sync"};
static constexpr LedPanel::Rgb kModeColors[3] = {
    {0x30, 0x30, 0x30}, {0x40, 0xFF, 0x80}, {0xB0, 0x40, 0xFF}};

static VirtualButton modelButton = VirtualButton(kButtonB1, "Model")
    .Ident("model")
    .Tap(OnB1Tap, "Next patch (or bank, at the bank level)")
    .Hold(kLevelHoldMs, OnB1Hold, "Switch between the patch and bank levels");
static VirtualButton envButton = VirtualButton(kButtonB3, "Envelope")
    .Ident("env").Selector(kEnvNames).Colors(kEnvColors).Bind(&s_env);
static VirtualButton modeButton = VirtualButton(kButtonB2, "Tame mode")
    .Ident("tame.mode").Selector(kModeNames).Colors(kModeColors).Bind(&s_mode);

static Page page = Page(0)
    .Knobs(tune, chaos, character, tame, attackDecay, sustainRelease)
    .Buttons(modelButton, modeButton, envButton);  // B1, B2, B3

/* ── Hardware and engine ────────────────────────────────────────────────── */
static AlchemyLab         hw;
static ControlLoop        loop(hw);
static ButtonBank         buttons;
static Voice              voice;
static daisy::CpuLoadMeter cpu;

/* ── HostLink: reflashing and diagnostics over the front USB-C ──────────── */
// Identity only, no presets yet. It is what lets `make program-live` reboot the
// module into its bootloader with no buttons, and it carries the gauges and the
// click log below to hostlink-cli and the web programmer's Device console:
//   node deps/alchemy-sdk/tools/hostlink-cli/hostlink.mjs -p /dev/cu.usbmodem<serial> watch
// On macOS use the cu.* node; the CLI's tty.* default blocks on open.
static hostlink::Host        host("secret", "Secret", "1.0.0", SECRET_GIT_HASH);
static hostlink::Diagnostics debug;

static hostlink::Gauge<float>    g_cpuAvg ("cpu.avg",   "CPU average");
static hostlink::Gauge<float>    g_cpuMax ("cpu.max",   "CPU peak since model change");
static hostlink::Gauge<float>    g_gov    ("gov.scale", "Load governor (1 = not throttling)");
static hostlink::Gauge<float>    g_steps  ("steps",     "RK4 steps per sample");
static hostlink::Gauge<uint32_t> g_model  ("model",     "Model (B1)");
static hostlink::Gauge<float>    g_hz     ("hz",        "Pitch");
static hostlink::Gauge<float>    g_chaos  ("chaos",     "CHAOS value");
static hostlink::Gauge<float>    g_char   ("char",      "CHAR value");
static hostlink::Gauge<float>    g_tame   ("tame",      "TAME");
static hostlink::Gauge<bool>     g_gate   ("gate",      "Gate (J4)");
static hostlink::Gauge<bool>     g_env    ("env",       "Envelope gated (B3)");
static hostlink::Gauge<uint32_t> g_guard  ("guard",     "Guard re-seeds");
static hostlink::Gauge<uint32_t> g_jumps  ("jumps",     "Output jumps (clicks)");
static hostlink::Gauge<bool>     g_cal    ("cal",       "Board CV calibration loaded");

/* ── Click hunting ──────────────────────────────────────────────────────── */
// A sample-to-sample step in the output this big is a discontinuity, not a
// waveform: a full-scale sine at 880 Hz moves at most 0.12 per sample. When the
// audio callback sees one it records the panel state and what else happened in
// the same block, and the control loop logs it. The causes are the ones the
// engine can produce: a divergence-guard re-seed, a SYNC pull or a new SYNC
// snapshot, a GATE re-seed, a model change, or the governor cutting the pitch.
static constexpr float kJumpFs = 0.5f;

enum : uint8_t {
    kCauseGuard = 1, kCauseSyncPull = 2, kCauseSnapshot = 4,
    kCauseGate  = 8, kCauseModel    = 16, kCauseGovernor = 32,
    kCauseEnvelope = 64,   // B3 switched between Drone and Gated
};

struct Jump {
    uint32_t seq;          // bumped on every recorded jump
    float    size;         // largest step in the block, full scale
    float    chaos, charV, hz, tame;
    uint8_t  model, mode;  // mode already resolved from Auto
    uint8_t  causes;
    bool     env;
};
static Jump s_jump = {};   // written in the audio callback, copied out with IRQs held

/* ── Control -> audio handoff ───────────────────────────────────────────── */
// The control loop fills a complete set and copies it in with interrupts held
// off; the audio callback copies it out at the start of each block. So a block
// never integrates from a half-written set, which for Chua is not cosmetic: a
// new high `a` against an old `b` is its unbounded corner.
struct Params {
    float    chaos, charV, hz, tame;
    float    atkMs, decMs, sustain, relMs;
    uint8_t  model, mode;
    bool     env, gate;
    uint32_t retrig;   // bumped on every GATE rising edge
};
static Params   s_params = {5.0f, 0.23f, 110.0f, 0.0f, 10.0f, 200.0f, 0.5f, 200.0f,
                            0, 0, false, false, 0};
static uint32_t s_retrigSeen = 0;

static inline float ExpoMap(float n, float lo, float hi) {
    return lo * powf(hi / lo, n);
}
// NaN goes to 0 V: a plain clamp passes it through (every comparison with NaN
// is false), and the SDK's volts-to-code conversion would then cast it to an
// integer, which is undefined. The divergence guard makes a NaN unlikely here.
static inline float Clamp5(float v) {
    if (!isfinite(v)) return 0.0f;
    return v > 5.0f ? 5.0f : (v < -5.0f ? -5.0f : v);
}

/* ── Load governor ──────────────────────────────────────────────────────── */
// The step caps in chaos_core were measured on the Teensy at 600 MHz without
// TAME; this board runs at 400 MHz and TAME adds work per step. If a block runs
// long, run fewer integration steps -- the pitch goes flat -- rather than
// overrun.
//
// It has to live here, in the audio callback. The first build ran it from the
// control loop's frame, but an overrunning callback starves the control loop,
// so the governor that should rescue it never runs: knobs, LEDs and buttons
// freeze until power-off. That is the failure the Teensy build's governor was
// written for, and why it measured inside the audio interrupt too.
static float          s_ticksPerSample = 0.0f;   // timer ticks per audio sample, set in main()
static volatile bool  s_governed       = false;
static volatile float s_peakLoad       = 0.0f;    // worst block since the last heartbeat

static float GovernLoad(uint32_t elapsedTicks, size_t n)
{
    const float budget = (float)n * s_ticksPerSample;
    const float used   = (float)elapsedTicks;
    if (used > s_peakLoad * budget) s_peakLoad = used / budget;
    // Read back from the voice, not mirrored: setAlgo() resets it to 1 because a
    // throttle describes the cost of the model that earned it.
    float sc = voice.loadScale();
    if (used > 0.75f * budget) {                        // over 75% of the block
        sc *= 0.85f;                                    // ~4 blocks to halve
        if (sc < 0.02f) sc = 0.02f;
    } else if (used < 0.5f * budget && sc < 1.0f) {     // under 50%: creep back
        sc += 0.02f;
        if (sc > 1.0f) sc = 1.0f;
    }
    voice.setLoadScale(sc);
    s_governed = sc < 0.999f;
    return sc;
}

static float   s_prevL = 0.0f, s_prevR = 0.0f;   // last output sample, for jumps
static uint8_t s_modelSeen = 0xFF;
static bool    s_envSeen   = false;

static void AudioCallback(daisy::AudioHandle::InputBuffer  /*in*/,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           n)
{
    const uint32_t t0 = daisy::System::GetTick();
    cpu.OnBlockStart();

    const Params p = s_params;
    ChaosBase* a = algos[p.model < N_ALGOS ? p.model : 0];
    uint8_t causes = 0;
    if (p.model != s_modelSeen) {
        s_modelSeen = p.model;
        causes |= kCauseModel;
        cpu.Reset();                                 // peak CPU is per model
    }
    voice.setAlgo(a);                                // no-op unless it changed

    if (p.retrig != s_retrigSeen) {                  // GATE edge: re-seed
        s_retrigSeen = p.retrig;
        a->init();
        voice.invalidateSnapshot();
        causes |= kCauseGate;
    }
    if (voice.loadScale() < 0.999f) causes |= kCauseGovernor;
    if (p.env != s_envSeen) { s_envSeen = p.env; causes |= kCauseEnvelope; }

    voice.setEnvEnabled(p.env);
    voice.setEnvGate(p.gate);
    voice.setEnvADSR(p.atkMs, p.decMs, p.sustain, p.relMs);
    voice.setPitch(p.chaos, p.charV, p.hz, p.tame, static_cast<Voice::TameMode>(p.mode));

    const uint32_t guard0 = a->guardTrips;
    const uint32_t pulls0 = voice.syncPulls(), snaps0 = voice.snapCaptures();
    voice.render(out[0], out[1], n);
    if (a->guardTrips      != guard0) causes |= kCauseGuard;
    if (voice.syncPulls()    != pulls0) causes |= kCauseSyncPull;
    if (voice.snapCaptures() != snaps0) causes |= kCauseSnapshot;

    // X / Y CV, the raw state rather than the limited audio: the true attractor
    // for a scope. Once per block -- 2 kHz at 48 kHz / 24.
    hw.cv_jacks[kJackX].SetVolts(Clamp5(voice.getX() * a->cvScaleX));
    hw.cv_jacks[kJackY].SetVolts(Clamp5(voice.getY() * a->cvScaleY));

    // Click hunting: the largest sample-to-sample step, across the block edge too.
    float worst = 0.0f, pl = s_prevL, pr = s_prevR;
    for (size_t i = 0; i < n; i++) {
        const float dl = fabsf(out[0][i] - pl), dr = fabsf(out[1][i] - pr);
        if (dl > worst) worst = dl;
        if (dr > worst) worst = dr;
        pl = out[0][i]; pr = out[1][i];
    }
    s_prevL = pl; s_prevR = pr;
    if (worst > kJumpFs) {
        s_jump.seq++;
        s_jump.size   = worst;
        s_jump.chaos  = p.chaos;  s_jump.charV = p.charV;
        s_jump.hz     = p.hz;     s_jump.tame  = p.tame;
        s_jump.model  = p.model;
        s_jump.mode   = p.mode ? p.mode
                      : (a->pitchClass == PITCH_INCOHERENT ? Voice::TAME_SYNC : Voice::TAME_FORCE);
        s_jump.causes = causes;
        s_jump.env    = p.env;
        g_jumps.Set(s_jump.seq);
    }

    const float sc = GovernLoad(daisy::System::GetTick() - t0, n);
    cpu.OnBlockEnd();

    // Gauges: lock-free scalar stores, safe from here.
    g_cpuAvg.Set(cpu.GetAvgCpuLoad() * 100.0f);
    g_cpuMax.Set(cpu.GetMaxCpuLoad() * 100.0f);
    g_gov.Set(sc);
    g_steps.Set(voice.stepsPerSample());
    g_model.Set(p.model);
    g_hz.Set(p.hz);
    g_chaos.Set(p.chaos);
    g_char.Set(p.charV);
    g_tame.Set(p.tame);
    g_gate.Set(p.gate);
    g_env.Set(p.env);
    g_guard.Set(a->guardTrips);
}

/* ── 1 ms poll: gate, V/Oct, knobs + CV -> Params ───────────────────────── */
static bool     s_gateHigh = false;
static uint32_t s_retrig   = 0;

static void Poll(uint32_t /*t_ms*/)
{
    // Only Gated re-seeds on a GATE edge, where the envelope's attack starts from
    // silence at the same instant and hides the jump. In Drone the gate has no
    // job -- the envelope ignores it -- and a re-seed with the VCA open was a
    // click on every sequenced note.
    const bool  gated = s_env != 0;
    const float g     = hw.cv_jacks[kJackGate].Volts();
    if (!s_gateHigh && g > kGateOn)       { s_gateHigh = true; if (gated) ++s_retrig; }
    else if (s_gateHigh && g < kGateOff)  { s_gateHigh = false; }

    const uint8_t model = s_model < N_ALGOS ? s_model : 0;
    const ChaosBase* a  = algos[model];

    Params p;
    p.chaos  = a->chaosMin + chaos.Norm()     * (a->chaosMax - a->chaosMin);
    p.charV  = a->charMin  + character.Norm() * (a->charMax  - a->charMin);
    p.hz     = tune.Value() * exp2f(hw.cv_jacks[kJackVoct].Volts());
    p.tame   = tame.Norm();
    const float adN = attackDecay.Norm(), srN = sustainRelease.Norm();
    p.atkMs   = ExpoMap(adN, kAtkMinMs, kAtkMaxMs);
    p.decMs   = ExpoMap(adN, kDecMinMs, kDecMaxMs);
    p.sustain = srN;
    p.relMs   = ExpoMap(srN, kRelMinMs, kRelMaxMs);
    p.model  = model;
    p.mode   = s_mode < 3 ? s_mode : 0;
    p.env    = s_env != 0;
    p.gate   = s_gateHigh;
    p.retrig = s_retrig;

    daisy::ScopedIrqBlocker block;
    s_params = p;
}

/* ── Render: B1's two LEDs ──────────────────────────────────────────────── */
// Runs after the frame's clear and the button bank's colours, so these stick.
// Top = bank, bottom = patch. At the bank level the top LED blinks, about twice a
// second, to say which level a short press will step.
static void Render(uint32_t t_ms)
{
    const bool showTop = !s_bankLevel || ((t_ms / 250u) & 1u) == 0u;
    hw.leds.SetButton(kButtonB1, LedPanel::ButtonLed::Top,
                      showTop ? kSix[s_bank % kBankSize] : LedPanel::Rgb{0, 0, 0});
    hw.leds.SetButton(kButtonB1, LedPanel::ButtonLed::Bottom, kSix[s_patch % kBankSize]);
}

/* ── Frame (16 ms): governor LED and the USB log ────────────────────────── */
static void LogClick(const Jump& j, uint32_t missed)
{
    char why[48] = "";
    static const struct { uint8_t bit; const char* name; } kNames[] = {
        {kCauseGuard, "guard "}, {kCauseSyncPull, "sync-pull "}, {kCauseSnapshot, "snapshot "},
        {kCauseGate, "gate "},   {kCauseModel, "model "},        {kCauseGovernor, "governor "},
        {kCauseEnvelope, "envelope "}};
    for (const auto& c : kNames)
        if (j.causes & c.bit) strncat(why, c.name, sizeof why - strlen(why) - 1);
    if (!why[0]) strcpy(why, "none ");
    why[strlen(why) - 1] = '\0';

    const uint8_t m = j.model < N_ALGOS ? j.model : 0;
    debug.Warn("click %.2f FS (+%lu more): %s, CHAOS %.3f, CHAR %.3f, %.1f Hz, TAME %.2f %s, %s [%s]",
               (double)j.size, (unsigned long)missed, kModelNames[m],
               (double)j.chaos, (double)j.charV, (double)j.hz, (double)j.tame,
               kModeNames[j.mode < 3 ? j.mode : 0], j.env ? "Gated" : "Drone", why);
}

static void Frame()
{
    // The Seed's own LED: lit while the governor holds pitch back.
    hw.seed.SetLed(s_governed);

    // Panel changes, so the click log can be read against what was played.
    static uint8_t model = 0xFF, env = 0xFF, mode = 0xFF;
    static int8_t  level = -1;
    if (s_model != model) {
        model = s_model;
        debug.Info("Model: %s (bank %u, patch %u)", kModelNames[model < N_ALGOS ? model : 0],
                   (unsigned)(s_bank + 1), (unsigned)(s_patch + 1));
    }
    if (level != (int8_t)s_bankLevel) { level = (int8_t)s_bankLevel; debug.Info("B1 level: %s", s_bankLevel ? "bank" : "patch"); }
    if (s_env   != env)   { env   = s_env;   debug.Info("Envelope: %s", kEnvNames[env ? 1 : 0]); }
    if (s_mode  != mode)  { mode  = s_mode;  debug.Info("TAME mode: %s", kModeNames[mode < 3 ? mode : 0]); }

    const uint32_t now = daisy::System::GetNow();

    // Heartbeat, every 5 s: the worst block's load since the last one, and what
    // was playing. If the module ever freezes, the beats stop, and the last one
    // says how loaded the audio was just before.
    static uint32_t lastBeatMs = 0;
    if (now - lastBeatMs >= 5000u) {
        lastBeatMs = now;
        float peak;
        {
            daisy::ScopedIrqBlocker block;
            peak = s_peakLoad;
            s_peakLoad = 0.0f;
        }
        const Params p = s_params;
        debug.Info("load peak %.0f%% avg %.0f%%, steps %.1f, gov %.2f | %s %.1f Hz, TAME %.2f %s, guard %lu",
                   (double)(peak * 100.0f), (double)(cpu.GetAvgCpuLoad() * 100.0f),
                   (double)voice.stepsPerSample(), (double)voice.loadScale(),
                   kModelNames[p.model < N_ALGOS ? p.model : 0], (double)p.hz, (double)p.tame,
                   kModeNames[p.mode < 3 ? p.mode : 0],
                   (unsigned long)algos[p.model < N_ALGOS ? p.model : 0]->guardTrips);
    }

    // The latest click, at most four times a second; the count says how many
    // went by unlogged in between.
    static uint32_t lastSeq = 0, lastLogMs = 0;
    if (s_jump.seq != lastSeq && now - lastLogMs >= 250u) {
        Jump j;
        {
            daisy::ScopedIrqBlocker block;
            j = s_jump;
        }
        LogClick(j, j.seq - lastSeq - 1u);
        lastSeq = j.seq; lastLogMs = now;
    }
}

#ifdef SECRET_BENCH
/* ── make BENCH=1: measure every model's step cap on this board ─────────── */
// The caps in chaos_core came from the Teensy at 600 MHz without TAME, and the
// Chua episode showed they don't carry over. Before audio starts, run each model
// flat out and log the load, so each cap can be set from this chip.
//
// It runs in the control loop, not the audio callback, so nothing can be starved
// whatever a model costs. The pitch asked for is far above anything playable, so
// the schedule clamps at the cap: the worst block the model can produce. TAME 1
// is the worst case per step (FORCE's drive and the pull both run every step).
// The real callback adds a few percent on top: the click scan, CV and gauges.
static void Bench()
{
    static Voice v;
    static float l[kEngineBlockSamples], r[kEngineBlockSamples];
    constexpr int kBlocks = 400;   // 0.2 s of audio per measurement
    const float budget = (float)kEngineBlockSamples * s_ticksPerSample;
    const float cyclesPerTick =
        (float)daisy::System::GetSysClkFreq() / (float)daisy::System::GetTickFreq();
    debug.Info("bench: %lu MHz, %.0f timer ticks per %u-sample block",
               (unsigned long)(daisy::System::GetSysClkFreq() / 1000000u), (double)budget,
               (unsigned)kEngineBlockSamples);
    v.setSampleRate(hw.SampleRate());
    for (int m = 0; m < N_ALGOS; m++) {
        ChaosBase* a = algos[m];
        const float chaos = 0.5f * (a->chaosMin + a->chaosMax);
        const float charV = 0.5f * (a->charMin + a->charMax);
        v.setAlgo(a);
        float avg[2], worst[2], steps = 0.0f;
        for (int t = 0; t < 2; t++) {
            v.setLoadScale(1.0f);
            uint32_t sum = 0, most = 0;
            for (int b = 0; b < kBlocks; b++) {
                v.setPitch(chaos, charV, 1.0e6f, t ? 1.0f : 0.0f);   // far past any cap
                const uint32_t t0 = daisy::System::GetTick();
                v.render(l, r, kEngineBlockSamples);
                const uint32_t dt = daisy::System::GetTick() - t0;
                sum += dt;
                if (dt > most) most = dt;
            }
            avg[t]   = (float)sum / kBlocks / budget;
            worst[t] = (float)most / budget;
            steps    = v.stepsPerSample();
        }
        const float cycPerStep = avg[0] * budget * cyclesPerTick / (steps * kEngineBlockSamples);
        debug.Info("bench %-15s cap %4.1f st/smp, %3.0f cyc/st | TAME0 %3.0f%% (worst %3.0f%%), TAME1 %3.0f%% (%3.0f%%)",
                   kModelNames[m], (double)steps, (double)cycPerStep,
                   (double)(avg[0] * 100.0f), (double)(worst[0] * 100.0f),
                   (double)(avg[1] * 100.0f), (double)(worst[1] * 100.0f));
    }
}
#endif

int main()
{
    hw.Init();

    voice.setSampleRate(hw.SampleRate());
    voice.setAlgo(algos[0]);
    cpu.Init(hw.SampleRate(), static_cast<int>(hw.BlockSize()));
    s_ticksPerSample = static_cast<float>(daisy::System::GetTickFreq()) / hw.SampleRate();

    hw.cv_jacks[kJackX].EnableCvOutput();
    hw.cv_jacks[kJackY].EnableCvOutput();

    g_cpuAvg.Unit("%");
    g_cpuMax.Unit("%");
    g_hz.Unit("Hz");
    hostlink::GaugeBase* gauges[] = {&g_cpuAvg, &g_cpuMax, &g_gov,  &g_steps, &g_model,
                                     &g_hz,     &g_chaos,  &g_char, &g_tame,  &g_gate,
                                     &g_env,    &g_guard,  &g_jumps, &g_cal};
    for (hostlink::GaugeBase* g : gauges) debug.Watch(*g);
    g_cal.Set(hw.IsCalibrated());
    host.Extend(debug);

#ifdef SECRET_BENCH
    Bench();   // before audio starts: see Bench()
#endif

    hw.StartAudio(AudioCallback);

    loop.Use(page)
        .Use(buttons)
        .Use(host)
        .OnPoll(Poll)
        .OnFrame(Frame)
        .OnRender(Render);

    if (!host.ConfigurationOk() || !debug.ConfigurationOk())
        debug.Error("HostLink setup failed");
    debug.Info("Secret %s, CV %s", SECRET_GIT_HASH,
               hw.IsCalibrated() ? "calibrated" : "UNCALIBRATED (hold B1+B2 at power-on)");

    for (;;) loop.Tick();
}
