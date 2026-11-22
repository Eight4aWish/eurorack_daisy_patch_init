// Scanned synthesis engine for daisy_multiosc.
//
// A ring of masses connected by springs (a slow, sub-audio "string") is excited
// by the gate; its evolving *shape* is read at audio rate as the waveform. Pitch
// (scan rate) is decoupled from the timbral evolution (the spring dynamics), so
// a held note slowly morphs. See Verplank/Mathews/Shaw scanned synthesis.
//
// The panel (docs/PANEL.md, agreed with David on 2026-11-22):
//   pot 1  Excitation: how the ring is struck (Pulse, Bump, Two, Noise)
//   pot 2  Tension: how stiff the springs are, so how fast the shape changes   (+ the MOD 1 jack)
//   pot 3  Hammer: how hard each strike is, and how hard a drone is fed        (+ the MOD 2 jack)
//   pot 4  Centring: the pull back to rest on every mass                       (+ the MOD 3 jack)
//   hold + pot 4  Decay, which here is the Damping: how fast the movement dies. A struck ring has no
//          attack, so hold + pot 3 does nothing (David).
// Until the first gate it is a drone, fed all the time; from the first gate on, each gate strikes it and
// it rings out by the damping. Pitch is V/OCT alone (the scan rate). The host's envelope is not used.
//
// Original work. Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisysp.h"
#include "multiosc_core/engine.h"

namespace multiosc {

class ScannedVoice : public Engine {
  public:
    void Init(float sample_rate) override;
    void Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size) override;

    const char* Name() const override { return "SCAN"; }
    const char* Choice() const override;
    const char* PotLabel(int pot) const override;
    const char* DecayLabel() const override { return "Damp"; }
    bool        UsesEnvelope() const override { return false; }
    float       DefaultDecay() const override { return 0.44f; } // damping 0.005, the first build's

  private:
    static constexpr int kN       = 64; // masses (power of two)
    static constexpr int kShapes  = 4;

    void  Excite(float amount);
    void  UpdateDynamics();
    float NextRand();

    float    pos_[kN]  = {0};
    float    prev_[kN] = {0};
    float    sample_rate_ = 48000.f;
    float    scan_phase_  = 0.f;

    float    freq_      = 110.f;
    float    tension_   = 0.25f;  // pot 2: wave speed / brightness
    float    damping_   = 0.005f; // hold + pot 4: how fast it dies
    float    hammer_    = 0.5f;   // pot 3: excitation amount
    float    center_    = 0.004f; // pot 4: restoring force

    uint8_t  shape_     = 0;      // pot 1: excitation profile
    Stepper  shape_pot_{kShapes};
    bool     prev_gate_ = false;

    float    dc_x1_ = 0.f, dc_y1_ = 0.f; // output DC blocker
    uint32_t rng_   = 0x1234abcdu;       // excitation noise
};

} // namespace multiosc
