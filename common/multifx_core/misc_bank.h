// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / misc_bank.h
// Bank D of the unified MultiFX: the "everything else" family, wrapped to mirror
// the other banks.
//   Resonator – 2-partial state-variable bandpass with envelope-follower drive
//   Pitch     – time-domain OLA pitch shifter (DaisySP PitchShifter)
//   Drive     – tanh soft-clip overdrive with post tone + level
//   Crush     – bitcrusher + sample-rate reducer with post tone
//
// Each effect takes three 0..1 params (p1,p2,p3) and returns a fully-wet stereo
// pair; the shell applies the global Mix and output voicing afterwards.
//
// Memory: the pitch shifters own sizeable internal buffers, so declare the
// instance with DSY_SDRAM_BSS in the shell:
//     DSY_SDRAM_BSS static mfx::MiscBank g_misc;
// (Drive/Crush add only float state, so the bank stays trivially constructible.)
//
// Dependency: DaisySP only (Svf, PitchShifter).

#pragma once
#include "daisysp.h"
#include "dsp_helpers.h"

namespace mfx {

enum class MiscMode { Resonator, Pitch, Drive, Crush };

class MiscBank {
 public:
  void Init(float samplerate) {
    sr_ = samplerate;
    bp1L_.Init(sr_); bp2L_.Init(sr_);
    bp1R_.Init(sr_); bp2R_.Init(sr_);
    pitchL_.Init(sr_); pitchR_.Init(sr_);
    Reset(MiscMode::Resonator);
  }

  void Reset(MiscMode mode) {
    excite_envL_ = excite_envR_ = 0.f;
    drive_lpL_   = drive_lpR_   = 0.f;
    crush_holdL_ = crush_holdR_ = 0.f;
    crush_lpL_   = crush_lpR_   = 0.f;
    crush_cnt_   = 0;
    if (mode == MiscMode::Pitch) {
      pitchL_.Init(sr_);
      pitchR_.Init(sr_);
    }
  }

  void Process(MiscMode mode, float dryL, float dryR,
               float p1, float p2, float p3, float& wetL, float& wetR) {
    using daisysp::fmap;
    using daisysp::Mapping;
    switch (mode) {
      case MiscMode::Resonator: {
        float base_freq = fmap(p1, 60.f, 1200.f, Mapping::LOG);
        float damping   = fmap(p2, 0.2f, 0.95f);
        float brighten  = fmap(p2, 0.2f, 1.0f);
        float in_level  = clampf(p3, 0.f, 1.f);
        const float w1  = 0.6f * (1.0f - 0.7f * brighten);
        const float w2  = 0.6f * (0.3f + 0.7f * brighten);

        bp1L_.SetFreq(base_freq);        bp1L_.SetRes(damping);
        bp1R_.SetFreq(base_freq);        bp1R_.SetRes(damping);
        bp2L_.SetFreq(base_freq * 1.5f); bp2L_.SetRes(damping * 0.9f);
        bp2R_.SetFreq(base_freq * 1.5f); bp2R_.SetRes(damping * 0.9f);

        const float att = 0.002f, rel = 0.0008f;
        float inl = dryL * in_level;
        float inr = dryR * in_level;
        float magL = fabsf(inl), magR = fabsf(inr);
        excite_envL_ += (magL - excite_envL_) * (magL > excite_envL_ ? att : rel);
        excite_envR_ += (magR - excite_envR_) * (magR > excite_envR_ ? att : rel);
        bp1L_.Process(inl * (1.0f + excite_envL_));
        bp2L_.Process(inl * (1.0f + excite_envL_));
        bp1R_.Process(inr * (1.0f + excite_envR_));
        bp2R_.Process(inr * (1.0f + excite_envR_));
        wetL = bp1L_.Band() * w1 + bp2L_.Band() * w2;
        wetR = bp1R_.Band() * w1 + bp2R_.Band() * w2;
      } break;

      case MiscMode::Pitch: {
        float semitones = floorf(fmap(p1, -12.f, 13.f)); // integer -12..+12
        uint32_t buf_sz = (uint32_t)fmap(p2, 1024.f, 16384.f);
        float    fun    = clampf(p3, 0.f, 1.f);
        pitchL_.SetTransposition(semitones); pitchR_.SetTransposition(semitones);
        pitchL_.SetDelSize(buf_sz);          pitchR_.SetDelSize(buf_sz);
        pitchL_.SetFun(fun);                 pitchR_.SetFun(fun);
        float inl = dryL, inr = dryR; // PitchShifter::Process takes a non-const ref
        wetL = pitchL_.Process(inl);
        wetR = pitchR_.Process(inr);
      } break;

      case MiscMode::Drive: { // p1 = drive, p2 = tone, p3 = output level
        float drive  = fmap(p1, 1.f, 24.f, Mapping::LOG);
        float tone_a = fmap(p2, 0.04f, 1.f); // post one-pole LP (1 = open)
        float level  = clampf(p3, 0.f, 1.f);
        float dl = tanhf(dryL * drive);
        float dr = tanhf(dryR * drive);
        drive_lpL_ += tone_a * (dl - drive_lpL_);
        drive_lpR_ += tone_a * (dr - drive_lpR_);
        wetL = drive_lpL_ * level;
        wetR = drive_lpR_ * level;
      } break;

      case MiscMode::Crush: { // p1 = bit depth, p2 = sample-rate reduce, p3 = tone
        float bits   = fmap(p1, 1.f, 16.f);
        float step   = 1.f / powf(2.f, bits);   // quantisation step
        int   hold   = (int)fmap(p2, 1.f, 40.f); // sample-and-hold length
        if (hold < 1) hold = 1;
        float tone_a = fmap(p3, 0.04f, 1.f);
        if (crush_cnt_ <= 0) {
          crush_cnt_   = hold;
          crush_holdL_ = floorf(dryL / step + 0.5f) * step;
          crush_holdR_ = floorf(dryR / step + 0.5f) * step;
        }
        crush_cnt_--;
        crush_lpL_ += tone_a * (crush_holdL_ - crush_lpL_);
        crush_lpR_ += tone_a * (crush_holdR_ - crush_lpR_);
        wetL = crush_lpL_;
        wetR = crush_lpR_;
      } break;
    }
  }

 private:
  // No in-class initializers — must stay trivially constructible for SDRAM.
  float sr_;
  daisysp::Svf bp1L_, bp2L_, bp1R_, bp2R_;
  daisysp::PitchShifter pitchL_, pitchR_;
  float excite_envL_, excite_envR_;
  float drive_lpL_, drive_lpR_;                 // Drive post-tone state
  float crush_holdL_, crush_holdR_, crush_lpL_, crush_lpR_; // Crush state
  int   crush_cnt_;
};

} // namespace mfx
