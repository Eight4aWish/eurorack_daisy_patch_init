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
//   TUNE = rate (+ V/OCT)            short press = next formula
//   Play : MOD1 = A  MOD2 = B  MOD3 = Tone      (+ matching CV on each)
//   Edit : MOD1 = Bank  MOD2 = Func 2  MOD3 = Drone
//   TRIG (gate 1) = hard sync — restart the waveform at t = 0.
//   Out L = voice 1, Out R = voice 2. Free-running: no envelope by default.
//
// The Play page is Ogham's four pots exactly — Rate, A, B, Tone — and formula
// selection comes off the knobs onto the button, which is where Ogham puts it
// too (its Func encoder). Short press steps within the current family of twenty;
// the Edit page picks the family.
//
// DSP vendored unmodified from the Ogham module (Steven Collins, Keeos.io,
// https://github.com/keeos-io/ogham, MIT): bytebeat_engine.{h,cpp} and its
// hundred-formula bank formulas.{h,cpp}, which were found by machine search and
// are original to that project. Their copyright headers are intact. The Tone
// macro in lofi_tone.{h,cpp} is extracted and adapted from the same project's
// AudioPipeline — see that header for what changed. Ogham's FX chain,
// envelope-follower CV out and BPM clock are not ported — see README.md.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "multiosc_core/engine.h"
#include "bytebeat_engine.h"
#include "lofi_tone.h"

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
    LofiTone       tone_;
    LofiTone::State tone_st_[2]; // per voice, so the two never share history

    int bank_  = 0; // index into kBanks (short press)
    int func1_ = 0; // absolute bank index for voice 1
    int func2_ = 1; // absolute bank index for voice 2

    // Edit-page knobs change meaning between pages, so they hold until the
    // physical knob sweeps through the stored value (docs/PANEL.md).
    SoftTakeover st_bank_, st_func2_, st_drone_;

    float dc_x1_[2] = {0.f, 0.f}; // per-output DC blockers: a formula parked on
    float dc_y1_[2] = {0.f, 0.f}; // a constant is a DC level, not silence
};

} // namespace multiosc
