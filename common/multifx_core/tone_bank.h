// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / tone_bank.h
// Bank C of the unified MultiFX: the tone/shaper effects ported from the Daisy
// Patch SM v1 firmware (all pure DaisySP), wrapped to mirror ReverbBank/DelayBank.
//   Ladder   – Moog 4-pole ladder LP24 (self-oscillates, input drive)
//   Svf      – state-variable filter morphing LP -> BP -> HP -> Notch
//   Comb     – tuned comb / Karplus-Strong with damped feedback
//   WaveChr  – wavefolder into a stereo chorus
//
// Each effect takes three 0..1 params (p1,p2,p3) and returns a fully-wet stereo
// pair; the shell applies the global Mix and output voicing afterwards.
//
// Memory: Comb holds two ~2 s DelayLines, so declare the instance with
// DSY_SDRAM_BSS in the shell:
//     DSY_SDRAM_BSS static mfx::ToneBank g_tone;
//
// Dependency: DaisySP only (LadderFilter, Svf, Wavefolder, Chorus, DelayLine).

#pragma once
#include "daisysp.h"
#include "dsp_helpers.h"

namespace mfx {

enum class ToneMode { Ladder, Svf, Comb, WaveChr };

class ToneBank {
 public:
  void Init(float samplerate) {
    sr_ = samplerate;
    ladderL_.Init(sr_); ladderR_.Init(sr_);
    svfL_.Init(sr_);    svfR_.Init(sr_);
    combL_.Init();      combR_.Init();
    wfoldL_.Init();     wfoldR_.Init();
    chorus_.Init(sr_);
    chorus_.SetDelay(0.4f);
    chorus_.SetFeedback(0.1f);
    Reset(ToneMode::Ladder);
  }

  // Call on patch change. Clears the comb's feedback state so a new patch does
  // not inherit the previous one's ringing buffer.
  void Reset(ToneMode /*mode*/) {
    combL_.Reset(); combR_.Reset();
    comb_lpL_ = comb_lpR_ = 0.f;
  }

  void Process(ToneMode mode, float dryL, float dryR,
               float p1, float p2, float p3, float& wetL, float& wetR) {
    using daisysp::fmap;
    using daisysp::Mapping;
    switch (mode) {
      case ToneMode::Ladder: {
        float cutoff = fmap(p1, 20.f, 18000.f, Mapping::LOG);
        float res    = fmap(p2, 0.f, 1.8f);
        float drive  = fmap(p3, 0.f, 4.f);
        ladderL_.SetFreq(cutoff); ladderL_.SetRes(res); ladderL_.SetInputDrive(drive);
        ladderR_.SetFreq(cutoff); ladderR_.SetRes(res); ladderR_.SetInputDrive(drive);
        wetL = ladderL_.Process(dryL);
        wetR = ladderR_.Process(dryR);
      } break;

      case ToneMode::Svf: {
        float cutoff = fmap(p1, 20.f, 16000.f, Mapping::LOG);
        float res    = fmap(p2, 0.f, 1.f);
        float morph  = clampf(p3, 0.f, 1.f);
        svfL_.SetFreq(cutoff); svfL_.SetRes(res);
        svfR_.SetFreq(cutoff); svfR_.SetRes(res);
        svfL_.Process(dryL);
        svfR_.Process(dryR);
        if (morph < 0.333f) {
          float t = morph / 0.333f;
          wetL = svfL_.Low()  * (1.f - t) + svfL_.Band()  * t;
          wetR = svfR_.Low()  * (1.f - t) + svfR_.Band()  * t;
        } else if (morph < 0.667f) {
          float t = (morph - 0.333f) / 0.334f;
          wetL = svfL_.Band() * (1.f - t) + svfL_.High()  * t;
          wetR = svfR_.Band() * (1.f - t) + svfR_.High()  * t;
        } else {
          float t = (morph - 0.667f) / 0.333f;
          wetL = svfL_.High() * (1.f - t) + svfL_.Notch() * t;
          wetR = svfR_.High() * (1.f - t) + svfR_.Notch() * t;
        }
      } break;

      case ToneMode::Comb: {
        float freq     = fmap(p1, kCombMinHz, 1200.f, Mapping::LOG);
        float feedback = fmap(p2, 0.f, 0.99f);
        float delay_s  = fminf(sr_ / freq, kCombMax - 1.f);
        combL_.SetDelay(delay_s);
        combR_.SetDelay(delay_s);
        float lp_alpha = fmap(p3, 0.08f, 0.92f);
        float dl = combL_.Read();
        float dr = combR_.Read();
        comb_lpL_ += lp_alpha * (dl - comb_lpL_);
        comb_lpR_ += lp_alpha * (dr - comb_lpR_);
        combL_.Write(dryL + comb_lpL_ * feedback);
        combR_.Write(dryR + comb_lpR_ * feedback);
        wetL = dl; wetR = dr;
      } break;

      case ToneMode::WaveChr: {
        float fold = fmap(p1, 1.f, 8.f);
        wfoldL_.SetGain(fold);
        wfoldR_.SetGain(fold);
        chorus_.SetLfoFreq(fmap(p2, 0.1f, 5.f));
        chorus_.SetLfoDepth(clampf(p3, 0.f, 1.f));
        float fl = wfoldL_.Process(dryL);
        float fr = wfoldR_.Process(dryR);
        chorus_.Process((fl + fr) * 0.5f);
        wetL = chorus_.GetLeft();
        wetR = chorus_.GetRight();
      } break;
    }
  }

 private:
  // The comb's lowest tuned frequency sets the longest delay it ever needs:
  // SR / 40 Hz ≈ 1200 samples @ 48 k (well under 4096, which also covers 96 k).
  // Keeping this small lets the whole ToneBank live in SRAM — important because
  // LadderFilter's constructor writes to its members, which would fault if it ran
  // (pre-main) in not-yet-initialized SDRAM.
  static constexpr float  kCombMinHz = 40.f;
  static constexpr size_t kCombLen   = 4096;
  static constexpr float  kCombMax   = (float)kCombLen;

  float sr_;
  daisysp::LadderFilter ladderL_, ladderR_;
  daisysp::Svf          svfL_, svfR_;
  daisysp::DelayLine<float, kCombLen> combL_, combR_;
  daisysp::Wavefolder   wfoldL_, wfoldR_;
  daisysp::Chorus       chorus_;
  float comb_lpL_, comb_lpR_;
};

} // namespace mfx
