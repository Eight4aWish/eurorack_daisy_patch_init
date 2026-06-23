// Scanned synthesis engine for daisy_multiosc.
//
// A ring of masses connected by springs (a slow, sub-audio "string") is excited
// by the gate; its evolving *shape* is read at audio rate as the waveform. Pitch
// (scan rate) is decoupled from the timbral evolution (the spring dynamics), so
// a held note slowly morphs. See Verplank/Mathews/Shaw scanned synthesis.
//
// Universal panel (docs/PANEL.md):
//   TUNE = pitch (+V/Oct)            short press = excitation shape
//   Play : MOD1 = Tension  MOD2 = Damping  MOD3 = Hammer
//   Edit : MOD1 = Centering (restoring force)
//   Env on  = gated (pluck on gate, rings out per damping; silent at rest)
//   Env off = continuous self-excitation -> evolving drone (default)
//
// Original work. Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisysp.h"
#include "multiosc_core/engine.h"

namespace multiosc {

class ScannedVoice : public Engine {
  public:
    void Init(DaisyPatchSM& hw, float sample_rate) override;
    void Process(DaisyPatchSM&             hw,
                 const EngineContext&      ctx,
                 AudioHandle::InputBuffer  in,
                 AudioHandle::OutputBuffer out,
                 size_t                    size) override;

    const char* Name() const override { return "SCAN"; }
    const char* ModLabel(int idx, bool edit) const override;
    const char* ShortLabel() const override { return "EXC"; }
    const char* Selection() const override;
    bool        DefaultEnvOn() const override { return false; } // evolving drone

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

    float    freq_held_ = 110.f;  // pitch (held on the Edit page)
    float    tension_   = 0.25f;  // MOD1 : wave speed / brightness
    float    damping_   = 0.005f; // MOD2 : drone <-> pluck decay
    float    hammer_    = 0.5f;   // MOD3 : excitation amount
    float    center_    = 0.004f; // Edit MOD1 : restoring force

    uint8_t  shape_     = 0;      // excitation profile (short press)
    bool     prev_gate_ = false;

    float    dc_x1_ = 0.f, dc_y1_ = 0.f; // output DC blocker
    uint32_t rng_   = 0x1234abcdu;       // excitation noise
};

} // namespace multiosc
