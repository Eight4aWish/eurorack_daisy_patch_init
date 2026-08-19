// Lo-fi Tone macro — see lofi_tone.h for provenance and what was adapted.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE), adapting MIT code
// (c) 2026 Steven Collins.

#include "lofi_tone.h"

#include <cmath>

namespace multiosc {

namespace {

// Knob geometry. Unlike Ogham's, these are the nominal ends of a clean 0..1 ADC
// reading rather than measured pot extremes — see the header.
constexpr float kCenter   = 0.5f;
constexpr float kDeadzone = 0.02f; // symmetric clean band around noon
constexpr float kPotMin   = 0.0f;
constexpr float kPotMax   = 1.0f;

// CW staging, as fractions of the CW throw (0 = centre, 1 = full CW).
constexpr float kSrrMax     = 154.0f; // longest sample-and-hold, exponential ramp
constexpr float kCwSatDrive = 3.0f;   // soft-saturation drive at full CW
constexpr float kDistDrive  = 6.0f;   // overdrive gain at full CW

// CCW staging, as fractions of the CCW throw.
constexpr float kFilterEnd  = 0.6f;   // low-pass fully shut by 0.6 of the throw
constexpr float kFoldStart  = 0.40f;  // drive + wavefold starts (~10 o'clock)
constexpr float kLpCoeffMin = 0.025f; // 2-pole low-pass floor
constexpr float kDriveMax   = 24.0f;  // pre-fold drive at full CCW
constexpr float kSatDrive   = 3.0f;   // post-fold saturation drive

// Resonant band-pass swept over the CW throw (Ogham's baked g_lofiConfig).
constexpr float kBpCutoffStartHz = 740.0f;
constexpr float kBpCutoffEndHz   = 4100.0f;
constexpr float kBpQStart        = 2.9f;
constexpr float kBpQEnd          = 6.9f;
constexpr float kBpMixMax        = 0.78f;
constexpr float kSweepCurve      = 1.3f; // >1 back-loads the sweep into the upper throw

// Output soft knee: transparent below this, asymptotic to 1.0 above.
constexpr float kLimitKnee = 0.7f;

} // namespace

void LofiTone::Init(float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    SetMacro(kCenter);
}

float LofiTone::Wavefold(float x)
{
    // Triangle wavefolder, period 4: folds any magnitude back into [-1, 1] and
    // adds harmonics. Passes |x| <= 1 through unchanged.
    float q = fmodf(x + 1.0f, 4.0f);
    if(q < 0.0f)
        q += 4.0f;
    const float tri = (q <= 2.0f) ? q : 4.0f - q;
    return tri - 1.0f;
}

void LofiTone::SetMacro(float pot)
{
    if(pot > kCenter + kDeadzone)
    {
        clean_ = false;
        float cw
            = (pot - (kCenter + kDeadzone)) / (kPotMax - (kCenter + kDeadzone));
        if(cw > 1.0f)
            cw = 1.0f;

        // Sample-rate reduction: geometric ramp 1 -> kSrrMax, so the onset just
        // past centre is gentle and accelerates toward full CW.
        srr_factor_ = (int)(powf(kSrrMax, cw) + 0.5f);
        if(srr_factor_ < 1)
            srr_factor_ = 1;

        sat_amt_  = cw;
        dist_amt_ = cw;

        // Band-pass: cutoff and Q both rise across the throw, wet mix from zero.
        const float shaped = powf(cw, kSweepCurve);
        const float fc = kBpCutoffStartHz + shaped * (kBpCutoffEndHz - kBpCutoffStartHz);
        const float q  = kBpQStart + cw * (kBpQEnd - kBpQStart);
        float f = 2.0f * sinf(3.14159265f * fc / sample_rate_);
        if(f > 0.99f)
            f = 0.99f;
        if(f < 0.0f)
            f = 0.0f;
        bp_f_ = f;
        float damp = (q > 0.05f) ? 1.0f / q : 1.0f / 0.05f;
        if(damp > 2.0f)
            damp = 2.0f;
        if(damp < 0.02f)
            damp = 0.02f;
        bp_damp_ = damp;
        bp_mix_  = shaped * kBpMixMax;

        lp_coeff_ = 1.0f;
        drive_gain_ = 1.0f;
        fold_mix_   = 0.0f;
    }
    else if(pot < kCenter - kDeadzone)
    {
        clean_ = false;
        float ccw
            = ((kCenter - kDeadzone) - pot) / ((kCenter - kDeadzone) - kPotMin);
        if(ccw > 1.0f)
            ccw = 1.0f;

        // Low-pass sweeps shut first, exponentially — equal octaves per degree.
        float f_amt = ccw / kFilterEnd;
        if(f_amt > 1.0f)
            f_amt = 1.0f;
        lp_coeff_ = powf(kLpCoeffMin, f_amt);

        // Drive + wavefold blend in over the back of the throw.
        float d_amt = (ccw - kFoldStart) / (1.0f - kFoldStart);
        if(d_amt < 0.0f)
            d_amt = 0.0f;
        if(d_amt > 1.0f)
            d_amt = 1.0f;
        drive_gain_ = 1.0f + d_amt * (kDriveMax - 1.0f);
        fold_mix_   = d_amt;

        srr_factor_ = 1;
        sat_amt_    = 0.0f;
        dist_amt_   = 0.0f;
        bp_mix_     = 0.0f;
    }
    else
    {
        clean_      = true;
        srr_factor_ = 1;
        sat_amt_    = 0.0f;
        dist_amt_   = 0.0f;
        bp_mix_     = 0.0f;
        lp_coeff_   = 1.0f;
        drive_gain_ = 1.0f;
        fold_mix_   = 0.0f;
    }
}

float LofiTone::Process(float v, State& s) const
{
    // True bypass in the deadzone. The 2-pole low-pass below is identity at
    // coefficient 1.0 in exact arithmetic but not in floating point — `a + (v -
    // a)` does not round back to `v` — so "clean" has to mean *skipped*, not
    // *unity*. Callers may also test IsClean() to skip the call entirely.
    if(clean_)
        return v;

    // Sample-rate reduction (CW).
    if(srr_factor_ > 1)
    {
        if(s.srr_count <= 0)
        {
            s.srr_held  = v;
            s.srr_count = srr_factor_;
        }
        s.srr_count--;
        v = s.srr_held;
    }
    // Soft saturation riding with the reduction (CW).
    if(sat_amt_ > 0.0f)
    {
        const float sat = tanhf(v * (1.0f + sat_amt_ * (kCwSatDrive - 1.0f)));
        v += sat_amt_ * (sat - v);
    }
    // Overdrive into a hard clip (CW).
    if(dist_amt_ > 0.0f)
    {
        const float drive = 1.0f + dist_amt_ * (kDistDrive - 1.0f);
        float       d     = v * drive;
        if(d > 1.0f)
            d = 1.0f;
        if(d < -1.0f)
            d = -1.0f;
        v += dist_amt_ * (d - v);
    }
    // 2-pole low-pass (CCW; coefficient 1 leaves the signal alone).
    s.lp1 += lp_coeff_ * (v - s.lp1);
    s.lp2 += lp_coeff_ * (s.lp1 - s.lp2);
    v = s.lp2;
    // Drive -> wavefold -> saturation, blended in (CCW).
    if(fold_mix_ > 0.0f)
    {
        const float folded    = Wavefold(v * drive_gain_);
        const float processed = tanhf(folded * (1.0f + fold_mix_ * (kSatDrive - 1.0f)));
        v += fold_mix_ * (processed - v);
    }
    // Resonant band-pass, blended dry -> band-pass (CW).
    if(bp_mix_ > 0.0f)
    {
        s.svf_lp += bp_f_ * s.svf_bp;
        const float hp = v - s.svf_lp - bp_damp_ * s.svf_bp;
        s.svf_bp += bp_f_ * hp;
        // High Q can ring; clamp so a sweep can never run away.
        if(s.svf_bp > 4.0f)
            s.svf_bp = 4.0f;
        else if(s.svf_bp < -4.0f)
            s.svf_bp = -4.0f;
        if(s.svf_lp > 4.0f)
            s.svf_lp = 4.0f;
        else if(s.svf_lp < -4.0f)
            s.svf_lp = -4.0f;
        v += bp_mix_ * (s.svf_bp - v);
    }

    // Keep the stage inside full scale. Measured across the bank against every
    // knob position, the worst case reaches 1.88 from the engine's +/-1 — the
    // overdrive and the resonant band-pass both add level. Ogham can afford that
    // because it halves its output afterwards to suit a hot analog stage; here it
    // would simply clip at the codec. A soft knee leaves everything below 0.7
    // untouched and asymptotes to 1.0, so the character survives and the ceiling
    // holds.
    const float mag = fabsf(v);
    if(mag > kLimitKnee)
    {
        const float over = (mag - kLimitKnee) / (1.0f - kLimitKnee);
        const float lim  = kLimitKnee + (1.0f - kLimitKnee) * tanhf(over);
        v = (v < 0.0f) ? -lim : lim;
    }
    return v;
}

} // namespace multiosc
