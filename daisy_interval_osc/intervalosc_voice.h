// interval_osc as a multiosc engine.
//
// Dual oscillator with a quantized interval/ratio offset, on the panel agreed with David on 2026-11-22
// (docs/PANEL.md):
//   pot 1  Mode: Interval (semitones) or Rational (just ratios)
//   pot 2  Interval, or the ratio in Rational mode      (+ the MOD 1 jack)
//   pot 3  Detune, up to a semitone each way            (+ the MOD 2 jack)
//   pot 4  Pulse width                                  (+ the MOD 3 jack)
//   tap    the waveform pair
// Pitch is V/OCT alone; the envelope and volume are the host's. Both oscillators are summed to both
// outputs, so the interval and detune are heard on one.
//
// The oscillator behaviour, the interval/rational modes and the ratio table are
// ported from IntervalOsc.cpp beside this file (Nick Donaldson,
// ndonald2/DaisyPatches). His copyright carries over with the DSP; the engine
// wrapper, panel mapping and soft-takeover are new. The standalone build is kept
// behind its own Makefile; this is the integrated form for daisy_multiosc.
//
// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2020 andrewikenberry
// SPDX-FileCopyrightText: 2022 Nick Donaldson        (original IntervalOsc DSP)
// SPDX-FileCopyrightText: 2026 David Baghurst        (multiosc port)
// See LICENSE-daisypatches.txt beside this file, and the repo LICENSE.

#pragma once

#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "multiosc_core/engine.h"

namespace multiosc {

class IntervalOscVoice : public Engine {
  public:
    void Init(float sample_rate) override;
    void Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size) override;

    const char* Name() const override { return "INTVL"; }
    const char* Choice() const override { return harmonic_mode_ ? "Ratl" : "Intv"; }
    const char* PotLabel(int pot) const override;
    const char* TapSelection() const override;
    uint8_t     TapState() const override { return wave_mode_; }
    void        SetTapState(uint8_t s) override;

  private:
    enum WaveMode {
        MODE_SIN_SIN,
        MODE_TRI_TRI,
        MODE_SAW_SAW,
        MODE_RECT_RECT,
        MODE_SAW_SIN,
        MODE_SAW_TRI,
        MODE_SAW_RECT,
        MODE_RECT_SIN,
        MODE_RECT_TRI,
        MODE_RECT_SAW,
        MODE_LAST
    };

    void  NextWave();
    void  UpdateWaveforms();
    float Osc2SinScale() const;

    daisysp::BlOsc      oscs_[2];
    daisysp::Oscillator sin_oscs_[2];

    uint8_t wave_mode_      = 0;
    bool    harmonic_mode_  = false;
    Stepper mode_{2};
    float   sample_rate_    = 48000.f;
    float   smooth_coef_    = 0.2f;
    float   base_           = 56.f; // smoothed base note
    float   offset_         = 0.f;  // smoothed offset / ratio
    float   detune_         = 0.f;
    float   pw_             = 0.5f;
};

} // namespace multiosc
