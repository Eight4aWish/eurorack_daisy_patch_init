// Plain waveforms engine: one oscillator, the four classic shapes.
//
// Grown from the SINE test voice. A general-purpose VCO for the jobs the rest
// of the rack is too clever for: a clean sine to tune against, a saw or square
// into a filter, a triangle to fold.
//
// Universal panel (docs/PANEL.md):
//   TUNE = pitch (+V/Oct)            short press = shape (Sin / Tri / Saw / Sqr)
//   Play : MOD1 = pulse width (Sqr only; + MOD1 CV)   MOD2, MOD3 unused
//   Edit : nothing beyond the host's env on/off on TUNE
//   OUT L = OUT R (mono)
//   Env off = continuous VCO (default); env on = gated by TRIG
//
// Saw and square are polyBLEP. The triangle is the naive one: DaisySP's polyBLEP
// triangle is a leaky integrator whose shape bends with pitch, and a triangle's
// harmonics fall fast enough that the naive one barely aliases.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisysp.h"
#include "multiosc_core/engine.h"

namespace multiosc {

class WavesEngine : public Engine {
  public:
    void Init(DaisyPatchSM& hw, float sample_rate) override
    {
        (void)hw;
        osc_.Init(sample_rate);
        osc_.SetAmp(1.0f);
        SetShape(0);
    }

    void Process(DaisyPatchSM&             hw,
                 const EngineContext&      ctx,
                 AudioHandle::InputBuffer  in,
                 AudioHandle::OutputBuffer out,
                 size_t                    size) override
    {
        (void)in;

        if(ctx.short_press)
            SetShape((shape_ + 1) % kShapes);

        // Pitch and width held on the Edit page, where TUNE is the env toggle.
        if(!ctx.edit_page)
        {
            const float midi = daisysp::fmap(hw.GetAdcValue(KNOB_TUNE), 28.f, 88.f)
                               + hw.GetAdcValue(CV_VOCT) * 60.f;
            float freq = daisysp::mtof(midi);
            if(freq < 20.0f)
                freq = 20.0f; // no sub-audio rumble
            if(freq > 8000.0f)
                freq = 8000.0f;
            osc_.SetFreq(freq);

            // Fully anticlockwise = square, clockwise narrows to a 5% pulse.
            const float w = daisysp::fclamp(
                hw.GetAdcValue(KNOB_MOD1) + hw.GetAdcValue(CV_MOD1), 0.f, 1.f);
            osc_.SetPw(0.5f - 0.45f * w);
        }

        // env on: gated (silent at rest); env off: continuous.
        // Slewed amplitude so note on/off never clicks.
        const bool  gate   = hw.gate_in_1.State() || hw.gate_in_1.Trig();
        const float target = ctx.env_on ? (gate ? 0.5f : 0.0f) : 0.5f;
        for(size_t i = 0; i < size; i++)
        {
            amp_ += (target - amp_) * 0.004f; // ~5 ms slew
            const float s = osc_.Process() * level_ * amp_;
            out[0][i]     = s;
            out[1][i]     = s;
        }
    }

    const char* Name() const override { return "WAVES"; }
    const char* ModLabel(int idx, bool edit) const override
    {
        return (!edit && idx == 0 && shape_ == 3) ? "PW" : "-";
    }
    const char* ShortLabel() const override { return "WAVE"; }
    const char* Selection() const override
    {
        static const char* kTag[kShapes] = {"Sin", "Tri", "Saw", "Sqr"};
        return kTag[shape_];
    }
    bool DefaultEnvOn() const override { return false; } // VCO

  private:
    static constexpr int kShapes = 4;

    void SetShape(uint8_t s)
    {
        static const uint8_t kWave[kShapes]
            = {daisysp::Oscillator::WAVE_SIN,
               daisysp::Oscillator::WAVE_TRI,
               daisysp::Oscillator::WAVE_POLYBLEP_SAW,
               daisysp::Oscillator::WAVE_POLYBLEP_SQUARE};
        shape_ = s;
        osc_.SetWaveform(kWave[s]);
        // DaisySP scales its polyBLEP square by 0.707; undo it so every shape
        // peaks at the same level, as on a hardware VCO.
        level_ = s == 3 ? 1.0f / 0.707f : 1.0f;
    }

    daisysp::Oscillator osc_;
    uint8_t             shape_ = 0;
    float               level_ = 1.0f;
    float               amp_   = 0.0f;
};

} // namespace multiosc
