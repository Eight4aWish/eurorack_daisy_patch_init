// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / dsp_helpers.h
// Small, allocation-free DSP helpers shared by every effect.
// Extracted verbatim (same constants/curves) from the Daisy Seed MultiFX so
// the *sound* is preserved when these run on the Patch SM shell.
//
// Dependency: DaisySP only (for daisysp::fonepole). No Arduino / DaisyDuino.

#pragma once
#include <cmath>
#include "daisysp.h"

namespace mfx {

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

// Unipolar sine on a 0..1 phase.
inline float sin01(float ph) { return sinf(2.f * 3.14159265f * ph); }

// Exponential (perceptual) map of x01 -> [minv, maxv]. Use for time/frequency.
inline float map_exp01(float x01, float minv, float maxv) {
  x01 = clampf(x01, 0.f, 1.f);
  float lnmin = logf(minv), lnrange = logf(maxv) - lnmin;
  return expf(lnmin + x01 * lnrange);
}

// Linear map of x01 -> [minv, maxv].
inline float map_lin01(float x01, float minv, float maxv) {
  x01 = clampf(x01, 0.f, 1.f);
  return minv + x01 * (maxv - minv);
}

// One-pole low-pass applied to a stereo pair in place (coefficient a in 0..1).
// This is the move that makes delay feedback darken naturally on each repeat.
inline void onepole_lp(float xL, float xR, float a, float& yL, float& yR) {
  yL += a * (xL - yL);
  yR += a * (xR - yR);
}

// Smoothed control value. Wraps daisysp::fonepole so every parameter glide in
// the core uses one consistent, tunable smoother instead of ad-hoc statics.
//
// No in-class member initializers on purpose: instances embedded in a bank that
// is placed in DSY_SDRAM_BSS must be trivially constructible, otherwise their
// constructor runs before SDRAM is initialized (pre-main static init) and writes
// to dead memory. Call Init() from the owner's Init() instead.
struct SmoothedParam {
  float value;
  float coeff;
  bool  primed;

  // coeff default matches the Seed firmware's fonepole rate.
  void  Init(float c = 0.0015f) { value = 0.f; coeff = c; primed = false; }
  void  Reset(float v) { value = v; primed = true; }
  // Call once per sample with the new target.
  float Process(float target) {
    if (!primed) { value = target; primed = true; return value; }
    daisysp::fonepole(value, target, coeff);
    return value;
  }
};

} // namespace mfx
