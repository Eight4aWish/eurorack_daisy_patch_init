// notamp_dsp — the not-amps' processing, shared by the firmware (main.cpp) and the
// Mac harness (tools/a2_notamp.cpp), so the harness renders exactly what the module
// runs. Chosen 2026-10-02 by tools/notamp_search.py and by ear; the definitions and
// their level tables are generated into notamps.h by tools/notamp_design.py.
//
// A not-amp is a real capture with one transform and one control, the steer (0..1):
//   Sine     every neuron's activation sin(g x)/g          steer: g, log
//   Slope    every neuron's leaky-ReLU slope                steer: slope, linear
//   Freeze   one layer's output held for p samples          steer: p, log
//   Fold     a triangle fold inside one layer               steer: threshold, log
//   Morph    weights A + t(B - A), t past 1: "past" B        steer: t, linear
//   FbGain   output fed back to the input after D samples   steer: loop gain, linear
//   FbPitch  the same at a fixed gain                       steer: D (the pitch), log
//   Rate     the engine run at 1/R of the sample rate       steer: R in kRateSteps
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "nam/nam_a2_runtime.h"

namespace notamps
{
enum class Kind : uint8_t { Sine, Slope, Freeze, Fold, Morph, FbGain, FbPitch, Rate };

struct Def
{
    const char* name;     // on the OLED, at most 10 characters
    Kind        kind;
    const char* cap_a;    // the capture on the card (Morph: the one pushed away from)
    const char* cap_b;    // Morph: the capture pushed past; else nullptr
    const char* cap_ref;  // the capture whose header gain sets the level
    float       lo, hi;   // the parameter at steer 0 and steer 1
    bool        log_map;  // the steer law
    uint8_t     layer;    // Freeze, Fold: which layer
    float       fixed;    // FbGain: loop delay in samples; FbPitch: loop gain
    float       gain_db[9]; // level correction at steer 0, 1/8 ... 1
};

constexpr float kRateSteps[5] = {1.f, 2.f, 3.f, 4.f, 6.f};
constexpr int   kBlock        = nam_a2_daisy::kBlockSize;  // 48
constexpr int   kFbRing       = 2048;  // > the longest loop (1200) plus a block; a power of two
constexpr int   kMaxRate      = 6;

// The control's value for steer u, by the not-amp's law.
inline float Param(const Def& d, float u)
{
    if(d.kind == Kind::Rate)
        return kRateSteps[(int)lroundf(u * 4.f)];
    return d.log_map ? expf(logf(d.lo) + (logf(d.hi) - logf(d.lo)) * u) : d.lo + (d.hi - d.lo) * u;
}

// The level correction at steer u, linear, interpolated from the nine-point table.
inline float LevelGain(const Def& d, float u)
{
    const float x  = u * 8.f;
    const int   k  = x >= 8.f ? 7 : (int)x;
    const float db = d.gain_db[k] + (d.gain_db[k + 1] - d.gain_db[k]) * (x - (float)k);
    return powf(10.f, db / 20.f);
}

class Processor
{
  public:
    // A new not-amp: a and b are its source weights (b only for Morph), which must
    // outlive it. Clears every bend; Apply() then sets this one's.
    void Begin(const Def& d, const float* a, const float* b)
    {
        def_ = &d;
        a_ = a;
        b_ = b;
        last_ = -1.f;
        rate_ = 1;
        std::memset(fb_, 0, sizeof(fb_));
        fbw_ = 0;
        ResetRate();
        nam_a2_daisy::bend::Clear();
    }

    void End() { def_ = nullptr; nam_a2_daisy::bend::Clear(); }
    bool Active() const { return def_ != nullptr; }
    const Def* Current() const { return def_; }

    // The weights to load for steer u: A, or the morph at its current t.
    // w must hold kA2WeightCount floats.
    const float* InitialWeights(float u, float* w) const
    {
        if(def_->kind != Kind::Morph)
            return a_;
        Morph(Param(*def_, u), w);
        return w;
    }

    // Once per block, before Process(): set the bend for steer u. A Morph whose
    // t has moved rewrites the weights through rewrite(w), with no prewarm, as
    // the engine allows between blocks. Returns the level correction (linear).
    template <class Rewrite>
    float Apply(float u, float* w, Rewrite rewrite)
    {
        using namespace nam_a2_daisy;
        const Def&  d = *def_;
        const float v = Param(d, u);
        switch(d.kind)
        {
            case Kind::Sine: bend::SetActivation(bend::kActSine, v); break;
            case Kind::Slope: bend::SetActivation(bend::kActSlope, v); break;
            case Kind::Freeze:
                bend::active[d.layer]  = true;
                bend::freezeP[d.layer] = (int)lroundf(v);
                break;
            case Kind::Fold:
                bend::active[d.layer] = true;
                bend::fold[d.layer]   = v;
                break;
            case Kind::Morph:
                if(fabsf(v - last_) > 0.0005f)
                {
                    Morph(v, w);
                    rewrite(w);
                    last_ = v;
                }
                break;
            case Kind::FbGain: fbg_ = v; fbd_ = (int)d.fixed; break;
            case Kind::FbPitch: fbg_ = d.fixed; fbd_ = (int)lroundf(v); break;
            case Kind::Rate:
            {
                const int r = (int)v;
                if(r != rate_)
                {
                    rate_ = r;
                    ResetRate();
                }
                break;
            }
        }
        if(fbd_ < kBlock)
            fbd_ = kBlock;  // the loop cannot close inside one block
        return LevelGain(d, u);
    }

    // One 48-sample block: x in, raw network output (before the level) in y.
    // run(in, out) runs the engine on one 48-sample block.
    template <class Run>
    void Process(const float* x, float* y, Run run)
    {
        const Kind k = def_->kind;
        if(k == Kind::FbGain || k == Kind::FbPitch)
        {
            float in[kBlock];
            for(int n = 0; n < kBlock; n++)
                in[n] = x[n] + fbg_ * fb_[(fbw_ + kFbRing - fbd_ + n) & (kFbRing - 1)];
            run(in, y);
            for(int n = 0; n < kBlock; n++)
            {
                const float v = std::isfinite(y[n]) ? y[n] : 0.f;
                y[n] = v;
                fb_[(fbw_ + n) & (kFbRing - 1)] = v;
            }
            fbw_ = (fbw_ + kBlock) & (kFbRing - 1);
        }
        else if(k == Kind::Rate && rate_ > 1)
        {
            // Streaming: average R samples into one, run the engine once 48 are
            // collected (every R blocks), and hold each output sample R times.
            // An output comes out 48·R samples after its input — see Latency().
            // Output before input, so a block's first output lands exactly 48·R
            // samples after its first input — the latency the dry side assumes.
            // x and y may be the same buffer (the firmware processes in place), so
            // the input sample is read before its output overwrites it. Writing
            // first fed the output back as input: a self-sustaining tone, heard on
            // the module as RATE BUG's and RATE SVT's "constant tone" (2026-10-02).
            for(int n = 0; n < kBlock; n++)
            {
                const float xn = x[n];
                y[n] = play_[pos_ / rate_];
                if(pos_ < kBlock * rate_ - 1)
                    pos_++;
                acc_ += xn;
                if(++accN_ == rate_)
                {
                    dec_[decN_++] = acc_ / (float)rate_;
                    acc_  = 0.f;
                    accN_ = 0;
                    if(decN_ == kBlock)
                    {
                        run(dec_, play_);
                        decN_ = 0;
                        pos_  = 0;
                    }
                }
            }
        }
        else
            run(x, y);
    }

    // Samples by which the not-amp's output trails its input; the dry side of
    // the mix is delayed to match.
    int Latency() const { return (def_ && def_->kind == Kind::Rate && rate_ > 1) ? kBlock * rate_ : 0; }

  private:
    void Morph(float t, float* w) const
    {
        for(int i = 0; i < nam_a2_daisy::kA2WeightCount; i++)
            w[i] = a_[i] + t * (b_[i] - a_[i]);
    }

    void ResetRate()
    {
        acc_  = 0.f;
        accN_ = 0;
        decN_ = 0;
        pos_  = 0;
        std::memset(play_, 0, sizeof(play_));
    }

    const Def*   def_ = nullptr;
    const float* a_   = nullptr;
    const float* b_   = nullptr;
    float        last_ = -1.f;

    float fb_[kFbRing] = {};
    int   fbw_ = 0, fbd_ = kBlock;
    float fbg_ = 0.f;

    int   rate_ = 1, accN_ = 0, decN_ = 0, pos_ = 0;
    float acc_ = 0.f;
    float dec_[kBlock] = {}, play_[kBlock] = {};
};

// The dry side of the mix, delayed to line up with a not-amp that has latency.
class DryDelay
{
  public:
    float Process(float x, int delay)
    {
        buf_[w_] = x;
        const float y = buf_[(w_ + kLen - delay) % kLen];
        w_ = (w_ + 1) % kLen;
        return y;
    }

  private:
    static constexpr int kLen = kBlock * kMaxRate + 1;
    float buf_[kLen] = {};
    int   w_ = 0;
};

} // namespace notamps
