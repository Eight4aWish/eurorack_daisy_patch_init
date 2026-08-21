// ModalVoice with a settable mode count.
//
// The algorithm is Emilie Gillet's modal resonator from Rings/Elements, ported
// to DaisySP by Ben Sergentanis. Original MIT; as part of Sorrow this file is
// GPL-3.0-or-later. The signal path is unchanged.
//
// Unlike analog_snare_fast.h and hihat_fast.h, this is *not* a
// coefficient-hoisting exercise. Those two spent their budget recomputing
// filter coefficients every sample - six sinf and fourteen powf in the snare's
// case - for values that changed once per note, and hoisting bought a 5x
// saving. ModalVoice is not like that: reading it, only one powf and a hundred
// or so flops of setup are hoistable, perhaps 2-4% out of its 38%. The rest is
// the mode bank actually filtering, which is real work and cannot be moved.
//
// What can be changed is how many modes there are. DaisySP hardcodes
// resonator_.Init(0.015f, 24, sr) and cost scales with that count, so the
// number is the lever. Measured on the host:
//
//     modes    ns/sample    ring ms    tonality
//         6         26.8        164        0.85
//         8         28.0        118        0.84
//        12         31.2        114        0.83
//        24         48.3        109        0.78
//
// Cost falls by about a third from 24 to 12, which is the point: at 38% of a
// sample period two modal voices could not share a kit, and at roughly 24% they
// can.
//
// The timbre goes the *opposite* way to what I assumed writing this. I expected
// fewer partials to read as more percussive and less bell-like; in fact fewer
// modes measure slightly *more* tonal (0.83 at twelve against 0.78 at
// twenty-four), because there are fewer inharmonic components to blur the
// pitch. Twelve is chosen as the point where the cost goal is met while staying
// nearest the twenty-four-mode character, which is the sound worth keeping.
//
// The small hoist is done too, since the fork exists anyway: everything derived
// from brightness, damping, structure and accent is recomputed on a parameter
// change or a trigger, not per sample.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 David Baghurst
// SPDX-FileCopyrightText: 2016 Emilie Gillet
// SPDX-FileCopyrightText: 2020 Electrosmith, Corp

#pragma once

#include "PhysicalModeling/resonator.h"
#include "Utility/dsp.h"

#include <cmath>

namespace sorrow
{

template <int kModes = 12>
class ModalVoiceFast
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        trig_        = false;

        excitation_filter_.Init();
        resonator_.Init(0.015f, kModes, sample_rate_);

        SetFreq(440.0f);
        SetStructure(0.5f);
        SetBrightness(0.5f);
        SetDamping(0.25f);
        SetAccent(0.8f);
        dirty_ = true;
    }

    void Trig()
    {
        trig_  = true;
        dirty_ = true;   // accent feeds the derived values
    }

    void SetSustain(bool) {}   // one-shot only; the sustain path costs a Dust generator

    void SetAccent(float accent)
    {
        accent_ = daisysp::fclamp(accent, 0.0f, 1.0f);
        dirty_  = true;
    }
    void SetFreq(float freq)
    {
        f0_ = daisysp::fclamp(freq / sample_rate_, 0.0f, 0.499f);
        resonator_.SetFreq(freq);
        dirty_ = true;
    }
    void SetStructure(float structure)
    {
        structure_ = daisysp::fclamp(structure, 0.0f, 1.0f);
        resonator_.SetStructure(structure_);
        dirty_ = true;
    }
    void SetBrightness(float brightness)
    {
        brightness_ = daisysp::fclamp(brightness, 0.0f, 1.0f);
        dirty_      = true;
    }
    void SetDamping(float damping)
    {
        damping_ = daisysp::fclamp(damping, 0.0f, 1.0f);
        dirty_   = true;
    }

    float Process(bool trigger = false)
    {
        if(dirty_)
            Update();

        float temp = 0.0f;
        if(trigger || trig_)
        {
            const float attenuation = 1.0f - damping_acc_ * 0.5f;
            const float amplitude   = (0.12f + 0.08f * accent_) * attenuation;
            temp  = amplitude
                   * powf(2.0f, daisysp::kOneTwelfth * (cutoff_ * cutoff_ * 24.0f))
                   / cutoff_;
            trig_ = false;
        }

        const float one = 1.0f;
        excitation_filter_.template Process<daisysp::ResonatorSvf<1>::LOW_PASS,
                                            false>(&cutoff_, &q_, &one, temp, &temp);

        return resonator_.Process(temp);
    }

  private:
    void Update()
    {
        dirty_ = false;

        brightness_acc_ = brightness_ + 0.25f * accent_ * (1.0f - brightness_);
        damping_acc_    = damping_ + 0.25f * accent_ * (1.0f - damping_);

        constexpr float kRange = 60.0f;   // the one-shot value; sustain would use 36
        cutoff_ = fminf(2.0f * f0_
                            * powf(2.0f,
                                   daisysp::kOneTwelfth
                                       * ((brightness_acc_ * (2.0f - brightness_acc_)
                                           - 0.5f)
                                          * kRange)),
                        0.499f);
        q_ = 1.5f;

        resonator_.SetBrightness(brightness_acc_);
        resonator_.SetDamping(damping_acc_);
    }

    float sample_rate_ = 48000.0f;
    bool  trig_        = false;
    bool  dirty_       = true;

    float f0_ = 0.0f, structure_ = 0.0f, brightness_ = 0.0f, damping_ = 0.0f;
    float accent_ = 0.0f;

    // Derived by Update()
    float brightness_acc_ = 0.0f;
    float damping_acc_    = 0.0f;
    float cutoff_         = 0.1f;
    float q_              = 1.5f;

    daisysp::ResonatorSvf<1> excitation_filter_;
    daisysp::Resonator       resonator_;
};

} // namespace sorrow
