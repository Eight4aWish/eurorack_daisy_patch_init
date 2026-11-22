// fm4op as a multiosc engine: a 4-operator FM voice.
//
// The panel (docs/PANEL.md, agreed with David on 2026-11-22):
//   pot 1  Algorithm, eight of them, from one carrier with a deep stack to four sines mixed
//   pot 2  Ratio: a set of ratios for operators 2, 3 and 4, harmonic first, then inharmonic
//   pot 3  Index: how much modulation, every modulator together; at zero the carriers are plain sines
//   pot 4  Feedback: operator 4 modulating itself, from a sine through saw-like to noisy
// Pots 2-4 mean the same in every algorithm; the host gives them the MOD 1-3 jacks. The envelope and
// volume are the host's. Operator 1 is always a carrier, and every routing runs from a higher operator to
// a lower one, so operators are computed 4, 3, 2, 1 and each sees its modulators' current sample.
//
// The modulation is frequency-scaled, as the original fm4op: what a modulator adds to its target is scaled
// by the target's own increment, so it moves the target's frequency in proportion to it and the timbre
// holds across the keyboard. A rewrite to raw phase modulation with single-sample feedback aliased badly
// and was reverted once; the feedback here is frequency-scaled too, and averages the last two samples.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "multiosc_core/engine.h"

namespace multiosc {

class FmFourOp : public Engine {
  public:
    void Init(float sample_rate) override;
    void Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size) override;

    const char* Name() const override { return "FM4OP"; }
    const char* Choice() const override;
    const char* PotLabel(int pot) const override;

    static constexpr int kAlgorithms = 8;
    static constexpr int kRatioSets  = 11;

  private:
    float   sample_rate_ = 48000.f;
    float   phase_[4]    = {0, 0, 0, 0};
    float   out_[4]      = {0, 0, 0, 0}; // this sample's outputs
    float   fb_[2]       = {0, 0};       // operator 4's last two outputs
    float   index_       = 0.f;          // smoothed
    float   feedback_    = 0.f;          // smoothed
    Stepper algo_{kAlgorithms};
    Stepper ratio_{kRatioSets};
};

} // namespace multiosc
