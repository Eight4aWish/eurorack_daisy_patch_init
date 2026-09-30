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
//   B1        model: Rossler, Van der Pol, Lorenz, Chua, Duffing, Coupled Rossler
//   B2        envelope: Drone (VCA open) or Gated by J4
//   B3        TAME mode: Auto, Force, Sync (Auto = the model's own choice)
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
#include "alchemy/surface/button_bank.h"
#include "alchemy/surface/control_loop.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/virtual_button.h"
#include "alchemy/surface/virtual_knob.h"

#include "chaos_core/Registry.h"
#include "chaos_core/Voice.h"

#include <math.h>
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
static uint8_t s_model = 0, s_env = 0, s_mode = 0;   // kept in sync by Bind()

static const char* kModelNames[N_ALGOS] = {
    "Rossler", "Van der Pol", "Lorenz", "Chua", "Duffing", "Coupled Rossler"};
static constexpr LedPanel::Rgb kModelColors[N_ALGOS] = {
    {0xFF, 0x60, 0x00}, {0xFF, 0xFF, 0x40}, {0x40, 0x80, 0xFF},
    {0xFF, 0x00, 0xC0}, {0x00, 0xFF, 0x60}, {0xFF, 0xFF, 0xFF}};

static const char* kEnvNames[2] = {"Drone", "Gated"};
static constexpr LedPanel::Rgb kEnvColors[2] = {{0x20, 0x20, 0x20}, {0xFF, 0xA0, 0x20}};

// Order matches Voice::TameMode: AUTO, FORCE, SYNC.
static const char* kModeNames[3] = {"Auto", "Force", "Sync"};
static constexpr LedPanel::Rgb kModeColors[3] = {
    {0x30, 0x30, 0x30}, {0x40, 0xFF, 0x80}, {0xB0, 0x40, 0xFF}};

static VirtualButton modelButton = VirtualButton(kButtonB1, "Model")
    .Ident("model").Selector(kModelNames).Colors(kModelColors).Bind(&s_model);
static VirtualButton envButton = VirtualButton(kButtonB2, "Envelope")
    .Ident("env").Selector(kEnvNames).Colors(kEnvColors).Bind(&s_env);
static VirtualButton modeButton = VirtualButton(kButtonB3, "Tame mode")
    .Ident("tame.mode").Selector(kModeNames).Colors(kModeColors).Bind(&s_mode);

static Page page = Page(0)
    .Knobs(tune, chaos, character, tame, attackDecay, sustainRelease)
    .Buttons(modelButton, envButton, modeButton);

/* ── Hardware and engine ────────────────────────────────────────────────── */
static AlchemyLab         hw;
static ControlLoop        loop(hw);
static ButtonBank         buttons;
static Voice              voice;
static daisy::CpuLoadMeter cpu;

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

static void AudioCallback(daisy::AudioHandle::InputBuffer  /*in*/,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           n)
{
    cpu.OnBlockStart();

    const Params p = s_params;
    ChaosBase* a = algos[p.model < N_ALGOS ? p.model : 0];
    voice.setAlgo(a);                                // no-op unless it changed

    if (p.retrig != s_retrigSeen) {                  // GATE edge: re-seed
        s_retrigSeen = p.retrig;
        a->init();
        voice.invalidateSnapshot();
    }

    voice.setEnvEnabled(p.env);
    voice.setEnvGate(p.gate);
    voice.setEnvADSR(p.atkMs, p.decMs, p.sustain, p.relMs);
    voice.setPitch(p.chaos, p.charV, p.hz, p.tame, static_cast<Voice::TameMode>(p.mode));
    voice.render(out[0], out[1], n);

    // X / Y CV, the raw state rather than the limited audio: the true attractor
    // for a scope. Once per block -- 2 kHz at 48 kHz / 24.
    hw.cv_jacks[kJackX].SetVolts(Clamp5(voice.getX() * a->cvScaleX));
    hw.cv_jacks[kJackY].SetVolts(Clamp5(voice.getY() * a->cvScaleY));

    cpu.OnBlockEnd();
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

/* ── Frame (16 ms): load governor ───────────────────────────────────────── */
// The step caps in chaos_core are estimates. If a block runs long, run fewer
// integration steps -- the pitch goes flat -- rather than overrun the audio.
// It should never engage; if it does, that model's maxStepsPerSecond is too
// high for this board.
static float s_loadScale = 1.0f;

static void Frame()
{
    const float peak = cpu.GetMaxCpuLoad();
    cpu.Reset();
    if (peak > 0.85f)                             s_loadScale *= 0.85f;
    else if (peak < 0.60f && s_loadScale < 1.0f)  s_loadScale *= 1.05f;
    if (s_loadScale > 1.0f)  s_loadScale = 1.0f;
    if (s_loadScale < 0.05f) s_loadScale = 0.05f;
    voice.setLoadScale(s_loadScale);

    // The Seed's own LED: lit while the governor holds pitch back.
    hw.seed.SetLed(s_loadScale < 0.999f);
}

int main()
{
    hw.Init();

    voice.setSampleRate(hw.SampleRate());
    voice.setAlgo(algos[0]);
    cpu.Init(hw.SampleRate(), static_cast<int>(hw.BlockSize()));

    hw.cv_jacks[kJackX].EnableCvOutput();
    hw.cv_jacks[kJackY].EnableCvOutput();

    hw.StartAudio(AudioCallback);

    loop.Use(page)
        .Use(buttons)
        .OnPoll(Poll)
        .OnFrame(Frame);

    for (;;) loop.Tick();
}
