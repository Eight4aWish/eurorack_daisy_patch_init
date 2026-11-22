// Scanned synthesis engine — original work.
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "scanned_voice.h"
#include <cmath>

using namespace daisysp;

namespace multiosc {

float ScannedVoice::NextRand()
{
    rng_ = rng_ * 1664525u + 1013904223u;
    return (float)(rng_ >> 9) * (1.0f / 8388608.0f) - 1.0f; // -1..1
}

void ScannedVoice::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    for(int i = 0; i < kN; i++)
        pos_[i] = prev_[i] = 0.0f;
    scan_phase_ = 0.0f;
    prev_gate_  = false;
    shape_      = 0;
}

// Inject an excitation profile into the string (adds displacement -> velocity).
void ScannedVoice::Excite(float amount)
{
    switch(shape_)
    {
        case 0: // Pulse: single node
            pos_[kN / 4] += amount;
            break;
        case 1: // Bump: raised cosine over 16 nodes
            for(int j = 0; j < 16; j++)
            {
                const float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * j / 16.0f));
                pos_[(kN / 4 + j) & (kN - 1)] += amount * w;
            }
            break;
        case 2: // Two opposed bumps
            for(int j = 0; j < 8; j++)
            {
                const float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * j / 8.0f));
                pos_[(8 + j) & (kN - 1)] += amount * w;
                pos_[(40 + j) & (kN - 1)] -= amount * w;
            }
            break;
        default: // Noise burst across the whole string
            for(int i = 0; i < kN; i++)
                pos_[i] += amount * 0.5f * NextRand();
            break;
    }
}

// One sub-audio dynamics tick (finite-difference string with centering + damping).
void ScannedVoice::UpdateDynamics()
{
    float nxt[kN];
    const float decay = 1.0f - damping_;
    for(int i = 0; i < kN; i++)
    {
        const int   l   = (i - 1) & (kN - 1);
        const int   r   = (i + 1) & (kN - 1);
        const float lap = pos_[l] + pos_[r] - 2.0f * pos_[i];
        float       v   = 2.0f * pos_[i] - prev_[i] + tension_ * lap - center_ * pos_[i];
        nxt[i]          = v * decay;
    }
    for(int i = 0; i < kN; i++)
    {
        prev_[i] = pos_[i];
        float p  = nxt[i];
        if(p > 4.0f)
            p = 4.0f;
        if(p < -4.0f)
            p = -4.0f;
        pos_[i] = p;
    }
}

const char* ScannedVoice::PotLabel(int pot) const
{
    switch(pot)
    {
        case 1: return "Tens";
        case 2: return "Hamr";
        case 3: return "Cntr";
        default: return "";
    }
}

const char* ScannedVoice::Choice() const
{
    static const char* kTag[kShapes] = {"Pulse", "Bump", "Two", "Noise"};
    return kTag[shape_];
}

void ScannedVoice::Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size)
{
    shape_   = static_cast<uint8_t>(shape_pot_.Process(c.pot[0]));
    tension_ = fmap(c.pot[1], 0.02f, 0.45f);
    hammer_  = c.pot[2];
    center_  = fmap(c.pot[3], 0.0f, 0.02f);
    // Decay is damping, turned round so clockwise rings longer; exponential, as decay time goes as its
    // inverse: 0.03 (short) to 0.0005 (long), the first build's range.
    damping_ = 0.03f * powf(0.0005f / 0.03f, c.decay);

    float f = mtof(c.note);
    if(f < 20.f)
        f = 20.f;
    if(f > 5000.f)
        f = 5000.f;
    freq_ = f;

    // A drone until the first gate, fed all the time; from then on each gate strikes it.
    const bool rising = c.gate && !prev_gate_;
    prev_gate_        = c.gate;
    if(!c.gated)
        Excite(hammer_ * 0.02f); // continuous self-excitation -> drone
    if(rising)
        Excite(hammer_);

    UpdateDynamics(); // one sub-audio tick per block

    const float inc = freq_ / sample_rate_;
    for(size_t n = 0; n < size; n++)
    {
        scan_phase_ += inc;
        if(scan_phase_ >= 1.0f)
            scan_phase_ -= 1.0f;

        const float fidx = scan_phase_ * kN;
        const int   i0   = (int)fidx;
        const float fr   = fidx - i0;
        const int   i1   = (i0 + 1) & (kN - 1);
        const float s    = pos_[i0] * (1.0f - fr) + pos_[i1] * fr;

        // DC-block then soft-clip for a safe, warm output level.
        const float dc = s - dc_x1_ + 0.999f * dc_y1_;
        dc_x1_         = s;
        dc_y1_         = dc;
        const float o  = tanhf(dc * 2.5f);
        out[0][n]      = o;
        out[1][n]      = o;
    }
}

} // namespace multiosc
