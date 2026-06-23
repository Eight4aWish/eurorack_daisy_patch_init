// Minimal test engine: a single sine oscillator. Exists to prove the boot menu
// and engine switching with >1 entry; replace/keep as a simple reference.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisysp.h"
#include "multiosc_core/engine.h"

namespace multiosc {

class SineEngine : public Engine {
  public:
    void Init(DaisyPatchSM& hw, float sample_rate) override
    {
        (void)hw;
        osc_.Init(sample_rate);
        osc_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        osc_.SetAmp(0.5f);
    }

    void Process(DaisyPatchSM&             hw,
                 const EngineContext&      ctx,
                 AudioHandle::InputBuffer  in,
                 AudioHandle::OutputBuffer out,
                 size_t                    size) override
    {
        (void)in;
        (void)ctx;
        const float tune = hw.GetAdcValue(KNOB_TUNE);
        const float cv   = hw.GetAdcValue(CV_VOCT);
        float freq = 55.0f * powf(2.0f, tune * 5.0f + (cv * 10.0f - 5.0f));
        if(freq < 20.0f)
            freq = 20.0f; // no sub-audio rumble
        if(freq > 8000.0f)
            freq = 8000.0f;
        osc_.SetFreq(freq);

        // Gated: silent at rest, slewed amplitude so note on/off never clicks.
        const bool  gate   = hw.gate_in_1.State() || hw.gate_in_1.Trig();
        const float target = gate ? 0.5f : 0.0f;
        for(size_t i = 0; i < size; i++)
        {
            amp_ += (target - amp_) * 0.004f; // ~5 ms slew
            const float s = osc_.Process() * amp_;
            out[0][i]     = s;
            out[1][i]     = s;
        }
    }

    const char* Name() const override { return "SINE"; }
    const char* ModLabel(int, bool) const override { return "-"; }
    const char* ShortLabel() const override { return ""; }
    const char* Selection() const override { return ""; }

  private:
    daisysp::Oscillator osc_;
    float               amp_ = 0.0f;
};

} // namespace multiosc
