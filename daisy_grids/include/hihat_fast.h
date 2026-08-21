// HiHat, restructured so it can be afforded.
//
// The algorithm is Emilie Gillet's 808 hi-hat from Plaits
// (plaits/dsp/drums/hihat.h), ported to DaisySP by Ben Sergentanis. Original
// code MIT; as part of Sorrow this file is GPL-3.0-or-later, since Sorrow is
// copyleft as a whole because of Grids. The signal path is unchanged operation
// for operation - only the timing of the arithmetic differs.
//
// Same story as analog_snare_fast.h. DaisySP's Process() recomputes, per sample:
//
//   - three powf() calls, via SemitonesToRatio, for the two envelope decay
//     coefficients and the band-pass cutoff
//   - SetFreq()/SetRes() on the colouration filter and the high-pass, each of
//     which calls sinf() and powf() internally to recalculate damping
//
// All of it depends only on decay_ and tone_, which in Sorrow are set once when
// the kit is rolled and then hold for the life of the note. They move into the
// setters behind a dirty flag.
//
// This matters more than the snare did: every kit contains a hat, so the
// cheapest hat sets the floor on what a kit can cost. Measured on target at
// 32 kHz the DaisySP version cost 21% of a sample period, out of a 33% floor.
//
// The metallic noise sources and VCAs are DaisySP's own and are reused
// unchanged - the expense was never in them.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 David Baghurst
// SPDX-FileCopyrightText: 2016 Emilie Gillet
// SPDX-FileCopyrightText: 2020 Electrosmith, Corp

#pragma once

#include "Drums/hihat.h"
#include "Filters/svf.h"
#include "Utility/dsp.h"

#include <cmath>
#include <cstdint>

namespace sorrow
{

template <typename MetallicNoiseSource = daisysp::SquareNoise,
          typename VCA                 = daisysp::LinearVCA,
          bool resonance               = true>
class HiHatFast
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;

        trig_         = false;
        envelope_     = 0.0f;
        noise_clock_  = 0.0f;
        noise_sample_ = 0.0f;
        rng_          = 0x2545F491u;

        SetFreq(3000.0f);
        SetTone(0.5f);
        SetDecay(0.2f);
        SetNoisiness(0.8f);
        SetAccent(0.8f);

        metallic_noise_.Init(sample_rate_);
        noise_coloration_svf_.Init(sample_rate_);
        hpf_.Init(sample_rate_);

        dirty_ = true;
    }

    void Trig() { trig_ = true; }

    // Kept for interface compatibility; Sorrow only fires one-shots.
    void SetSustain(bool) {}

    void SetAccent(float accent)
    {
        accent_ = daisysp::fclamp(accent, 0.0f, 1.0f);
    }

    void SetFreq(float f0)
    {
        f0_ = daisysp::fclamp(f0 / sample_rate_, 0.0f, 1.0f);
        // f0_ feeds the noise clock rate, which is per-sample arithmetic, but
        // it also scales nothing in the filters - no coefficient rebuild needed.
    }

    void SetTone(float tone)
    {
        tone_  = daisysp::fclamp(tone, 0.0f, 1.0f);
        dirty_ = true;
    }

    void SetDecay(float decay)
    {
        decay_ = fmaxf(decay, 0.0f);
        decay_ *= 1.7f;
        decay_ -= 1.2f;
        dirty_ = true;
    }

    void SetNoisiness(float noisiness)
    {
        noisiness_ = daisysp::fclamp(noisiness, 0.0f, 1.0f);
        noisiness_ *= noisiness_;
    }

    float Process(bool trigger = false)
    {
        if(dirty_)
            UpdateCoefficients();

        if(trigger || trig_)
        {
            trig_ = false;
            envelope_
                = (1.5f + 0.5f * (1.0f - decay_)) * (0.3f + 0.7f * accent_);
        }

        // Metallic noise from the six square oscillators.
        float out = metallic_noise_.Process(2.0f * f0_);

        // Band-pass it. Coefficients were set when tone last changed.
        noise_coloration_svf_.Process(out);
        out = noise_coloration_svf_.Band();

        // Not part of the 808 circuit: a variable amount of clocked noise on
        // top of the six oscillators, for variety.
        float noise_f = f0_ * (16.0f + 16.0f * (1.0f - noisiness_));
        noise_f       = daisysp::fclamp(noise_f, 0.0f, 0.5f);

        noise_clock_ += noise_f;
        if(noise_clock_ >= 1.0f)
        {
            noise_clock_ -= 1.0f;
            noise_sample_ = RandFloat() - 0.5f;
        }
        out += noisiness_ * (noise_sample_ - out);

        // VCA
        VCA vca;
        envelope_ *= envelope_ > 0.5f ? envelope_decay_ : cut_decay_;
        out = vca(out, envelope_);

        hpf_.Process(out);
        return hpf_.High();
    }

  private:
    // Everything the original recomputed per sample, done once per change.
    void UpdateCoefficients()
    {
        dirty_ = false;

        envelope_decay_ = 1.0f - 0.003f * SemitonesToRatio(-decay_ * 84.0f);
        cut_decay_      = 1.0f - 0.0025f * SemitonesToRatio(-decay_ * 36.0f);

        float cutoff = 150.0f / sample_rate_ * SemitonesToRatio(tone_ * 72.0f);
        cutoff       = daisysp::fclamp(cutoff, 0.0f, 16000.0f / sample_rate_);

        noise_coloration_svf_.SetFreq(cutoff * sample_rate_);
        noise_coloration_svf_.SetRes(resonance ? 3.0f + 6.0f * tone_ : 1.0f);

        hpf_.SetFreq(cutoff * sample_rate_);
        hpf_.SetRes(0.5f);
    }

    static float SemitonesToRatio(float in)
    {
        return powf(2.0f, in * daisysp::kOneTwelfth);
    }

    // rand() was called whenever the noise clock ticked; an xorshift is
    // indistinguishable here and far cheaper.
    float RandFloat()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000);
    }

    float sample_rate_ = 48000.0f;
    float accent_ = 0.0f, f0_ = 0.0f, tone_ = 0.0f, decay_ = 0.0f,
          noisiness_ = 0.0f;
    bool  trig_      = false;
    bool  dirty_     = true;

    float envelope_     = 0.0f;
    float noise_clock_  = 0.0f;
    float noise_sample_ = 0.0f;

    // Cached by UpdateCoefficients()
    float envelope_decay_ = 0.0f;
    float cut_decay_      = 0.0f;

    MetallicNoiseSource metallic_noise_;
    daisysp::Svf        noise_coloration_svf_;
    daisysp::Svf        hpf_;

    uint32_t rng_ = 0x2545F491u;
};

} // namespace sorrow
