// interval_osc multiosc engine — DSP ported from IntervalOsc.cpp beside this
// file (Nick Donaldson, ndonald2/DaisyPatches). See intervalosc_voice.h for the
// split of authorship, and LICENSE-daisypatches.txt for the permission notice.
//
// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2020 andrewikenberry
// SPDX-FileCopyrightText: 2022 Nick Donaldson        (original IntervalOsc DSP)
// SPDX-FileCopyrightText: 2026 David Baghurst        (multiosc port)

#include "intervalosc_voice.h"
#include <cmath>

using namespace daisysp;

namespace multiosc {

// Just / harmonic ratios for the offset oscillator in Rational mode.
static const float kFreqRatios[] = {
    1.f / 4.f, 1.f / 2.f, 2.f / 3.f, 3.f / 4.f,                  // sub-octave
    1.f,       5.f / 4.f, 4.f / 3.f, 3.f / 2.f, 5.f / 3.f,       // 1st octave
    2.f,       5.f / 2.f, 8.f / 3.f, 3.f,       10.f / 3.f,      // 2nd octave
    4.f,       5.f,       6.f,       7.f,        8.f,            // 3-4 octave
    9.f,       10.f,      11.f,      12.f,       13.f, 14.f, 15.f, 16.f};
static const int kNumRatios = (int)(sizeof(kFreqRatios) / sizeof(float));

void IntervalOscVoice::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    for(int i = 0; i < 2; i++)
    {
        oscs_[i].Init(sample_rate_);
        oscs_[i].SetAmp(0.9f);
        oscs_[i].SetWaveform(BlOsc::WAVE_SAW);

        sin_oscs_[i].Init(sample_rate_);
        sin_oscs_[i].SetAmp(0.9f);
        sin_oscs_[i].SetWaveform(Oscillator::WAVE_SIN);
    }
    smooth_coef_ = 1.0f / (sample_rate_ * 0.0001f);
    wave_mode_   = 0;
    UpdateWaveforms();
}

void IntervalOscVoice::SetTapState(uint8_t s)
{
    wave_mode_ = s < MODE_LAST ? s : 0;
    UpdateWaveforms();
}

void IntervalOscVoice::NextWave()
{
    wave_mode_ = (wave_mode_ + 1) % MODE_LAST;
    UpdateWaveforms();
}

void IntervalOscVoice::UpdateWaveforms()
{
    switch(wave_mode_)
    {
        case MODE_SIN_SIN: break; // sine blend handled separately
        case MODE_TRI_TRI:
            oscs_[0].SetWaveform(BlOsc::WAVE_TRIANGLE);
            oscs_[1].SetWaveform(BlOsc::WAVE_TRIANGLE);
            break;
        case MODE_SAW_SAW:
        case MODE_SAW_SIN:
            oscs_[0].SetWaveform(BlOsc::WAVE_SAW);
            oscs_[1].SetWaveform(BlOsc::WAVE_SAW);
            break;
        case MODE_SAW_TRI:
            oscs_[0].SetWaveform(BlOsc::WAVE_SAW);
            oscs_[1].SetWaveform(BlOsc::WAVE_TRIANGLE);
            break;
        case MODE_SAW_RECT:
            oscs_[0].SetWaveform(BlOsc::WAVE_SAW);
            oscs_[1].SetWaveform(BlOsc::WAVE_SQUARE);
            break;
        case MODE_RECT_RECT:
        case MODE_RECT_SIN:
            oscs_[0].SetWaveform(BlOsc::WAVE_SQUARE);
            oscs_[1].SetWaveform(BlOsc::WAVE_SQUARE);
            break;
        case MODE_RECT_TRI:
            oscs_[0].SetWaveform(BlOsc::WAVE_SQUARE);
            oscs_[1].SetWaveform(BlOsc::WAVE_TRIANGLE);
            break;
        case MODE_RECT_SAW:
            oscs_[0].SetWaveform(BlOsc::WAVE_SQUARE);
            oscs_[1].SetWaveform(BlOsc::WAVE_SAW);
            break;
        default: break;
    }
}

float IntervalOscVoice::Osc2SinScale() const
{
    switch(wave_mode_)
    {
        case MODE_SIN_SIN:
        case MODE_SAW_SIN:
        case MODE_RECT_SIN: return 1.0f;
        default: return 0.0f;
    }
}

const char* IntervalOscVoice::PotLabel(int pot) const
{
    switch(pot)
    {
        case 1: return harmonic_mode_ ? "Rat" : "Intv";
        case 2: return "Det";
        case 3: return "PW";
        default: return "";
    }
}

const char* IntervalOscVoice::TapSelection() const
{
    static const char* kTag[MODE_LAST] = {"Sin",  "Tri",  "Saw",  "Sqr",
                                          "SwSi", "SwTr", "SwSq", "SqSi",
                                          "SqTr", "SqSw"};
    return kTag[wave_mode_];
}

void IntervalOscVoice::Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size)
{
    if(c.tap)
        NextWave();

    // Pot 1 the mode; pots 2-4 come with their jacks already added by the host.
    harmonic_mode_ = mode_.Process(c.pot[0]) == 1;
    const float offset_target
        = harmonic_mode_ ? roundf(fmap(c.pot[1], 0.f, kNumRatios - 1)) : roundf(fmap(c.pot[1], -24.f, 24.f));
    detune_ = fmap(c.pot[2], 0.f, 1.0f); // up to 1 semitone each way
    pw_     = c.pot[3];

    // Base oscillator pitch: V/OCT alone.
    fonepole(base_, c.note, smooth_coef_);
    const float base_freq = mtof(base_ - detune_);
    oscs_[0].SetFreq(base_freq);
    sin_oscs_[0].SetFreq(base_freq);

    // Offset oscillator: just-ratio (Rational) or semitone interval.
    if(harmonic_mode_)
    {
        int idx = (int)offset_target;
        if(idx < 0)
            idx = 0;
        if(idx > kNumRatios - 1)
            idx = kNumRatios - 1;
        fonepole(offset_, kFreqRatios[idx], smooth_coef_);
        const float f = mtof(base_ + detune_) * offset_;
        oscs_[1].SetFreq(f);
        sin_oscs_[1].SetFreq(f);
    }
    else
    {
        fonepole(offset_, offset_target, smooth_coef_);
        const float f = mtof(base_ + offset_ + detune_);
        oscs_[1].SetFreq(f);
        sin_oscs_[1].SetFreq(f);
    }

    oscs_[0].SetPw(pw_);
    oscs_[1].SetPw(pw_);

    const float ss1 = (wave_mode_ == MODE_SIN_SIN) ? 1.0f : 0.0f;
    const float ss2 = Osc2SinScale();
    for(size_t i = 0; i < size; i++)
    {
        const float o1
            = (1.0f - ss1) * oscs_[0].Process() + ss1 * sin_oscs_[0].Process();
        const float o2
            = (1.0f - ss2) * oscs_[1].Process() + ss2 * sin_oscs_[1].Process();
        // Sum both oscillators to both outputs so interval/detune is audible on
        // a single output. The host applies the envelope and volume.
        const float s = (o1 + o2) * 0.5f;
        out[0][i]     = s;
        out[1][i]     = s;
    }
}

} // namespace multiosc
