// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / reverb_bank.h
// The four voiced reverbs from the Daisy Seed MultiFX, lifted out of the audio
// callback into a reusable bank. Same constants and curves as v1, so the sound
// is identical:
//   Classic  – plain ReverbSc
//   Plate    – smoothed predelay into ReverbSc
//   Tank     – predelay + light chorus modulation (de-metallises the tail)
//   Shimmer  – ReverbSc + PitchShifter(+12) with its own warm-up ramp
//
// Memory: this object holds ReverbSc + several DelayLines. Declare the instance
// with DSY_SDRAM_BSS in the shell so all buffers land in SDRAM, e.g.
//     DSY_SDRAM_BSS static mfx::ReverbBank g_reverb;
//
// Usage per audio sample:
//     g_reverb.Process(mode, dryL, dryR, p2, p3, wetL, wetR);
// Call g_reverb.Reset(mode) on every patch change (re-inits verb + arms shimmer).
//
// Dependency: DaisySP (ReverbSc, DelayLine, PitchShifter).

#pragma once
#include "daisysp.h"
#include "daisysp-lgpl.h" // ReverbSc lives in DaisySP-LGPL, not the core daisysp.h
#include "dsp_helpers.h"

namespace mfx {

enum class ReverbMode { Classic, Plate, Tank, Shimmer };

class ReverbBank {
 public:
  void Init(float samplerate) {
    sr_ = samplerate;
    verb_.Init(sr_);
    preL2_.Init(); preR2_.Init();
    preL3_.Init(); preR3_.Init();
    modL_.Init();  modR_.Init();
    shifter_.Init(sr_); shifter_.SetTransposition(12.f);
    phL_ = 0.f; phR_ = 0.5f;
    preA2_.Init(); preA3_.Init();
    preA2_.Reset(0.f); preA3_.Reset(0.f);
    warm_samps_ = 0;
  }

  // Call on patch change. Mirrors the Seed firmware's ResetFxForBankPatch().
  void Reset(ReverbMode mode) {
    verb_.Init(sr_);
    preL2_.Reset(); preR2_.Reset();
    preL3_.Reset(); preR3_.Reset();
    modL_.Reset();  modR_.Reset();
    phL_ = 0.f; phR_ = 0.5f;
    preA2_.primed = false; preA3_.primed = false;
    if (mode == ReverbMode::Shimmer) {
      warm_samps_ = kWarmSamps;
      shifter_.Init(sr_);
      shifter_.SetTransposition(12.f);
    } else {
      warm_samps_ = 0;
    }
  }

  // One stereo sample. p1/p2/p3 are the three effect params (0..1); the host
  // applies the global Mix afterwards. Classic ignores p3 (ReverbSc has no useful
  // third control); the others use it for an independent feedback / shimmer knob.
  void Process(ReverbMode mode, float dryL, float dryR,
               float p1, float p2, float p3, float& wetL, float& wetR) {
    switch (mode) {
      case ReverbMode::Classic: {
        verb_.SetFeedback(map_lin01(p1, 0.70f, 0.98f));
        verb_.SetLpFreq(map_lin01(p2, 1000.f, 18000.f));
        verb_.Process(dryL, dryR, &wetL, &wetR);
      } break;

      case ReverbMode::Plate: {
        float pre_ms = map_exp01(p1, 10.f, 80.f);
        float target = clampf(pre_ms * 0.001f * sr_, 1.f, kPre2Max - 1.f);
        float pre    = preA2_.Process(target);
        preL2_.SetDelay(pre); preR2_.SetDelay(pre);
        float inL = preL2_.Read(), inR = preR2_.Read();
        preL2_.Write(dryL); preR2_.Write(dryR);
        verb_.SetFeedback(map_lin01(p3, 0.75f, 0.97f)); // p3: independent size
        verb_.SetLpFreq(map_lin01(p2, 12000.f, 18000.f));
        verb_.Process(inL, inR, &wetL, &wetR);
      } break;

      case ReverbMode::Tank: {
        float pre_ms = map_exp01(p1, 30.f, 200.f);
        float target = clampf(pre_ms * 0.001f * sr_, 1.f, kPre3Max - 1.f);
        float pre    = preA3_.Process(target);
        preL3_.SetDelay(pre); preR3_.SetDelay(pre);
        float inL = preL3_.Read(), inR = preR3_.Read();
        preL3_.Write(dryL); preR3_.Write(dryR);

        float rate = 0.15f / sr_;
        phL_ += rate; if (phL_ >= 1.f) phL_ -= 1.f;
        phR_ += rate; if (phR_ >= 1.f) phR_ -= 1.f;
        modL_.SetDelay(clampf(sr_ * (0.006f + 0.002f * sin01(phL_)),        4.f, kModMax - 10.f));
        modR_.SetDelay(clampf(sr_ * (0.006f + 0.002f * sin01(phR_ + 0.3f)), 4.f, kModMax - 10.f));
        float mmL = modL_.Read(); modL_.Write(inL); inL = mmL;
        float mmR = modR_.Read(); modR_.Write(inR); inR = mmR;

        verb_.SetFeedback(map_lin01(p3, 0.85f, 0.985f)); // p3: independent size
        verb_.SetLpFreq(map_lin01(1.f - p2, 3000.f, 12000.f));
        verb_.Process(inL, inR, &wetL, &wetR);
      } break;

      case ReverbMode::Shimmer: {
        verb_.SetFeedback(map_lin01(p1, 0.75f, 0.98f));
        verb_.SetLpFreq(map_lin01(p2, 1500.f, 16000.f));
        float vL, vR;
        verb_.Process(dryL, dryR, &vL, &vR);

        float warm = 1.f;
        if (warm_samps_ > 0) { warm = 1.f - (warm_samps_ / (float)kWarmSamps); warm_samps_--; }

        float wetMono = 0.5f * (vL + vR);
        float shim = shifter_.Process(wetMono) * clampf(p3, 0.f, 1.f) * warm; // p3: shimmer amt
        wetL = vL + shim * 0.7f;
        wetR = vR + shim * 0.7f;
      } break;
    }
  }

 private:
  static constexpr float kPre2Max  = 12000.f;  // plate predelay line length
  static constexpr float kPre3Max  = 16000.f;  // tank predelay line length
  static constexpr float kModMax   = 1200.f;   // tank modulation line length
  static constexpr int   kWarmSamps = 8192;    // ~170 ms @ 48 k shimmer warm-up

  // No in-class member initializers: this object lives in DSY_SDRAM_BSS, so it
  // must be trivially constructible (see SmoothedParam note). All state is set
  // in Init()/Reset(), which run from main() after the SDRAM is up.
  float sr_;
  daisysp::ReverbSc verb_;
  daisysp::DelayLine<float, 12000> preL2_, preR2_;
  daisysp::DelayLine<float, 16000> preL3_, preR3_;
  daisysp::DelayLine<float, 1200>  modL_, modR_;
  daisysp::PitchShifter shifter_;
  float phL_, phR_;
  SmoothedParam preA2_, preA3_;
  int warm_samps_;
};

} // namespace mfx
