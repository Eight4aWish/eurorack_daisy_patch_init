// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The building blocks of Mirth's FX bank (FX.md), written for it: no DaisySP, no LGPL,
// and plain C++ so the same code runs on the Patch SM and in the Mac harness
// (tools/fx_render.cpp).
//
// Every struct here is trivially constructible, with no constructors and no in-class
// initialisers, because the bank lives in DSY_SDRAM_BSS: its constructor would run before
// the SDRAM is up. Init() and Clear() set every field instead.

#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fx {

constexpr float kPi    = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

// x in 0..1 to a..b on a log scale: the map for times, frequencies and decays.
inline float map_exp(float x, float a, float b) { return a * powf(b / a, clampf(x, 0.f, 1.f)); }
inline float map_lin(float x, float a, float b) { return a + (b - a) * clampf(x, 0.f, 1.f); }

// A delay line of N samples, N a power of two. Read(d) is the sample from d writes before
// the last, so Read(0) is the last one written; ReadFrac() reads between samples with a cubic
// (Hermite) curve, which keeps a moving read smooth where linear interpolation dulls and
// warbles the top end.
template <size_t N>
struct Delay
{
    static_assert((N & (N - 1)) == 0, "Delay length must be a power of two");
    float    buf[N];
    uint32_t w;

    void Clear()
    {
        memset(buf, 0, sizeof(buf));
        w = 0;
    }
    void  Write(float x)
    {
        buf[w] = x;
        w      = (w + 1) & (N - 1);
    }
    float Read(uint32_t d) const { return buf[(w - 1 - d) & (N - 1)]; }
    float ReadFrac(float d) const
    {
        if(d < 1.f)
            d = 1.f;
        if(d > (float)(N - 4))
            d = (float)(N - 4);
        const uint32_t i  = (uint32_t)d;
        const float    t  = d - (float)i;
        const float    y0 = Read(i - 1), y1 = Read(i), y2 = Read(i + 1), y3 = Read(i + 2);
        const float    c1 = 0.5f * (y2 - y0);
        const float    c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float    c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
};

// One-pole low-pass and high-pass, set by corner frequency.
struct OnePoleLP
{
    float z, a;
    void  Clear() { z = 0.f; }
    void  SetHz(float hz, float sr) { a = 1.f - expf(-kTwoPi * hz / sr); }
    float Process(float x)
    {
        z += a * (x - z);
        return z;
    }
};
struct OnePoleHP
{
    OnePoleLP lp;
    void      Clear() { lp.Clear(); }
    void      SetHz(float hz, float sr) { lp.SetHz(hz, sr); }
    float     Process(float x) { return x - lp.Process(x); }
};

// Schroeder all-pass of a fixed length: passes every frequency at equal level and smears
// it in time. The reverbs' diffusers, and the plate's tank.
template <size_t N>
struct Allpass
{
    Delay<N> d;
    uint32_t len;
    float    g;
    void     Init(uint32_t length, float gain)
    {
        d.Clear();
        len = length < N - 1 ? length : N - 1;
        g   = gain;
    }
    void  Clear() { d.Clear(); }
    float Process(float x)
    {
        const float v = d.Read(len - 1);
        const float u = x + g * v;
        d.Write(u);
        return v - g * u;
    }
    float Tap(uint32_t i) const { return d.Read(i); }
};

// The 8x8 Hadamard matrix, in place and orthonormal (scaled by 1/sqrt(8)): every line of a
// feedback delay network feeds every other at equal strength, losing no energy.
inline void Hadamard8(float* x)
{
    for(int h = 1; h < 8; h <<= 1)
        for(int i = 0; i < 8; i += h << 1)
            for(int j = i; j < i + h; j++)
            {
                const float a = x[j], b = x[j + h];
                x[j]          = a + b;
                x[j + h]      = a - b;
            }
    constexpr float k = 0.35355339f;  // 1/sqrt(8)
    for(int i = 0; i < 8; i++)
        x[i] *= k;
}

// A sine LFO advanced once a block and read with a ramp in between: one sinf() a block,
// not one a sample.
struct BlockLfo
{
    float ph, inc_block, prev, next;
    void  Init(float hz, float sr, size_t block, float phase)
    {
        ph        = phase;
        inc_block = hz * (float)block / sr;
        prev = next = sinf(kTwoPi * ph);
    }
    void  SetHz(float hz, float sr, size_t block) { inc_block = hz * (float)block / sr; }
    void  StartBlock()
    {
        prev = next;
        ph += inc_block;
        if(ph >= 1.f)
            ph -= 1.f;
        next = sinf(kTwoPi * ph);
    }
    float At(float frac) const { return prev + (next - prev) * frac; }  // frac 0..1 through the block
};

// A value that glides to its target, one pole per sample: for delay times, so a knob or a new
// tap tempo bends the repeats (as tape would) instead of jumping and clicking.
struct Glide
{
    float v, a;
    bool  primed;
    void  Init(float seconds, float sr)
    {
        a      = 1.f - expf(-1.f / (seconds * sr));
        primed = false;
        v      = 0.f;
    }
    float Process(float target)
    {
        if(!primed)
        {
            v      = target;
            primed = true;
        }
        v += a * (target - v);
        return v;
    }
};

// White noise in -1..1, a 32-bit LCG: cheap, and plenty for tape drift.
struct Noise
{
    uint32_t s;
    void     Seed(uint32_t seed) { s = seed ? seed : 1u; }
    float    Next()
    {
        s = s * 1664525u + 1013904223u;
        return (float)(int32_t)s * (1.f / 2147483648.f);
    }
};

// A soft saturator, tanh-shaped (a Pade approximant, exact to about 2% and bounded at +-1).
inline float soft_clip(float x)
{
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// An all-pass whose length is modulated: the plate's tank, where the slow wobble breaks up
// the metallic ring a fixed tank would have.
template <size_t N>
struct ModAllpass
{
    Delay<N> d;
    float    len, g;
    void     Init(float length, float gain)
    {
        d.Clear();
        len = length;
        g   = gain;
    }
    void  Clear() { d.Clear(); }
    float Process(float x, float mod)
    {
        const float v = d.ReadFrac(len - 1.f + mod);
        const float u = x + g * v;
        d.Write(u);
        return v - g * u;
    }
    float Tap(uint32_t i) const { return d.Read(i); }
};

// An octave-up pitch shifter: two read heads sweep through a 43 ms window faster than the
// write, half a window apart, each faded in and out with a raised cosine so the two always
// sum to one. Grainy by nature, which in SHIMMER's feedback loop is the sound.
struct OctaveUp
{
    Delay<4096> d;
    float       ph;
    static constexpr float kWin = 2048.f;
    void Clear()
    {
        d.Clear();
        ph = 0.f;
    }
    float Process(float x)
    {
        d.Write(x);
        ph += 1.f / kWin;  // ratio 2: the heads gain one sample on the write every sample
        if(ph >= 1.f)
            ph -= 1.f;
        float       p2 = ph + 0.5f;
        if(p2 >= 1.f)
            p2 -= 1.f;
        const float w1 = 0.5f - 0.5f * cosf(kTwoPi * ph);
        const float w2 = 0.5f - 0.5f * cosf(kTwoPi * p2);
        return w1 * d.ReadFrac(2.f + kWin * (1.f - ph)) + w2 * d.ReadFrac(2.f + kWin * (1.f - p2));
    }
};

}  // namespace fx
