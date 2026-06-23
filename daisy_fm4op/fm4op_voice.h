// fm4op as a multiosc engine.
//
// 4-operator FM voice mapped onto the universal panel (docs/PANEL.md):
//   TUNE = pitch (+ V/OCT)        short press = cycle algorithm
//   Play : MOD1..3 = per-algorithm FM controls (see ModLabel)
//   Edit : MOD1 = Attack  MOD2 = Release  MOD3 = Volume  (soft-takeover)
// Algorithms (short press): Parallel / Serial / Feedback. The original
// frequency-scaled modulation is preserved (modulation scales with the carrier
// increment), which keeps it musical and limits aliasing.
//
// DSP ported verbatim from daisy_fm4op/fm4op.cpp (original, MIT). The standalone
// fm4op.cpp build is retained behind its own Makefile for single-engine DFU
// testing; this class is the integrated form for daisy_multiosc.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisysp.h"
#include "multiosc_core/engine.h"

namespace multiosc {

class FmFourOp : public Engine {
  public:
    void Init(DaisyPatchSM& hw, float sample_rate) override;
    void Process(DaisyPatchSM&             hw,
                 const EngineContext&      ctx,
                 AudioHandle::InputBuffer  in,
                 AudioHandle::OutputBuffer out,
                 size_t                    size) override;

    const char* Name() const override { return "FM4OP"; }
    const char* ModLabel(int idx, bool edit) const override;
    const char* ShortLabel() const override { return "ALGO"; }
    const char* Selection() const override;
    int         BlockSize() const override { return 48; }

  private:
    struct Op {
        float phase, ratio, mod_index, incr, last_out;
    };

    void RecomputeIncrements(float base_freq);

    Op             ops_[4];
    daisysp::Adsr  env_;
    float          sample_rate_ = 48000.f;
    float          master_gain_ = 0.6f;
    uint8_t        algo_        = 0; // 0 Parallel, 1 Serial, 2 Feedback
    float          base_freq_   = 110.0f; // held while on the Edit page

    // Edit-page params (normalised 0..1) with soft-takeover.
    float        a_norm_ = 0.02f, r_norm_ = 0.32f, v_norm_ = 0.5f;
    SoftTakeover st_a_, st_r_, st_v_;
};

} // namespace multiosc
