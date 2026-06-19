// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / voicing.h
// The "secret sauce" output stage from the Daisy Seed MultiFX:
//   DC block  ->  gentle one-pole LPF (~14.5 kHz)  ->  soft clamp (+/-1.2)
//
// On the Seed this was added to fight a noisy home-built front end, but a gentle
// HF roll-off plus soft limiting is *also* exactly how you voice a digital FX to
// sound warm and rounded instead of brittle. That is a big part of why the Seed
// patches are preferred, so the Patch SM shell should run audio through this too.
//
// Dependency: none beyond <cmath>.

#pragma once
#include <cmath>
#include "dsp_helpers.h"

namespace mfx {

// First-order DC blocker:  y[n] = x[n] - x[n-1] + R*y[n-1]
struct DcBlock {
  float R = 0.995f, x1 = 0.f, y1 = 0.f;
  inline float Process(float x) { float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
  inline void  Reset() { x1 = y1 = 0.f; }
};

// One-pole low-pass.
struct OnePoleLP {
  float a = 0.f, y = 0.f;
  inline void SetCutoff(float fc, float fs) {
    float alpha = 1.f - expf(-2.f * 3.14159265f * fc / fs);
    a = clampf(alpha, 0.f, 1.f);
  }
  inline float Process(float x) { y += a * (x - y); return y; }
  inline void  Reset() { y = 0.f; }
};

// Combined stereo output stage. Drop the final wet/dry mix into this last.
//
// The ~14.5 kHz LPF was added on the Seed to tame a noisy analog front end. On a
// clean board it just dulls the top end, so it can be disabled by passing
// lpf_hz <= 0 to Init() — leaving only the DC blocker and the soft clamp (which
// still protects the hot patches from clipping).
struct OutputStage {
  DcBlock   dcL, dcR;
  OnePoleLP lpL, lpR;
  bool      lpf_on    = true;
  float     clamp_lvl = 1.2f;

  void Init(float samplerate, float lpf_hz = 14500.f, float clamp = 1.2f) {
    dcL.R = dcR.R = 0.995f;
    dcL.Reset(); dcR.Reset();
    lpf_on = (lpf_hz > 0.f);
    if (lpf_on) {
      lpL.SetCutoff(lpf_hz, samplerate);
      lpR.SetCutoff(lpf_hz, samplerate);
    }
    lpL.Reset(); lpR.Reset();
    clamp_lvl = clamp;
  }

  // Process a finished stereo sample in place.
  inline void Process(float& l, float& r) {
    l = dcL.Process(l); r = dcR.Process(r);
    if (lpf_on) { l = lpL.Process(l); r = lpR.Process(r); }
    // NaN guard: a stray NaN in a reverb feedback path must not latch silence.
    if (!(l == l)) l = 0.f;
    if (!(r == r)) r = 0.f;
    l = clampf(l, -clamp_lvl, clamp_lvl);
    r = clampf(r, -clamp_lvl, clamp_lvl);
  }
};

} // namespace mfx
