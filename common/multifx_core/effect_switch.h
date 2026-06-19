// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / effect_switch.h
// Click-free patch changes. Extracted from the Seed firmware's per-sample wet
// fade. Call Trigger() whenever the active effect changes, then multiply each
// wet sample by NextGain(). The Patch SM v1 has no equivalent, so adopting this
// removes the pops it currently produces on patch change.

#pragma once

namespace mfx {

struct EffectSwitch {
  int total = 2048;   // ~43 ms @ 48 k
  int samps = 0;

  // Arm a fade-in over `fade_samps` samples (defaults to the Seed value).
  void Trigger(int fade_samps = 2048) { total = fade_samps; samps = fade_samps; }

  // Per-sample wet gain, ramps 0 -> 1 after a Trigger(), then stays at 1.
  inline float NextGain() {
    if (samps <= 0) return 1.f;
    float g = 1.f - (samps / (float)total);
    samps--;
    return g;
  }
};

} // namespace mfx
