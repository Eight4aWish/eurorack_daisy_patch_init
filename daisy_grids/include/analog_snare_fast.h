// AnalogSnareDrum, restructured so it can be afforded.
//
// The algorithm is Emilie Gillet's 808 snare model from Plaits
// (plaits/dsp/drums/analog_snare_drum.h), ported to DaisySP by Ben Sergentanis.
// Original code MIT; as part of Sorrow this file is GPL-3.0-or-later, since
// Sorrow is copyleft as a whole because of Grids. Nothing about the sound is
// changed here - the signal path is the same operation for operation.
//
// What changed is when the arithmetic happens. DaisySP's Process() recomputes,
// for every single sample:
//
//   - two powf() calls, for the resonator Q and the noise envelope decay
//   - SetFreq() and SetRes() on five mode resonators and one noise filter,
//     and each of those calls sinf() and/or powf() internally to recalculate
//     the filter's damping
//
// That is roughly six sinf and fourteen powf per sample. Measured on the target
// at 32 kHz it cost 25% of a sample period - enough that a kit containing it
// overran the audio callback, which starves SysTick, which stops
// System::GetNow() advancing, which freezes Switch::Debounce(). The module kept
// playing while both panel switches died.
//
// Every one of those values depends only on freq, tone, decay, snappy and
// accent, none of which change while a note rings - they are set once when the
// kit is rolled. So they move into the setters behind a dirty flag, and Process
// does only the per-sample work. rand() also goes, replaced by an inline
// xorshift: newlib's rand() is not cheap and is called every sample.
//
// Sustain mode is dropped. Sorrow only ever fires one-shots, and the sustain
// path costs a sin() per mode per sample.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 David Baghurst
// SPDX-FileCopyrightText: 2016 Emilie Gillet
// SPDX-FileCopyrightText: 2020 Electrosmith, Corp

#pragma once

#include "Filters/svf.h"
#include "Utility/dsp.h"

#include <cmath>
#include <cstdint>

namespace sorrow
{

class AnalogSnareFast
{
  public:
    static constexpr int kNumModes = 5;

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;

        trig_                    = false;
        pulse_remaining_samples_ = 0;
        pulse_                   = 0.0f;
        pulse_height_            = 0.0f;
        pulse_lp_                = 0.0f;
        noise_envelope_          = 0.0f;
        rng_                     = 0x1234567u;

        SetAccent(0.6f);
        SetFreq(200.0f);
        SetDecay(0.3f);
        SetSnappy(0.7f);
        SetTone(0.5f);

        for(int i = 0; i < kNumModes; ++i)
            resonator_[i].Init(sample_rate_);
        noise_filter_.Init(sample_rate_);

        pulse_duration_   = static_cast<int>(1.0e-3f * sample_rate_);
        pulse_decay_time_ = 0.1e-3f * sample_rate_;

        dirty_ = true;
    }

    void Trig() { trig_ = true; }

    // Kept so the model matches the interface the voice pool expects. Sorrow
    // never sustains, and this fork does not implement it.
    void SetSustain(bool) {}

    void SetAccent(float accent)
    {
        accent_ = daisysp::fclamp(accent, 0.0f, 1.0f);
    }

    void SetFreq(float f0)
    {
        f0_    = daisysp::fclamp(f0 / sample_rate_, 0.0f, 0.4f);
        dirty_ = true;
    }

    void SetTone(float tone)
    {
        tone_  = daisysp::fclamp(tone, 0.0f, 1.0f) * 2.0f;
        dirty_ = true;
    }

    void SetDecay(float decay)
    {
        decay_ = decay;
        dirty_ = true;
    }

    void SetSnappy(float snappy)
    {
        snappy_ = daisysp::fclamp(snappy, 0.0f, 1.0f);
        dirty_  = true;
    }

    float Process(bool trigger = false)
    {
        if(dirty_)
            UpdateCoefficients();

        if(trigger || trig_)
        {
            trig_                    = false;
            pulse_remaining_samples_ = pulse_duration_;
            pulse_height_            = 3.0f + 7.0f * accent_;
            noise_envelope_          = 2.0f;
        }

        // Q45 / Q46
        float pulse = 0.0f;
        if(pulse_remaining_samples_)
        {
            --pulse_remaining_samples_;
            pulse  = pulse_remaining_samples_ ? pulse_height_ : pulse_height_ - 1.0f;
            pulse_ = pulse;
        }
        else
        {
            pulse_ *= 1.0f - 1.0f / pulse_decay_time_;
            pulse = pulse_;
        }

        // R189 / C57 / R190 + C58 / C59 / R197 / R196 / IC14
        pulse_lp_ = daisysp::fclamp(pulse_lp_, pulse, 0.75f);

        float shell = 0.0f;
        for(int i = 0; i < kNumModes; ++i)
        {
            const float excitation
                = i == 0 ? (pulse - pulse_lp_) + 0.006f * pulse : 0.026f * pulse;
            resonator_[i].Process(excitation);
            shell += gain_[i] * (resonator_[i].Band() + excitation * exciter_leak_);
        }
        shell = SoftClip(shell);

        // C56 / R194 / Q48 / C54 / R188 / D54
        float noise = 2.0f * RandFloat() - 1.0f;
        if(noise < 0.0f)
            noise = 0.0f;
        noise_envelope_ *= noise_env_decay_;
        noise *= noise_envelope_ * snappy_clamped_ * 2.0f;

        // C66 / R201 / C67 / R202 / R203 / Q49
        noise_filter_.Process(noise);
        noise = noise_filter_.Band();

        // IC13
        return noise + shell * (1.0f - snappy_clamped_);
    }

  private:
    // Everything the original recomputed per sample. Called once per parameter
    // change instead, which in Sorrow means once per kit roll.
    void UpdateCoefficients()
    {
        dirty_ = false;

        static const float kModeFrequencies[kNumModes]
            = {1.00f, 2.00f, 3.18f, 4.16f, 5.62f};

        const float decay_xt = decay_ * (1.0f + decay_ * (decay_ - 1.0f));
        const float q = 2000.0f * powf(2.0f, daisysp::kOneTwelfth * decay_xt * 84.0f);

        noise_env_decay_
            = 1.0f
              - 0.0017f
                    * powf(2.0f,
                           daisysp::kOneTwelfth * (-decay_ * (50.0f + snappy_ * 10.0f)));

        exciter_leak_ = snappy_ * (2.0f - snappy_) * 0.1f;
        snappy_clamped_ = daisysp::fclamp(snappy_ * 1.1f - 0.05f, 0.0f, 1.0f);

        for(int i = 0; i < kNumModes; ++i)
        {
            const float f = fminf(f0_ * kModeFrequencies[i], 0.499f);
            resonator_[i].SetFreq(f * sample_rate_);
            resonator_[i].SetRes((f * (i == 0 ? q : q * 0.25f)) * 0.2f);
        }

        float tone = tone_;
        if(tone < 0.666667f)
        {
            // 808-style (2 modes)
            tone *= 1.5f;
            gain_[0] = 1.5f + (1.0f - tone) * (1.0f - tone) * 4.5f;
            gain_[1] = 2.0f * tone + 0.15f;
            for(int i = 2; i < kNumModes; ++i)
                gain_[i] = 0.0f;
        }
        else
        {
            // What the 808 could have been with extra modes
            tone     = (tone - 0.666667f) * 3.0f;
            gain_[0] = 1.5f - tone * 0.5f;
            gain_[1] = 2.15f - tone * 0.7f;
            for(int i = 2; i < kNumModes; ++i)
            {
                gain_[i] = tone;
                tone *= tone;
            }
        }

        const float f_noise = f0_ * 16.0f;
        noise_filter_.SetFreq(f_noise * sample_rate_);
        noise_filter_.SetRes(f_noise * 1.5f);
    }

    // newlib's rand() is called once per sample by the original and is not
    // cheap; an inline xorshift is indistinguishable here and nearly free.
    float RandFloat()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000);
    }

    static float SoftLimit(float x)
    {
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }
    static float SoftClip(float x)
    {
        if(x < -3.0f)
            return -1.0f;
        if(x > 3.0f)
            return 1.0f;
        return SoftLimit(x);
    }

    float sample_rate_ = 48000.0f;
    float f0_ = 0.0f, tone_ = 0.0f, accent_ = 0.0f, snappy_ = 0.0f, decay_ = 0.0f;
    bool  trig_  = false;
    bool  dirty_ = true;

    int   pulse_remaining_samples_ = 0;
    int   pulse_duration_          = 48;
    float pulse_decay_time_        = 4.8f;
    float pulse_                   = 0.0f;
    float pulse_height_            = 0.0f;
    float pulse_lp_                = 0.0f;
    float noise_envelope_          = 0.0f;

    // Cached by UpdateCoefficients()
    float gain_[kNumModes]  = {};
    float exciter_leak_     = 0.0f;
    float snappy_clamped_   = 0.0f;
    float noise_env_decay_  = 0.0f;

    daisysp::Svf resonator_[kNumModes];
    daisysp::Svf noise_filter_;

    uint32_t rng_ = 0x1234567u;
};

} // namespace sorrow
