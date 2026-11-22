// fm4op multiosc engine: eight algorithms, and Ratio, Index and Feedback the same in each. See
// fm4op_voice.h.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "fm4op_voice.h"
#include <cmath>
#include "daisysp.h"

using namespace daisysp;

namespace multiosc {

static constexpr float kTwoPi    = 6.283185307f;
static constexpr float kMaxIndex = 8.0f; // as the original's Parallel depths
static constexpr float kMaxFb    = 3.0f; // operator 4 swinging its own frequency to 3x: noisy at the top
static constexpr float kSmooth   = 0.002f; // Index and Feedback, about 10 ms at 48 kHz

static inline float Wrap(float p)
{
    return p - kTwoPi * floorf(p * (1.0f / kTwoPi));
}

// For each operator, which operators modulate it (bit k = operator k + 1), and which operators are carriers.
struct Algorithm {
    uint8_t mods[4];
    uint8_t carriers;
};
static constexpr uint8_t OP1 = 1, OP2 = 2, OP3 = 4, OP4 = 8;
static const Algorithm kAlgo[FmFourOp::kAlgorithms] = {
    {{OP2, OP3, OP4, 0}, OP1},                   // 1  4 > 3 > 2 > 1          one deep stack
    {{OP2, OP3 | OP4, 0, 0}, OP1},               // 2  (3 + 4) > 2 > 1        two modulators feeding a stack
    {{OP2 | OP3, 0, OP4, 0}, OP1},               // 3  (4 > 3) + 2 > 1        a pair and a single into one
    {{OP2 | OP3 | OP4, 0, 0, 0}, OP1},           // 4  (2 + 3 + 4) > 1        three side by side
    {{OP2, 0, OP4, 0}, OP1 | OP3},               // 5  4 > 3, 2 > 1           two pairs
    {{OP4, OP4, OP4, 0}, OP1 | OP2 | OP3},       // 6  4 > 1, 2, 3            one modulator, three carriers
    {{0, 0, OP4, 0}, OP1 | OP2 | OP3},           // 7  4 > 3, plus 2 and 1    a pair and two sines
    {{0, 0, 0, 0}, OP1 | OP2 | OP3 | OP4},       // 8  1 + 2 + 3 + 4          four sines, an organ
};
static const char* const kAlgoName[FmFourOp::kAlgorithms]
    = {"Alg1", "Alg2", "Alg3", "Alg4", "Alg5", "Alg6", "Alg7", "Alg8"};

// Operators 2, 3 and 4 against operator 1 at the note: harmonic sets, then inharmonic (agreed with David,
// 2026-11-22, as a starting table to tune by ear). In algorithm 8 they act like drawbars.
static const float kRatio[FmFourOp::kRatioSets][3] = {
    {1.f, 1.f, 1.f},
    {1.f, 1.f, 2.f},
    {2.f, 2.f, 2.f},
    {2.f, 3.f, 4.f},         // the original's Parallel
    {0.5f, 1.f, 2.f},
    {3.f, 5.f, 7.f},         // odd harmonics: hollow, as drawbars
    {2.f, 4.f, 8.f},
    {1.f, 2.f, 3.f},
    {1.41421f, 1.f, 1.f},    // root 2: metallic
    {3.5f, 1.f, 1.f},
    {2.76f, 5.40f, 8.93f},   // a struck bar's overtones: marimba, glockenspiel
};

void FmFourOp::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    for(int i = 0; i < 4; i++)
        phase_[i] = out_[i] = 0.f;
    fb_[0] = fb_[1] = 0.f;
    index_ = feedback_ = 0.f;
}

const char* FmFourOp::Choice() const
{
    return kAlgoName[algo_.index()];
}

const char* FmFourOp::PotLabel(int pot) const
{
    switch(pot)
    {
        case 1: return "Rat";
        case 2: return "Idx";
        case 3: return "Fbk";
        default: return "";
    }
}

void FmFourOp::Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size)
{
    const Algorithm& alg = kAlgo[algo_.Process(c.pot[0])];
    const float*     r   = kRatio[ratio_.Process(c.pot[1])];
    const float index_target = kMaxIndex * c.pot[2] * c.pot[2];
    const float fb_target    = kMaxFb * c.pot[3] * c.pot[3];

    float f = mtof(c.note);
    if(f < 20.f)
        f = 20.f;
    if(f > 8000.f)
        f = 8000.f;
    const float base     = kTwoPi * f / sample_rate_;
    const float incr[4]  = {base, base * r[0], base * r[1], base * r[2]};

    int carriers = 0;
    for(int o = 0; o < 4; o++)
        carriers += (alg.carriers >> o) & 1;
    const float norm = 1.0f / static_cast<float>(carriers);

    for(size_t i = 0; i < size; i++)
    {
        index_ += kSmooth * (index_target - index_);
        feedback_ += kSmooth * (fb_target - feedback_);

        // Operators 4 down to 1: every modulator sits above its target, so its sample is already made.
        for(int o = 3; o >= 0; o--)
        {
            float m = 0.f;
            for(int k = o + 1; k < 4; k++)
                if(alg.mods[o] & (1 << k))
                    m += out_[k];
            float pm = incr[o] * m * index_;
            if(o == 3)
                pm += incr[3] * feedback_ * 0.5f * (fb_[0] + fb_[1]);
            out_[o]   = sinf(phase_[o] + pm);
            phase_[o] = Wrap(phase_[o] + incr[o] + pm);
        }
        fb_[1] = fb_[0];
        fb_[0] = out_[3];

        float s = 0.f;
        for(int o = 0; o < 4; o++)
            if(alg.carriers & (1 << o))
                s += out_[o];
        s *= norm;
        out[0][i] = s;
        out[1][i] = s;
    }
}

} // namespace multiosc
