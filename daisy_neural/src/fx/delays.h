// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The FX bank's four delays (FX.md): PING, TAPE, MULTITAP and ECHOVERB, written for Mirth.
// Every delay time glides (Glide, about 150 ms), so a turned knob or a new tap tempo bends
// the repeats as tape would, rather than jumping and clicking. With a clock on gate in 1,
// knob 2 stops being a time and picks a division of the clock instead: 1/16, 1/8, dotted
// 1/8 or 1/4, the clock taken as a quarter note.

#pragma once
#include "dsp.h"
#include "reverb.h"

namespace fx {

struct Tap
{
    bool  active;   // clocks arriving on gate in 1
    float samples;  // the interval between them
};

// Knob 2 as a delay time: a time in seconds from lo to hi on a log scale, or, with a clock,
// one of four divisions of it, in samples.
inline float DelayTime(float k2, const Tap& tap, float lo, float hi, float sr)
{
    if(tap.active)
    {
        static constexpr float kDiv[4] = {0.25f, 0.5f, 0.75f, 1.f};
        const int              i       = (int)clampf(k2 * 4.f, 0.f, 3.f);
        return tap.samples * kDiv[i];
    }
    return map_exp(k2, lo, hi) * sr;
}

constexpr size_t kLong = 131072;  // 2.7 s at 48 kHz

// PING: a true ping-pong. The input (both sides summed) goes into the left line only; the left
// line's output feeds the right, the right's feeds back to the left, so the repeats alternate
// L, R, L... Knob 2 the time (20 ms-1.4 s), knob 3 the feedback (to 0.95), knob 4 the damping:
// a low-pass in the loop (18 kHz down to 1.5 kHz), so each pass is darker.
class Ping
{
  public:
    void Init(float sr)
    {
        sr_ = sr;
        t_.Init(0.15f, sr);
        hp_.SetHz(60.f, sr);
        Clear();
    }
    void Clear()
    {
        l_.Clear();
        r_.Clear();
        lpL_.Clear();
        lpR_.Clear();
        hp_.Clear();
        t_.primed = false;
    }
    void Block(const float p[3], const Tap& tap)
    {
        target_ = DelayTime(p[0], tap, 0.020f, 1.4f, sr_);
        fb_     = map_lin(p[1], 0.f, 0.95f);
        lpL_.SetHz(map_exp(1.f - p[2], 1500.f, 18000.f), sr_);
        lpR_.a = lpL_.a;
    }
    void Process(float inL, float inR, float& outL, float& outR)
    {
        const float t = t_.Process(target_);
        const float a = l_.ReadFrac(t), b = r_.ReadFrac(t);
        l_.Write(hp_.Process(0.5f * (inL + inR)) + fb_ * lpR_.Process(b));
        r_.Write(fb_ * lpL_.Process(a));
        outL = a * kTrim;
        outR = b * kTrim;
    }
    static constexpr float kTrim = 1.33f;  // +2.5 dB: one line at a time is heard each side

  private:
    float           sr_, target_, fb_;
    Glide           t_;
    Delay<kLong>    l_, r_;
    OnePoleLP       lpL_, lpR_;
    OnePoleHP       hp_;
};

// TAPE: a tape echo, worn by knob 4. Wear adds wow (0.5 Hz) and flutter (6.5 Hz) to the
// read, a slow random drift, saturation in the loop, and a narrowing band (the low end thins
// from 60 to 250 Hz, the top falls from 12 to 2.5 kHz), so repeats soften and smear as tape
// does. Knob 2 the time (40 ms-1.2 s), knob 3 the feedback (to 1.0: past the middle it can
// build, and the saturation holds it). Left and right run their own heads, the wow a quarter
// cycle apart, so the echo is wide.
class Tape
{
  public:
    void Init(float sr)
    {
        sr_ = sr;
        t_.Init(0.25f, sr);
        noise_.Seed(12345u);
        Clear();
    }
    void Clear()
    {
        for(int c = 0; c < 2; c++)
        {
            d_[c].Clear();
            lp_[c].Clear();
            hp_[c].Clear();
        }
        t_.primed = false;
        wow_ = flut_ = drift_ = 0.f;
    }
    void Block(const float p[3], const Tap& tap)
    {
        target_ = DelayTime(p[0], tap, 0.040f, 1.2f, sr_);
        fb_     = map_lin(p[1], 0.f, 1.0f);
        wear_   = clampf(p[2], 0.f, 1.f);
        for(int c = 0; c < 2; c++)
        {
            lp_[c].SetHz(map_exp(1.f - wear_, 2500.f, 12000.f), sr_);
            hp_[c].SetHz(map_exp(wear_, 60.f, 250.f), sr_);
        }
        drive_ = map_lin(wear_, 1.f, 2.5f);
        const float n = (float)48 / sr_;  // one block
        wow_ += 0.5f * n;
        if(wow_ >= 1.f)
            wow_ -= 1.f;
        flut_ += 6.5f * n;
        if(flut_ >= 1.f)
            flut_ -= 1.f;
        drift_ += 0.02f * (noise_.Next() - drift_);  // a slow random walk
    }
    void Process(float inL, float inR, float& outL, float& outR)
    {
        const float t   = t_.Process(target_);
        const float in[2] = {inL, inR};
        float       out[2];
        for(int c = 0; c < 2; c++)
        {
            const float ph  = c ? 0.25f : 0.f;
            const float mod = wear_ * (0.006f * sinf(kTwoPi * (wow_ + ph)) + 0.0015f * sinf(kTwoPi * (flut_ + ph))
                                       + 0.004f * drift_);
            const float y = d_[c].ReadFrac(t * (1.f + mod));
            const float loop = hp_[c].Process(lp_[c].Process(y));
            d_[c].Write(soft_clip(drive_ * (in[c] + fb_ * loop)) / drive_);
            out[c] = y;
        }
        outL = out[0] * kTrim;
        outR = out[1] * kTrim;
    }
    static constexpr float kTrim = 1.0f;

  private:
    float        sr_, target_, fb_, wear_, drive_, wow_, flut_, drift_;
    Glide        t_;
    Noise        noise_;
    Delay<kLong> d_[2];
    OnePoleLP    lp_[2];
    OnePoleHP    hp_[2];
};

// MULTITAP: four taps in a rhythm, spread across the stereo field. Knob 2 the beat (80 ms-
// 650 ms, or a division of the clock), knob 3 the pattern, knob 4 the feedback (from the last
// tap, through a gentle low-pass, to 0.8). A pattern change crossfades over 30 ms, so the
// taps never jump.
class MultiTap
{
  public:
    struct TapDef
    {
        float beats, gain, pan;  // pan -1 left .. +1 right
    };
    static constexpr int kPatterns = 6;
    static constexpr TapDef kPat[kPatterns][4] = {
        {{1.f, 0.9f, -0.6f}, {2.f, 0.75f, 0.6f}, {3.f, 0.6f, -0.3f}, {4.f, 0.45f, 0.3f}},          // straight
        {{0.75f, 0.9f, -0.7f}, {1.5f, 0.75f, 0.7f}, {2.25f, 0.6f, -0.4f}, {3.f, 0.45f, 0.4f}},     // dotted
        {{0.667f, 0.9f, -0.5f}, {1.333f, 0.8f, 0.5f}, {2.f, 0.65f, 0.f}, {2.667f, 0.5f, -0.8f}},  // triplet
        {{1.f, 0.9f, -0.5f}, {1.667f, 0.7f, 0.5f}, {3.f, 0.6f, -0.5f}, {3.667f, 0.45f, 0.5f}},     // swing
        {{1.f, 0.6f, 0.f}, {1.75f, 0.7f, -0.6f}, {2.25f, 0.8f, 0.6f}, {2.5f, 0.9f, 0.f}},          // gathering
        {{0.5f, 0.8f, -1.f}, {1.f, 0.8f, 1.f}, {1.25f, 0.6f, -1.f}, {2.f, 0.6f, 1.f}},             // scatter
    };

    void Init(float sr)
    {
        sr_ = sr;
        t_.Init(0.15f, sr);
        lp_.SetHz(5000.f, sr);
        Clear();
        cur_ = prev_ = 0;
        xf_          = 1.f;
    }
    void Clear()
    {
        d_.Clear();
        lp_.Clear();
        last_ = 0.f;
        t_.primed = false;
    }
    void Block(const float p[3], const Tap& tap)
    {
        target_ = DelayTime(p[0], tap, 0.080f, 0.650f, sr_);
        const int want = (int)clampf(p[1] * kPatterns, 0.f, kPatterns - 1);
        if(want != cur_ && xf_ >= 1.f)
        {
            prev_ = cur_;
            cur_  = want;
            xf_   = 0.f;
        }
        fb_ = map_lin(p[2], 0.f, 0.8f);
    }
    void Process(float inL, float inR, float& outL, float& outR)
    {
        const float beat = t_.Process(target_);
        d_.Write(0.5f * (inL + inR) + fb_ * lp_.Process(last_));
        if(xf_ < 1.f)
            xf_ += 1.f / (0.030f * sr_);
        const float xf = clampf(xf_, 0.f, 1.f);
        float       l = 0.f, r = 0.f, last = 0.f;
        Sum(cur_, beat, xf, l, r, last);
        if(xf < 1.f)
        {
            float dummy;
            Sum(prev_, beat, 1.f - xf, l, r, dummy);
        }
        last_ = last;
        outL  = l * kTrim;
        outR  = r * kTrim;
    }
    static constexpr float kTrim = 1.0f;

  private:
    void Sum(int pat, float beat, float g, float& l, float& r, float& last) const
    {
        for(int i = 0; i < 4; i++)
        {
            const TapDef& t = kPat[pat][i];
            const float   y = d_.ReadFrac(beat * t.beats) * t.gain * g;
            l += y * (t.pan <= 0.f ? 1.f : 1.f - t.pan);
            r += y * (t.pan >= 0.f ? 1.f : 1.f + t.pan);
            if(i == 3)
                last = y;
        }
    }
    float           sr_, target_, fb_, last_, xf_;
    int             cur_, prev_;
    Glide           t_;
    Delay<kLong>    d_;
    OnePoleLP       lp_;
};

// ECHOVERB: an echo whose repeats feed a hall. Knob 2 the time (30 ms-1.2 s), knob 3 the
// feedback (to 0.9, darkening as it goes), knob 4 how much of the repeats bloom into the
// reverb (a 3.5 s hall, its own network): at 0 a plain echo, at the top the repeats
// dissolve into the space.
class EchoVerb
{
  public:
    void Init(float sr, size_t block)
    {
        sr_ = sr;
        t_.Init(0.15f, sr);
        fdn_.Init(kHallShape, sr, block);
        fdn_.Set(3.5f * 1.13f, 6000.f, 6.f);
        Clear();
    }
    void Clear()
    {
        for(int c = 0; c < 2; c++)
        {
            d_[c].Clear();
            lp_[c].Clear();
        }
        fdn_.Clear();
        t_.primed = false;
    }
    void Block(const float p[3], const Tap& tap)
    {
        target_ = DelayTime(p[0], tap, 0.030f, 1.2f, sr_);
        fb_     = map_lin(p[1], 0.f, 0.9f);
        for(int c = 0; c < 2; c++)
            lp_[c].SetHz(map_exp(1.f - p[1], 3500.f, 12000.f), sr_);
        verb_ = clampf(p[2], 0.f, 1.f);
        fdn_.StartBlock();
    }
    void Process(float inL, float inR, float frac, float& outL, float& outR)
    {
        const float t     = t_.Process(target_);
        const float in[2] = {inL, inR};
        float       e[2];
        for(int c = 0; c < 2; c++)
        {
            e[c] = d_[c].ReadFrac(t);
            d_[c].Write(in[c] + fb_ * lp_[c].Process(e[c]));
        }
        float vl, vr;
        fdn_.Process(e[0] * verb_, e[1] * verb_, frac, vl, vr);
        outL = (e[0] * (1.f - 0.5f * verb_) + vl) * kTrim;
        outR = (e[1] * (1.f - 0.5f * verb_) + vr) * kTrim;
    }
    static constexpr float kTrim = 1.0f;

  private:
    float        sr_, target_, fb_, verb_;
    Glide        t_;
    Delay<kLong> d_[2];
    OnePoleLP    lp_[2];
    Fdn<4096>    fdn_;
};

}  // namespace fx
