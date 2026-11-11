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
            base_[i] = len_[i] = (float)s.len[i];
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

    // Once a block, before Set(): the size of the space, as a scale on every line's length
    // (TANK's size; 1 is the shape as given). A moving size bends the tail, as a moving room would.
    void SetScale(float scale)
    {
        for(int i = 0; i < 8; i++)
            len_[i] = base_[i] * scale;
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
    float     base_[8], len_[8], g_[8];
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

// TANK: the big space. The same network on the longer, slower shape; knob 2 the size (the
// lines from 60% to 100% of the shape, the decay from 2 to 25 s with it), knob 3 the damping
// (more is darker), knob 4 the modulation depth (0-40 samples), which at the top makes the
// tail chorus and swim.
class Tank
{
  public:
    void Init(float sr, size_t block)
    {
        fdn_.Init(kTankShape, sr, block);
        Clear();
    }
    void Clear() { fdn_.Clear(); }
    void Block(const float p[3])
    {
        const float rt60 = map_exp(p[0], 2.f, 25.f);
        fdn_.SetScale(map_lin(p[0], 0.6f, 1.f));
        fdn_.Set(rt60 * 1.13f, map_exp(1.f - p[1], 800.f, 14000.f), map_lin(p[2], 0.f, 40.f));
        fdn_.StartBlock();
        gain_ = kTrim * powf(rt60 / 7.f, -0.27f);
    }
    void Process(float inL, float inR, float frac, float& outL, float& outR)
    {
        fdn_.Process(inL, inR, frac, outL, outR);
        outL *= gain_;
        outR *= gain_;
    }
    static constexpr float kTrim = 1.0f;  // set on the Mac: the dry's level at the knobs' middle

  private:
    float      gain_;
    Fdn<8192>  fdn_;
};

// SHIMMER: the hall with an octave-up shifter in its feedback. Part of the tail goes up an
// octave and back in, so each pass climbs again: the classic shimmer, which the old one only
// imitated by layering one shifted copy on top. Knob 2 the decay (1-20 s), knob 3 the tone,
// knob 4 how much climbs (0 is the plain hall). The shifted return is band-limited, 250 Hz to
// 7 kHz, and soft-clipped, so octave after octave cannot run away or turn to fizz.
class Shimmer
{
  public:
    void Init(float sr, size_t block)
    {
        sr_ = sr;
        fdn_.Init(kHallShape, sr, block);
        hp_.SetHz(250.f, sr);
        lp_.SetHz(7000.f, sr);
        Clear();
    }
    void Clear()
    {
        fdn_.Clear();
        shift_.Clear();
        hp_.Clear();
        lp_.Clear();
        ret_ = 0.f;
    }
    void Block(const float p[3])
    {
        const float rt60 = map_exp(p[0], 1.f, 20.f);
        fdn_.Set(rt60 * 1.13f, map_exp(p[1], 1500.f, 16000.f), 8.f);
        fdn_.StartBlock();
        amount_ = map_lin(p[2], 0.f, 0.32f);
        // The climbing octaves add energy too: up to 4.4 dB taken back at full shimmer.
        gain_   = kTrim * powf(rt60 / 4.5f, -0.27f) * map_lin(p[2], 1.f, 0.6f);
    }
    void Process(float inL, float inR, float frac, float& outL, float& outR)
    {
        fdn_.Process(inL, inR, frac, outL, outR, ret_);
        const float up = shift_.Process(0.5f * (outL + outR));
        ret_ = amount_ * soft_clip(lp_.Process(hp_.Process(up)));
        outL *= gain_;
        outR *= gain_;
    }
    static constexpr float kTrim = 1.0f;

  private:
    float     sr_, amount_, gain_, ret_;
    Fdn<4096> fdn_;
    OctaveUp  shift_;
    OnePoleHP hp_;
    OnePoleLP lp_;
};

// PLATE: Dattorro's plate ("Effect Design, Part 1", JAES 45/9, 1997), the standard published
// plate, its lengths scaled from his 29,761 Hz to 48 kHz. A band-limited input through four
// diffusers into a figure-of-eight tank, two halves each a modulated all-pass, a delay, a
// damper and a second all-pass, each half feeding the other; the output is his fourteen taps.
// Knob 2 the pre-delay (0-100 ms), knob 3 the tone (the tank's damping: up is brighter), knob 4
// the decay (his decay gain, 0.30-0.95).
class Plate
{
  public:
    void Init(float sr, size_t block)
    {
        sr_ = sr;
        k_  = sr / 29761.f;
        in_[0].Init(S(142), 0.75f);
        in_[1].Init(S(107), 0.75f);
        in_[2].Init(S(379), 0.625f);
        in_[3].Init(S(277), 0.625f);
        mapL_.Init((float)S(672), -0.70f);
        mapR_.Init((float)S(908), -0.70f);
        apL_.Init(S(1800), 0.50f);
        apR_.Init(S(2656), 0.50f);
        lfoL_.Init(1.0f, sr, block, 0.f);
        lfoR_.Init(1.0f, sr, block, 0.25f);
        Clear();
    }
    void Clear()
    {
        pre_.Clear();
        for(auto& a : in_)
            a.Clear();
        mapL_.Clear();
        mapR_.Clear();
        apL_.Clear();
        apR_.Clear();
        dL1_.Clear();
        dL2_.Clear();
        dR1_.Clear();
        dR2_.Clear();
        bw_ = dampL_ = dampR_ = 0.f;
    }
    void Block(const float p[3])
    {
        pre_len_ = map_lin(p[0], 0.f, 0.100f) * sr_;
        damp_    = map_lin(1.f - p[1], 0.0005f, 0.65f);
        decay_   = map_lin(p[2], 0.30f, 0.95f);
        // His decay gain gives an RT60 of about 1.15/-ln(decay) s (1.0 s at 0.30, 19 s at
        // 0.95, measured on the Mac); as the other reverbs, half the longer tail's extra
        // loudness is taken back, around 2.5 s.
        const float rt60 = 1.15f / -logf(decay_);
        gain_ = kTrim * powf(rt60 / 2.5f, -0.27f);
        lfoL_.StartBlock();
        lfoR_.StartBlock();
    }
    void Process(float inL, float inR, float frac, float& outL, float& outR)
    {
        pre_.Write(0.5f * (inL + inR));
        float x = pre_.ReadFrac(pre_len_ + 1.f);
        bw_ += 0.9995f * (x - bw_);  // his bandwidth filter
        x = in_[3].Process(in_[2].Process(in_[1].Process(in_[0].Process(bw_))));

        const float exc = 16.f * k_;  // his 16-sample excursion
        // Left half, fed by the right half's end; right half, fed by the left's.
        float l = mapL_.Process(x + decay_ * dR2_.Read(S(3163) - 1), exc * lfoL_.At(frac));
        dL1_.Write(l);
        l = dL1_.Read(S(4453) - 1);
        dampL_ += (1.f - damp_) * (l - dampL_);
        l = apL_.Process(dampL_ * decay_);
        dL2_.Write(l);

        float r = mapR_.Process(x + decay_ * dL2_.Read(S(3720) - 1), exc * lfoR_.At(frac));
        dR1_.Write(r);
        r = dR1_.Read(S(4217) - 1);
        dampR_ += (1.f - damp_) * (r - dampR_);
        r = apR_.Process(dampR_ * decay_);
        dR2_.Write(r);

        // His output taps (Table 2), scaled.
        outL = 0.6f * (dR1_.Read(S(266)) + dR1_.Read(S(2974)) - apR_.Tap(S(1913)) + dR2_.Read(S(1996))
                       - dL1_.Read(S(1990)) - apL_.Tap(S(187)) - dL2_.Read(S(1066)));
        outR = 0.6f * (dL1_.Read(S(353)) + dL1_.Read(S(3627)) - apL_.Tap(S(1228)) + dL2_.Read(S(2673))
                       - dR1_.Read(S(2111)) - apR_.Tap(S(335)) - dR2_.Read(S(121)));
        outL *= gain_;
        outR *= gain_;
    }
    static constexpr float kTrim = 0.67f;  // -3.5 dB: his 0.6 taps run hot at 48 kHz

  private:
    uint32_t S(uint32_t n) const { return (uint32_t)((float)n * k_ + 0.5f); }
    float sr_, k_, pre_len_, damp_, decay_, bw_, dampL_, dampR_, gain_;
    Delay<8192>      pre_;
    Allpass<1024>    in_[4];
    ModAllpass<2048> mapL_, mapR_;
    Allpass<8192>    apL_, apR_;
    Delay<8192>      dL1_, dL2_, dR1_, dR2_;
    BlockLfo         lfoL_, lfoR_;
};

}  // namespace fx
