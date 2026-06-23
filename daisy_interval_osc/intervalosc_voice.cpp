// interval_osc multiosc engine — DSP ported from IntervalOsc.cpp
// (Nick Donaldson, ndonald2/DaisyPatches, MIT).
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

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

void IntervalOscVoice::Init(DaisyPatchSM& hw, float sample_rate)
{
    (void)hw;
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

    env_.Init(sample_rate_);
    env_.SetTime(daisysp::ADSR_SEG_ATTACK, 0.01f);
    env_.SetTime(daisysp::ADSR_SEG_DECAY, 0.05f);
    env_.SetSustainLevel(1.0f); // AR behaviour (hold while gated)
    env_.SetTime(daisysp::ADSR_SEG_RELEASE, 0.3f);
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

const char* IntervalOscVoice::ModLabel(int idx, bool edit) const
{
    if(edit)
    {
        switch(idx)
        {
            case 0: return harmonic_mode_ ? "Ratl" : "Intv";
            case 1: return "Atk";
            case 2: return "Rel";
            default: return "";
        }
    }
    switch(idx)
    {
        case 0: return harmonic_mode_ ? "Rat" : "Intv";
        case 1: return "Det";
        case 2: return "PW";
        default: return "";
    }
}

const char* IntervalOscVoice::Selection() const
{
    static const char* kTag[MODE_LAST] = {"Sin",  "Tri",  "Saw",  "Sqr",
                                          "SwSi", "SwTr", "SwSq", "SqSi",
                                          "SqTr", "SqSw"};
    return kTag[wave_mode_];
}

void IntervalOscVoice::Process(DaisyPatchSM&             hw,
                               const EngineContext&      ctx,
                               AudioHandle::InputBuffer  in,
                               AudioHandle::OutputBuffer out,
                               size_t                    size)
{
    (void)in;

    if(ctx.short_press)
        NextWave();

    if(ctx.edit_page)
    {
        // Edit (pitch held; TUNE is the host env on/off control):
        // MOD1 = Interval/Rational mode, MOD2 = Attack, MOD3 = Release.
        harmonic_mode_ = hw.GetAdcValue(KNOB_MOD1) >= 0.5f;
        env_.SetTime(daisysp::ADSR_SEG_ATTACK,
                     fmap(hw.GetAdcValue(KNOB_MOD2), 0.002f, 0.5f));
        env_.SetTime(daisysp::ADSR_SEG_RELEASE,
                     fmap(hw.GetAdcValue(KNOB_MOD3), 0.01f, 1.5f));
    }
    else
    {
        const float k_tune  = hw.GetAdcValue(KNOB_TUNE);
        const float cv_voct = hw.GetAdcValue(CV_VOCT);
        const float k1      = hw.GetAdcValue(KNOB_MOD1);
        const float k2      = hw.GetAdcValue(KNOB_MOD2);
        const float k3      = hw.GetAdcValue(KNOB_MOD3);

        base_in_ = fmap(k_tune, 28.f, 84.f) + cv_voct * 60.f;

        float off = harmonic_mode_ ? roundf(fmap(k1, 0.f, kNumRatios - 1))
                                   : roundf(fmap(k1, -24.f, 24.f));
        off += roundf(hw.GetAdcValue(CV_MOD1) * (kNumRatios - 1));
        offset_target_ = off;
        detune_        = fmap(k2, 0.f, 1.0f); // up to 1 semitone each way
        pw_            = fclamp(k3 + hw.GetAdcValue(CV_MOD3), 0.f, 1.f);
    }

    // Base oscillator pitch (held on the Edit page).
    fonepole(base_, base_in_, smooth_coef_);
    const float base_freq = mtof(base_ - detune_);
    oscs_[0].SetFreq(base_freq);
    sin_oscs_[0].SetFreq(base_freq);

    // Offset oscillator: just-ratio (Rational) or semitone interval.
    if(harmonic_mode_)
    {
        int idx = (int)offset_target_;
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
        fonepole(offset_, offset_target_, smooth_coef_);
        const float f = mtof(base_ + offset_ + detune_);
        oscs_[1].SetFreq(f);
        sin_oscs_[1].SetFreq(f);
    }

    oscs_[0].SetPw(pw_);
    oscs_[1].SetPw(pw_);

    // Gate the (optional) envelope. When off, this is a continuous drone.
    const bool  gate = hw.gate_in_1.State() || hw.gate_in_1.Trig();
    const float ss1  = (wave_mode_ == MODE_SIN_SIN) ? 1.0f : 0.0f;
    const float ss2  = Osc2SinScale();
    for(size_t i = 0; i < size; i++)
    {
        const float o1
            = (1.0f - ss1) * oscs_[0].Process() + ss1 * sin_oscs_[0].Process();
        const float o2
            = (1.0f - ss2) * oscs_[1].Process() + ss2 * sin_oscs_[1].Process();
        const float env_amp = ctx.env_on ? env_.Process(gate) : 1.0f;
        // Sum both oscillators to both outputs so interval/detune is audible on
        // a single output.
        const float s = (o1 + o2) * 0.5f * env_amp;
        out[0][i]     = s;
        out[1][i]     = s;
    }
}

} // namespace multiosc
