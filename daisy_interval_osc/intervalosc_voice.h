// interval_osc as a multiosc engine.
//
// Dual oscillator with a quantized interval/ratio offset, mapped onto the
// universal panel (docs/PANEL.md):
//   TUNE = base pitch (+ V/OCT)     short press = cycle waveform
//   Play : MOD1 = Interval/Ratio  MOD2 = Detune  MOD3 = Pulse width
//   Edit : MOD1 = Interval/Rational mode
// Continuous (VCO/drone): no envelope. Out L = osc 1, Out R = osc 2.
//
// DSP ported from daisy_interval_osc/IntervalOsc.cpp
// (Nick Donaldson, ndonald2/DaisyPatches, MIT). The standalone build is kept
// behind its own Makefile; this is the integrated form for daisy_multiosc.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "multiosc_core/engine.h"

namespace multiosc {

class IntervalOscVoice : public Engine {
  public:
    void Init(DaisyPatchSM& hw, float sample_rate) override;
    void Process(DaisyPatchSM&             hw,
                 const EngineContext&      ctx,
                 AudioHandle::InputBuffer  in,
                 AudioHandle::OutputBuffer out,
                 size_t                    size) override;

    const char* Name() const override { return "INTVL"; }
    const char* ModLabel(int idx, bool edit) const override;
    const char* ShortLabel() const override { return "WAVE"; }
    const char* Selection() const override;
    int         BlockSize() const override { return 48; }
    bool        DefaultEnvOn() const override { return false; } // VCO/drone

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
    daisysp::Adsr       env_;

    uint8_t wave_mode_      = 0;
    bool    harmonic_mode_  = false;
    float   sample_rate_    = 48000.f;
    float   smooth_coef_    = 0.2f;
    float   base_           = 56.f; // smoothed base note
    float   base_in_        = 56.f; // base-note target (held on the Edit page)
    float   offset_         = 0.f;  // smoothed offset / ratio
    float   offset_target_  = 0.f;  // held on the Edit page
    float   detune_         = 0.f;
    float   pw_             = 0.5f;
};

} // namespace multiosc
