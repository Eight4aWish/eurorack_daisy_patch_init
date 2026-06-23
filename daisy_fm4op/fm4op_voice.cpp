// fm4op multiosc engine — DSP ported verbatim from fm4op.cpp (MIT).
//
// Three algorithms (short press cycles): Parallel / Serial / Feedback.
// Modulation is scaled by the carrier increment (original behaviour), which
// keeps the timbre musical and limits aliasing. MOD labels are per-algorithm.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "fm4op_voice.h"
#include <cmath>

using namespace daisysp;

namespace multiosc {

static constexpr float kTwoPi = 2.0f * static_cast<float>(M_PI);

static inline float WrapPhase(float p)
{
    if(p >= kTwoPi)
        p -= kTwoPi;
    if(p < 0.0f)
        p += kTwoPi;
    return p;
}

void FmFourOp::RecomputeIncrements(float base_freq)
{
    for(int i = 0; i < 4; i++)
        ops_[i].incr = kTwoPi * (base_freq * ops_[i].ratio) / sample_rate_;
}

void FmFourOp::Init(DaisyPatchSM& hw, float sample_rate)
{
    (void)hw;
    sample_rate_ = sample_rate;

    const float ratios[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    for(int i = 0; i < 4; i++)
        ops_[i] = {0.0f, ratios[i], 0.0f, 0.0f, 0.0f};

    env_.Init(sample_rate_);
    env_.SetTime(ADSR_SEG_ATTACK, 0.01f);
    env_.SetTime(ADSR_SEG_DECAY, 0.15f);
    env_.SetTime(ADSR_SEG_RELEASE, 0.4f);
    env_.SetSustainLevel(0.7f);

    algo_ = 0;

    st_a_.Set(a_norm_);
    st_r_.Set(r_norm_);
    st_v_.Set(v_norm_);
}

const char* FmFourOp::ModLabel(int idx, bool edit) const
{
    static const char* kEdit[3] = {"Vol", "Atk", "Rel"};
    static const char* kPlay[3][3] = {
        {"Low", "Mid", "Hi"},     // Parallel: depth of 2x/3x/4x modulators
        {"Rat1", "Rat2", "Rat3"}, // Serial:   ratios of the three operators
        {"Fbk", "Mid", "Hi"},     // Feedback: feedback + 2x/3x modulator depth
    };
    if(idx < 0 || idx > 2)
        return "";
    return edit ? kEdit[idx] : kPlay[algo_][idx];
}

const char* FmFourOp::Selection() const
{
    // Short tag for the "NAME:SEL" title line.
    static const char* kTag[3] = {"PARA", "SERI", "FDBK"};
    return kTag[algo_];
}

void FmFourOp::Process(DaisyPatchSM&             hw,
                       const EngineContext&      ctx,
                       AudioHandle::InputBuffer  in,
                       AudioHandle::OutputBuffer out,
                       size_t                    size)
{
    (void)in;

    const float k1       = hw.GetAdcValue(KNOB_MOD1);
    const float k2       = hw.GetAdcValue(KNOB_MOD2);
    const float k3       = hw.GetAdcValue(KNOB_MOD3);
    const float k_tune   = hw.GetAdcValue(KNOB_TUNE);
    const float cv_pitch = hw.GetAdcValue(CV_VOCT);

    if(ctx.short_press)
        algo_ = (algo_ + 1) % 3;

    if(ctx.page_changed && ctx.edit_page)
    {
        st_a_.Reset(a_norm_);
        st_r_.Reset(r_norm_);
        st_v_.Reset(v_norm_);
    }

    // Read the gate once per block: level OR latched edge, so sub-block
    // triggers still register. Drive the ADSR through Process(gate) alone — a
    // hard Retrigger() would zero the level and click on overlapping notes.
    // No "play before first gate" drone: the voice is silent until gated.
    const bool gate = hw.gate_in_1.State() || hw.gate_in_1.Trig();

    // Pitch: knob = 0..6 octaves, CV = ±5 V (1 V/oct). Clamp to the audible
    // range so extreme settings can't produce sub-audio DC rumble.
    const float exponent = (k_tune * 6.0f) + ((cv_pitch * 10.0f) - 5.0f);
    float       base_freq = 50.0f * powf(2.0f, exponent);
    if(base_freq < 20.0f)
        base_freq = 20.0f;
    if(base_freq > 8000.0f)
        base_freq = 8000.0f;

    if(ctx.edit_page)
    {
        // Edit page (FM controls frozen). Grid: MOD1=Vol, MOD2=Atk, MOD3=Rel
        // so Atk/Rel sit together on the bottom row.
        v_norm_ = st_v_.Process(k1);
        a_norm_ = st_a_.Process(k2);
        r_norm_ = st_r_.Process(k3);
        env_.SetTime(ADSR_SEG_ATTACK, 0.001f + a_norm_ * 0.5f);
        env_.SetTime(ADSR_SEG_RELEASE, 0.02f + r_norm_ * 1.2f);
        master_gain_ = 0.2f + v_norm_ * 0.8f;
    }
    else if(algo_ == 0)
    {
        // Parallel: three modulators (2x/3x/4x) onto the carrier.
        ops_[1].ratio     = 2.0f;
        ops_[2].ratio     = 3.0f;
        ops_[3].ratio     = 4.0f;
        ops_[1].mod_index = k1 * 8.0f;
        ops_[2].mod_index = k2 * 8.0f;
        ops_[3].mod_index = k3 * 8.0f;
    }
    else if(algo_ == 1)
    {
        // Serial: chain op3->op2->op1->carrier; knobs select ratios.
        static const float harmonic[]
            = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f};
        auto pick = [](float k) {
            int idx = (int)(k * 8.999f);
            if(idx < 0)
                idx = 0;
            if(idx > 8)
                idx = 8;
            return harmonic[idx];
        };
        ops_[1].ratio     = pick(k1);
        ops_[2].ratio     = pick(k2);
        ops_[3].ratio     = pick(k3);
        ops_[1].mod_index = 4.0f;
        ops_[2].mod_index = 3.0f;
        ops_[3].mod_index = 2.0f;
    }
    else
    {
        // Feedback: op1 self-feedback (k1), plus 2x/3x modulators.
        ops_[1].ratio     = 2.0f;
        ops_[2].ratio     = 2.5f;
        ops_[3].ratio     = 3.0f;
        ops_[1].mod_index = 0.0f;
        ops_[2].mod_index = k2 * 10.0f;
        ops_[3].mod_index = k3 * 10.0f;
        ops_[1].last_out  = k1 * 6.0f; // feedback depth
    }

    RecomputeIncrements(base_freq);

    for(size_t i = 0; i < size; i++)
    {
        const float env_amp = env_.Process(gate);

        float mod = 0.0f;
        if(algo_ == 0)
        {
            float m1 = sinf(ops_[1].phase);
            ops_[1].phase = WrapPhase(ops_[1].phase + ops_[1].incr);
            float m2 = sinf(ops_[2].phase);
            ops_[2].phase = WrapPhase(ops_[2].phase + ops_[2].incr);
            float m3 = sinf(ops_[3].phase);
            ops_[3].phase = WrapPhase(ops_[3].phase + ops_[3].incr);
            mod = (m1 * ops_[1].mod_index + m2 * ops_[2].mod_index
                   + m3 * ops_[3].mod_index)
                  * ops_[0].incr;
        }
        else if(algo_ == 1)
        {
            float m3 = sinf(ops_[3].phase);
            ops_[3].phase = WrapPhase(ops_[3].phase + ops_[3].incr);
            float m2_mod = m3 * ops_[3].mod_index * ops_[2].incr;
            float m2     = sinf(ops_[2].phase + m2_mod);
            ops_[2].phase = WrapPhase(ops_[2].phase + ops_[2].incr + m2_mod);
            float m1_mod = m2 * ops_[2].mod_index * ops_[1].incr;
            float m1     = sinf(ops_[1].phase + m1_mod);
            ops_[1].phase = WrapPhase(ops_[1].phase + ops_[1].incr + m1_mod);
            mod = (m1 * ops_[1].mod_index) * ops_[0].incr;
        }
        else
        {
            float fb_in = sinf(ops_[1].phase);
            float m1
                = sinf(ops_[1].phase + fb_in * ops_[1].last_out * ops_[1].incr);
            ops_[1].phase = WrapPhase(ops_[1].phase + ops_[1].incr
                                      + fb_in * ops_[1].last_out * ops_[1].incr);
            float m2 = sinf(ops_[2].phase);
            ops_[2].phase = WrapPhase(ops_[2].phase + ops_[2].incr);
            float m3 = sinf(ops_[3].phase);
            ops_[3].phase = WrapPhase(ops_[3].phase + ops_[3].incr);
            mod = (m1 * ops_[1].last_out + m2 * ops_[2].mod_index
                   + m3 * ops_[3].mod_index)
                  * ops_[0].incr;
        }

        const float sample = sinf(ops_[0].phase + mod) * env_amp * master_gain_;
        ops_[0].phase      = WrapPhase(ops_[0].phase + ops_[0].incr + mod);
        out[0][i]          = sample;
        out[1][i]          = sample;
    }
}

} // namespace multiosc
