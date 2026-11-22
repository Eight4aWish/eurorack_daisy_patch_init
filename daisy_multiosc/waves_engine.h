// Plain waveforms engine: one oscillator, the four classic shapes.
//
// Grown from the SINE test voice. A general-purpose VCO for the jobs the rest
// of the rack is too clever for: a clean sine to tune against, a saw or square
// into a filter, a triangle to fold.
//
// The panel (docs/PANEL.md, agreed with David on 2026-11-22):
//   pot 1  Shape: Sin, Tri, Saw, Sqr
//   pot 2  Pulse width, square only: anticlockwise a square, clockwise a 5% pulse   (+ the MOD 1 jack)
//   pots 3, 4  free
// Pitch is V/OCT alone; the envelope (a drone until the first gate) and volume are the host's.
// OUT L = OUT R.
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
    void Init(float sample_rate) override
    {
        osc_.Init(sample_rate);
        osc_.SetAmp(1.0f);
        SetShape(0);
    }

    void Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size) override
    {
        const int shape = shape_pot_.Process(c.pot[0]);
        if(shape != shape_)
            SetShape(static_cast<uint8_t>(shape));

        float freq = daisysp::mtof(c.note);
        if(freq < 20.0f)
            freq = 20.0f; // no sub-audio rumble
        if(freq > 8000.0f)
            freq = 8000.0f;
        osc_.SetFreq(freq);
        // Fully anticlockwise = square, clockwise narrows to a 5% pulse.
        osc_.SetPw(0.5f - 0.45f * c.pot[1]);

        for(size_t i = 0; i < size; i++)
        {
            const float s = osc_.Process() * level_;
            out[0][i]     = s;
            out[1][i]     = s;
        }
    }

    const char* Name() const override { return "WAVES"; }
    const char* Choice() const override
    {
        static const char* kTag[kShapes] = {"Sin", "Tri", "Saw", "Sqr"};
        return kTag[shape_];
    }
    const char* PotLabel(int pot) const override { return (pot == 1 && shape_ == 3) ? "PW" : "-"; }

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
    Stepper             shape_pot_{kShapes};
    uint8_t             shape_ = 0;
    float               level_ = 1.0f;
};

} // namespace multiosc
