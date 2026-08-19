// Bytebeat oscillator engine for daisy_multiosc.
//
// Bytebeat: a formula in one integer variable `t` evaluated per tick, its low
// eight bits taken as the sample. The shifts and masks are periodic in powers of
// two, so a formula carries its own bar/beat structure — rhythm and melody out
// of arithmetic, with no oscillator, wavetable or envelope. Pitch is the rate at
// which `t` advances, so V/Oct is varispeed: shape preserved, everything
// transposed together.
//
// Universal panel (docs/PANEL.md):
//   TUNE = rate (+ V/OCT)            short press = cycle bank
//   Play : MOD1 = A  MOD2 = B  MOD3 = Func 1 (within bank)
//   Edit : MOD1 = Func 2 (whole bank)  MOD2 = Grid (A/B interp)  MOD3 = Drone
//   TRIG (gate 1) = hard sync — restart the waveform at t = 0.
//   Out L = voice 1, Out R = voice 2. Free-running: no envelope by default.
//
// DSP vendored unmodified from the Ogham module (Steven Collins, Keeos.io,
// https://github.com/keeos-io/ogham, MIT): bytebeat_engine.{h,cpp} and its
// hundred-formula bank formulas.{h,cpp}, which were found by machine search and
// are original to that project. Their copyright headers are intact; only this
// wrapper is new. Ogham's FX chain, envelope-follower CV out and BPM clock are
// not ported here — see README.md.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "multiosc_core/engine.h"
#include "bytebeat_engine.h"

namespace multiosc {

class BytebeatVoice : public Engine {
  public:
    void Init(DaisyPatchSM& hw, float sample_rate) override;
    void Process(DaisyPatchSM&             hw,
                 const EngineContext&      ctx,
                 AudioHandle::InputBuffer  in,
                 AudioHandle::OutputBuffer out,
                 size_t                    size) override;

    const char* Name() const override { return "BEAT"; }
    const char* ModLabel(int idx, bool edit) const override;
    const char* ShortLabel() const override { return "BANK"; }
    const char* Selection() const override;
    bool        DefaultEnvOn() const override { return false; } // free-running

  private:
    // The bank ships in five families of twenty (formulas.cpp), plus the A440
    // tuning reference in its own slot. Short press steps between them; MOD3
    // picks within the current one. The table itself lives in the .cpp so it
    // needs no out-of-class definition.
    struct Bank {
        const char* name;
        int         first;
        int         count;
    };
    static const Bank& BankAt(int i);
    static const int   kNumBanks = 6;

    // The vendored engine hardcodes a 48 kHz output rate; if the host ever runs
    // this engine at another rate, scale the requested rate to keep ticks per
    // second — and so pitch — correct, rather than editing vendored code.
    float sr_scale_ = 1.0f;

    BytebeatEngine engine_;

    int bank_  = 0; // index into kBanks (short press)
    int func1_ = 0; // absolute bank index for voice 1
    int func2_ = 1; // absolute bank index for voice 2

    // Edit-page knobs change meaning between pages, so they hold until the
    // physical knob sweeps through the stored value (docs/PANEL.md).
    SoftTakeover st_func2_, st_grid_, st_drone_;

    float dc_x1_[2] = {0.f, 0.f}; // per-output DC blockers: a formula parked on
    float dc_y1_[2] = {0.f, 0.f}; // a constant is a DC level, not silence
};

} // namespace multiosc
