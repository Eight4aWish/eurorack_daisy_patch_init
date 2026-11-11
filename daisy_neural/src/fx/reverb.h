// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The feedback delay network behind CLASSIC, TANK, SHIMMER and ECHOVERB (FX.md). It is the
// family the old CLASSIC belonged to (Costello's ReverbSc is an eight-line FDN), written
// fresh here: eight lines of mutually prime lengths, an orthonormal Hadamard mix, a
// one-pole damper and a decay gain on every line, every line slowly modulated so the tail
// never rings at fixed pitches, and two all-pass diffusers on each input to thicken the
// onset. Left feeds lines 0-3 and right 4-7; left is read from the even lines and right from
// the odd ones, so the two sides decorrelate.
//
// Decay is RT60, the seconds for the tail to fall 60 dB: each line's gain is
// 10^(-3 L / (RT60 fs)), so every line decays at the same rate whatever its length.
// Tone is the dampers' corner: the top end then dies faster than the bottom, as in a room.

#pragma once
#include "dsp.h"

namespace fx {

// The line lengths for each voicing, in samples at 48 kHz: mutually prime, spread over
// about an octave so the modes interleave.
struct FdnShape
{
    uint32_t len[8];
    float    mod_hz[8];  // each line's modulation rate
};
// CLASSIC: 30-58 ms, a medium-large hall.
inline constexpr FdnShape kHallShape = {{1433, 1601, 1867, 2053, 2251, 2399, 2617, 2797},
                                        {0.31f, 0.47f, 0.59f, 0.71f, 0.83f, 0.37f, 0.53f, 0.67f}};
// TANK: 60-118 ms, a far bigger space, modulated slower and deeper.
inline constexpr FdnShape kTankShape = {{2903, 3271, 3637, 4001, 4409, 4793, 5227, 5653},
                                        {0.11f, 0.17f, 0.23f, 0.29f, 0.13f, 0.19f, 0.27f, 0.31f}};

template <size_t N>  // line buffer length: a power of two above the longest line plus modulation
class Fdn
{
  public:
    void Init(const FdnShape& s, float sr, size_t block)
    {
        sr_ = sr;
        for(int i = 0; i < 8; i++)
        {
            len_[i] = (float)s.len[i];
            lfo_[i].Init(s.mod_hz[i], sr, block, (float)i / 8.f);
        }
        ap_[0].Init(142, 0.70f);  // the diffusers: Dattorro's input lengths, at 48 kHz
        ap_[1].Init(379, 0.70f);
        ap_[2].Init(107, 0.70f);
        ap_[3].Init(277, 0.70f);
        mod_depth_ = 0.f;
        Set(2.f, 8000.f, 0.f);
        Clear();
    }
    void Clear()
    {
        for(int i = 0; i < 8; i++)
        {
            line_[i].Clear();
            damp_[i].Clear();
        }
        for(auto& a : ap_)
            a.Clear();
    }

    // Once a block: decay as RT60 seconds, the dampers' corner in Hz, and the modulation
    // depth in samples (0 holds the lines still).
    void Set(float rt60, float damp_hz, float mod_samples)
    {
        for(int i = 0; i < 8; i++)
        {
            g_[i] = powf(10.f, -3.f * len_[i] / (rt60 * sr_));
            damp_[i].SetHz(damp_hz, sr_);
        }
        mod_depth_ = mod_samples;
    }
    void StartBlock()
    {
        for(auto& l : lfo_)
            l.StartBlock();
    }

    // One sample; frac is how far through the block it is (for the modulation ramp). In feeds
    // the network; extra is added inside the loop (SHIMMER's pitch-shifted return).
    void Process(float inL, float inR, float frac, float& outL, float& outR, float extra = 0.f)
    {
        const float dl = ap_[1].Process(ap_[0].Process(inL));
        const float dr = ap_[3].Process(ap_[2].Process(inR));

        float y[8];
        for(int i = 0; i < 8; i++)
        {
            const float d = len_[i] - 1.f + mod_depth_ * (1.f + lfo_[i].At(frac));
            y[i]          = damp_[i].Process(line_[i].ReadFrac(d)) * g_[i];
        }
        outL = 0.5f * (y[0] - y[2] + y[4] - y[6]);
        outR = 0.5f * (y[1] - y[3] + y[5] - y[7]);

        Hadamard8(y);
        const float inject[8] = {dl, -dl, dl, -dl, dr, -dr, dr, -dr};
        for(int i = 0; i < 8; i++)
            line_[i].Write(y[i] + 0.5f * inject[i] + extra);
    }

  private:
    float     sr_;
    float     len_[8], g_[8];
    float     mod_depth_;
    Delay<N>  line_[8];
    OnePoleLP damp_[8];
    BlockLfo  lfo_[8];
    Allpass<512> ap_[4];
};

// CLASSIC: the hall, with knob 2 the decay (0.4-12 s), knob 3 the tone (the dampers'
// corner, 1.5-16 kHz) and knob 4 the pre-delay (0-150 ms), which the old one lacked.
class Classic
{
  public:
    void Init(float sr, size_t block)
    {
        sr_ = sr;
        fdn_.Init(kHallShape, sr, block);
        Clear();
    }
    void Clear()
    {
        fdn_.Clear();
        preL_.Clear();
        preR_.Clear();
    }
    // The dampers take about 12% off the decay asked for (measured on the Mac, tools/
    // fx_listen.py), so it is asked for that much longer. A longer tail is louder, by about
    // 11 dB per tenfold decay; half of that is taken back, so MIX means much the same at any
    // decay while a long reverb still sounds bigger, and at the knob's middle (2.2 s) the wet
    // comes out as loud as the dry went in.
    void Block(const float p[3])
    {
        const float rt60 = map_exp(p[0], 0.4f, 12.f);
        fdn_.Set(rt60 * 1.13f, map_exp(p[1], 1500.f, 16000.f), 6.f);
        fdn_.StartBlock();
        pre_  = map_lin(p[2], 0.f, 0.150f) * sr_;
        gain_ = 1.41f * powf(rt60 / 2.2f, -0.27f);
    }
    void Process(float inL, float inR, float frac, float& outL, float& outR)
    {
        preL_.Write(inL);
        preR_.Write(inR);
        fdn_.Process(preL_.ReadFrac(pre_ + 1.f), preR_.ReadFrac(pre_ + 1.f), frac, outL, outR);
        outL *= gain_;
        outR *= gain_;
    }
    Fdn<4096>& Network() { return fdn_; }

  private:
    float      sr_, pre_, gain_;
    Fdn<4096>  fdn_;
    Delay<8192> preL_, preR_;
};

}  // namespace fx
