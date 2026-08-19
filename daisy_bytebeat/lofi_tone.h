// Lo-fi Tone macro — one bipolar knob, clean at noon.
//
//   CCW  2-pole low-pass sweeping shut, then drive -> wavefold -> saturation
//   noon clean (deadzone)
//   CW   sample-rate reduction, soft saturation, overdrive, and a resonant
//        band-pass sweeping up as the throw opens
//
// Extracted and adapted from the Ogham module's AudioPipeline (Steven Collins,
// Keeos.io, https://github.com/keeos-io/ogham, MIT), where it is the `Tone` pot.
// The staging and its tuned constants are carried over as they are; what changed
// for this hardware:
//
//   - Pot calibration. Ogham measures its own pots (centre 0.458, full CW
//     0.9597) because a 10k pull-down makes them non-linear, and averages that
//     across its fleet. libDaisy hands us a clean 0..1, so centre is 0.5 and the
//     ends are the ends. Copying Ogham's numbers would sit the clean deadzone
//     visibly left of noon.
//   - The one-pole high-pass stage is dropped: SetLofiMacro never enables it in
//     the shipping firmware (all three branches leave its coefficient at zero).
//   - Its live-tunable g_lofiConfig, which the SWD monitor writes over, is baked
//     to constants here.
//   - The band-pass coefficient takes the sample rate rather than assuming 48k.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE), adapting MIT code
// (c) 2026 Steven Collins.

#pragma once

namespace multiosc {

class LofiTone {
  public:
    void Init(float sample_rate);

    // Set from a 0..1 knob. Cheap enough to call per block; does the powf work.
    void SetMacro(float pot);

    // True while the knob sits in the clean deadzone (nothing is processed).
    bool IsClean() const { return clean_; }

    // One sample through the chain. `s` is the per-voice filter state, so the
    // two voices are processed identically without sharing history.
    struct State {
        float lp1 = 0.f, lp2 = 0.f; // 2-pole low-pass (CCW)
        float srr_held = 0.f;       // sample-and-hold (CW)
        int   srr_count = 0;
        float svf_lp = 0.f, svf_bp = 0.f; // band-pass SVF (CW)
    };
    float Process(float v, State& s) const;

  private:
    static float Wavefold(float x);

    float sample_rate_ = 48000.f;
    bool  clean_       = true;

    // CCW
    float lp_coeff_   = 1.0f; // 1 = open
    float drive_gain_ = 1.0f;
    float fold_mix_   = 0.0f;
    // CW
    int   srr_factor_ = 1; // 1 = off
    float sat_amt_    = 0.0f;
    float dist_amt_   = 0.0f;
    float bp_f_       = 0.0f;
    float bp_damp_    = 1.0f;
    float bp_mix_     = 0.0f;
};

} // namespace multiosc
