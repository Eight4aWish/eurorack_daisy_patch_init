#pragma once
/**
 * gran_layer.h — the Wasp's granular layer, played after a FILTERS network (David's
 * design, 2026-10-20; CLAUDE.md, "Faking the randomness on top").
 *
 * The network learns what the Wasp does every time; this adds what it does differently
 * every time, borrowed from recordings of it misbehaving. It analyses nothing. Every 512
 * samples it takes the last 2,048 of the network's output, scales each FFT bin by a gain
 * in dB drawn from a bank of recorded lifts and dips, and overlap-adds the result with a
 * Hann window: with every gain at 0 dB the output is the input, kDelay samples late.
 *
 * The bank, the grains and the phase-in are tools/wasp_layer.py's, which builds the
 * layer section of a version-2 .grnb and holds this algorithm in float64 (Stream). The
 * two must agree; tools/gran_check.py runs this header on the desktop
 * (tools/gran_host.cpp) against it before anything is flashed.
 *
 * Grains: two streams, a new grain every L frames, offset by L / 2, crossfaded with
 * triangles. A grain is one take's lift and dip against the average for L consecutive
 * frames, starting at a frame whose CV was within the CV window of the CV now, shifted
 * so it sits at the same distance from the cutoff. The phase-in: gains scale from 0 at
 * the section's first Res to 1 at its second (8 and 9 for the Wasp); between two
 * banks' Res marks each grain takes the higher bank with the chance given by where Res
 * sits between them.
 *
 * Timing: one frame's work is three audio blocks (Advance(), once per block: the FFT;
 * the gains; the inverse FFT and overlap-add), about 1,800 cycles a sample each on the
 * H750 at most, so it never lands on one block. kDelay leaves room for that. The dry
 * side is delayed by the same, by Process().
 *
 * Portable C++, standard library only. The FFT is a plain radix-2 one (1,024-point
 * complex, packed real), so the Patch SM and the desktop run identical code.
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gran
{

inline constexpr int      kFft       = 2048;
inline constexpr int      kHalf      = kFft / 2;  // the complex FFT's size
inline constexpr int      kBins      = kFft / 2 + 1;
inline constexpr int      kHop       = 512;
inline constexpr int      kBlock     = 48;
inline constexpr int      kDelay     = kFft + 4 * kBlock;  // 2,240 samples, 46.7 ms
inline constexpr int      kRing      = 4096;
inline constexpr uint32_t kMask      = kRing - 1;
inline constexpr int      kMaxBanks  = 4;
inline constexpr int      kMaxBands  = 128;
inline constexpr int      kMaxTakes  = 4;
inline constexpr int      kTries     = 32;
inline constexpr int      kHeader    = 48;
inline constexpr int      kBankDesc  = 16;
inline constexpr uint32_t kMagic     = 0x594C5247u;  // "GRLY" read little-endian
inline constexpr double   kTwoPi     = 6.283185307179586;

struct Bank
{
    float          res = 0.f, slope = 0.f;
    uint32_t       frames = 0, sources = 0;
    const uint8_t* cv    = nullptr;  // frames x f32, little-endian, any alignment
    const int8_t*  gains = nullptr;  // takes x frames x bands
};

// A parsed layer section. Points into the caller's buffer, which must outlive it.
struct Layer
{
    int   banks = 0, bands = 0, takes = 0, grain = 0;
    float lc0 = 0.f, dlc = 1.f, db_step = 0.1f, cv_window = 1.f, res_start = 8.f, res_full = 9.f;
    Bank  bank[kMaxBanks];
};

inline float RdF(const uint8_t* p)
{
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

// Parse a layer section (tools/wasp_layer.py pack()). False on anything out of range or
// outside the buffer: a damaged card must not reach the audio as gains.
inline bool Parse(const uint8_t* p, size_t len, Layer& L)
{
    auto u16 = [&](size_t o) { return (uint32_t)(p[o] | (p[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return (uint32_t)p[o] | ((uint32_t)p[o + 1] << 8) | ((uint32_t)p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24); };
    if(len < (size_t)kHeader || u32(0) != kMagic || u16(4) != 1)
        return false;
    const int n = (int)u16(6), bands = (int)u16(8), takes = (int)u16(10), grain = (int)u16(12);
    if(n < 1 || n > kMaxBanks || bands < 2 || bands > kMaxBands || takes < 1 || takes > kMaxTakes || grain < 2
       || u16(14) != (uint32_t)kFft || u16(16) != (uint32_t)kHop || len < (size_t)(kHeader + kBankDesc * n))
        return false;
    Layer out;
    out.banks = n, out.bands = bands, out.takes = takes, out.grain = grain;
    out.lc0 = RdF(p + 20), out.dlc = RdF(p + 24), out.db_step = RdF(p + 28), out.cv_window = RdF(p + 32);
    out.res_start = RdF(p + 36), out.res_full = RdF(p + 40);
    if(!(out.dlc > 0.f) || !(out.res_full > out.res_start))
        return false;
    for(int i = 0; i < n; i++)
    {
        const uint8_t* d  = p + kHeader + kBankDesc * i;
        Bank&          b  = out.bank[i];
        b.res              = RdF(d);
        b.slope            = RdF(d + 4);
        b.frames           = u32(kHeader + kBankDesc * i + 8);
        const uint32_t off = u32(kHeader + kBankDesc * i + 12);
        if(b.frames <= (uint32_t)grain || (uint64_t)off + 4ull * b.frames + (uint64_t)takes * b.frames * bands > len
           || (i > 0 && !(b.res > out.bank[i - 1].res)))
            return false;
        b.sources = b.frames - (uint32_t)grain + 1;
        b.cv      = p + off;
        b.gains   = reinterpret_cast<const int8_t*>(p + off + 4ull * b.frames);
    }
    L = out;
    return true;
}

// Marsaglia's xorshift32, as wasp_layer.XorShift32.
struct Rng
{
    uint32_t x = 1;
    uint32_t Next()
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    }
    float Uniform() { return (float)(Next() >> 8) * (1.f / 16777216.f); }
};

// 2^x for the gains' range: round to the nearest integer, then e^(f ln 2) for |f| <= 0.5
// by its series to the sixth power (error about 1e-7), then the exponent bits.
inline float Exp2(float x)
{
    const float r = std::floor(x + 0.5f);
    const float y = (x - r) * 0.69314718f;
    float       p = 1.f / 720.f;
    p             = p * y + 1.f / 120.f;
    p             = p * y + 1.f / 24.f;
    p             = p * y + 1.f / 6.f;
    p             = p * y + 0.5f;
    p             = p * y + 1.f;
    p             = p * y + 1.f;
    int32_t bits;
    std::memcpy(&bits, &p, 4);
    bits += (int32_t)r * (1 << 23);
    std::memcpy(&p, &bits, 4);
    return p;
}

class Engine
{
  public:
    // The tables. Once, at start-up (std::cos and std::sin, so not in the audio path).
    void Init()
    {
        for(int n = 0; n < kFft; n++)
            win_[n] = (float)(0.5 - 0.5 * std::cos(kTwoPi * n / kFft));  // periodic Hann
        for(int k = 0; k < kHalf / 2; k++)
        {
            cos_[k] = (float)std::cos(kTwoPi * k / kHalf);
            sin_[k] = (float)std::sin(kTwoPi * k / kHalf);
        }
        for(int k = 0; k <= kHalf; k++)
        {
            wcos_[k] = (float)std::cos(kTwoPi * k / kFft);
            wsin_[k] = (float)std::sin(kTwoPi * k / kFft);
        }
        int bits = 0;
        while((1 << bits) < kHalf)
            bits++;
        for(int i = 0; i < kHalf; i++)
        {
            int r = 0;
            for(int b = 0; b < bits; b++)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            rev_[i] = (uint16_t)r;
        }
        for(int k = 0; k < kBins; k++)
        {
            const double f = (double)k * 48000.0 / kFft;
            lfq_[k]        = (float)std::log2(f > 1.0 ? f : 1.0);
        }
    }

    // Silence, history cleared, grains forgotten. Main loop, with the output muted.
    void Reset(uint32_t seed)
    {
        std::memset(in_, 0, sizeof(in_));
        std::memset(ola_, 0, sizeof(ola_));
        t_ = 0, stage_ = 0, frame_ready_ = false, frame_ = 0;
        rng_.x = seed ? seed : 1;
        for(auto& g : grain_)
            g.live = false;
    }

    // One sample: x in, the layer's output and the dry input out, both kDelay late.
    inline float Process(float x, float& dry)
    {
        in_[t_ & kMask]     = x;
        dry                 = in_[(t_ - kDelay) & kMask];
        const uint32_t r    = (t_ - kDelay) & kMask;
        const float    y    = ola_[r];
        ola_[r]             = 0.f;
        if(((t_ + 1) % kHop) == 0)
        {
            frame_ready_ = true;
            frame_end_   = t_;
        }
        t_++;
        return y;
    }

    // Once per block, after its samples, with that block's cutoff (volts) and Res: one
    // stage of the frame in hand. A frame takes the cutoff and Res of the block it
    // completed in.
    void Advance(const Layer& L, float cut, float res)
    {
        switch(stage_)
        {
            case 0:
                if(!frame_ready_)
                    return;
                frame_ready_ = false;
                f_end_ = frame_end_, f_cut_ = cut, f_res_ = res;
                Forward();
                stage_ = 1;
                break;
            case 1:
                Gains(L);
                stage_ = 2;
                break;
            default:
                Inverse();
                stage_ = 0;
                break;
        }
    }

  private:
    struct Grain
    {
        bool     live = false;
        int      bank = 0;
        uint32_t take = 0, src = 0;
    };

    static float Clip10(float v) { return v < 0.f ? 0.f : (v > 10.f ? 10.f : v); }

    // The depth, the banks either side of Res, and the chance of the higher one; the
    // comparisons in double so they match wasp_layer.py's exactly.
    static void PhaseIn(const Layer& L, float res, float& depth, int& lo, int& hi, double& p)
    {
        double d = ((double)res - L.res_start) / ((double)L.res_full - L.res_start);
        depth    = (float)(d < 0.0 ? 0.0 : (d > 1.0 ? 1.0 : d));
        lo = hi = 0, p = 0.0;
        if((double)res <= L.bank[0].res)
            return;
        for(int i = 0; i + 1 < L.banks; i++)
            if((double)res < L.bank[i + 1].res)
            {
                lo = i, hi = i + 1;
                p  = ((double)res - L.bank[i].res) / ((double)L.bank[i + 1].res - L.bank[i].res);
                return;
            }
        lo = hi = L.banks - 1;
    }

    void NewGrain(Grain& g, const Layer& L)
    {
        float  depth;
        int    lo, hi;
        double p;
        PhaseIn(L, f_res_, depth, lo, hi, p);
        g.bank        = ((double)rng_.Uniform() < p) ? hi : lo;
        const Bank& b = L.bank[g.bank];
        g.take        = rng_.Next() % (uint32_t)L.takes;
        uint32_t src  = 0;
        for(int t = 0; t < kTries; t++)
        {
            src = rng_.Next() % b.sources;
            if(std::fabs((double)RdF(b.cv + 4 * src) - (double)f_cut_) < (double)L.cv_window)
                break;
        }
        g.src  = src;
        g.live = true;
    }

    // In-place radix-2 FFT of kHalf points, forward (e^-) or inverse (e^+, unscaled).
    void Fft(bool inverse)
    {
        for(int i = 0; i < kHalf; i++)
        {
            const int j = rev_[i];
            if(j > i)
            {
                float t = re_[i];
                re_[i]  = re_[j];
                re_[j]  = t;
                t       = im_[i];
                im_[i]  = im_[j];
                im_[j]  = t;
            }
        }
        const float sgn = inverse ? 1.f : -1.f;
        for(int len = 2; len <= kHalf; len <<= 1)
        {
            const int half = len >> 1, step = kHalf / len;
            for(int i = 0; i < kHalf; i += len)
                for(int k = 0; k < half; k++)
                {
                    const float wr = cos_[k * step], wi = sgn * sin_[k * step];
                    const int   a = i + k, b = a + half;
                    const float tr = re_[b] * wr - im_[b] * wi;
                    const float ti = re_[b] * wi + im_[b] * wr;
                    re_[b]         = re_[a] - tr;
                    im_[b]         = im_[a] - ti;
                    re_[a] += tr;
                    im_[a] += ti;
                }
        }
    }

    // Stage 1: the frame, windowed, packed two real samples to a complex one, through
    // the FFT, and split into the real FFT's kBins bins.
    void Forward()
    {
        const uint32_t start = f_end_ - (kFft - 1);
        for(int n = 0; n < kHalf; n++)
        {
            re_[n] = in_[(start + 2 * n) & kMask] * win_[2 * n];
            im_[n] = in_[(start + 2 * n + 1) & kMask] * win_[2 * n + 1];
        }
        Fft(false);
        // X[k] = E[k] + W^k O[k], E and O the even and odd samples' FFTs, W = e^(-2 pi i / kFft).
        xr_[0]     = re_[0] + im_[0];
        xi_[0]     = 0.f;
        xr_[kHalf] = re_[0] - im_[0];
        xi_[kHalf] = 0.f;
        for(int k = 1; k < kHalf; k++)
        {
            const float zr = re_[k], zi = im_[k], cr = re_[kHalf - k], ci = -im_[kHalf - k];
            const float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci);
            const float orr = 0.5f * (zi - ci), oi = -0.5f * (zr - cr);  // (Z - conj Z') / 2i
            const float wr = wcos_[k], wi = -wsin_[k];
            xr_[k]         = er + wr * orr - wi * oi;
            xi_[k]         = ei + wr * oi + wi * orr;
        }
    }

    // Stage 2: this frame's gains, both streams advanced by one frame, applied to the bins.
    void Gains(const Layer& L)
    {
        float  depth;
        int    lo, hi;
        double p;
        PhaseIn(L, f_res_, depth, lo, hi, p);
        const int G = L.grain;
        for(int k = 0; k < kBins; k++)
            g_[k] = 0.f;
        const float inv_dlc = 1.f / L.dlc, top = (float)(L.bands - 1);
        for(int s = 0; s < 2; s++)
        {
            const int i = (int)((frame_ + (uint32_t)(s ? G / 2 : 0)) % (uint32_t)G);
            Grain&    g = grain_[s];
            if(i == 0 || !g.live)
                NewGrain(g, L);
            if(depth == 0.f)
                continue;
            const Bank&   b     = L.bank[g.bank];
            const float   w     = 1.f - std::fabs(((float)i + 0.5f) / (float)G * 2.f - 1.f);
            const float   shift = b.slope * (Clip10(RdF(b.cv + 4 * (g.src + i))) - Clip10(f_cut_));
            const float   base  = (shift - L.lc0) * inv_dlc;
            const int8_t* row   = b.gains + ((size_t)g.take * b.frames + g.src + i) * (size_t)L.bands;
            const float   a     = depth * w * L.db_step;
            for(int k = 0; k < kBins; k++)
            {
                const float pos = lfq_[k] * inv_dlc + base;
                float       v;
                if(pos <= 0.f)
                    v = row[0];
                else if(pos >= top)
                    v = row[L.bands - 1];
                else
                {
                    const int   j = (int)pos;
                    const float f = pos - (float)j;
                    v             = (float)row[j] + f * (float)(row[j + 1] - row[j]);
                }
                g_[k] += a * v;
            }
        }
        frame_++;
        if(depth == 0.f)
            return;
        for(int k = 0; k < kBins; k++)
        {
            const float m = Exp2(g_[k] * 0.16609640474f);  // 10^(dB / 20)
            xr_[k] *= m;
            xi_[k] *= m;
        }
    }

    // Stage 3: back to kHalf complex points, the inverse FFT, unpacked, windowed and
    // overlap-added where the frame came from.
    void Inverse()
    {
        // E[k] = (X[k] + conj X[N/2-k]) / 2, O[k] = (X[k] - conj X[N/2-k]) W^-k / 2, Z = E + i O.
        for(int k = 0; k < kHalf; k++)
        {
            const float ar = xr_[k], ai = xi_[k], br = xr_[kHalf - k], bi = -xi_[kHalf - k];
            const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
            const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
            const float wr = wcos_[k], wi = wsin_[k];  // W^-k
            const float orr = dr * wr - di * wi, oi = dr * wi + di * wr;
            re_[k]          = er - oi;
            im_[k]          = ei + orr;
        }
        Fft(true);
        const uint32_t start = f_end_ - (kFft - 1);
        const float    norm  = 1.f / ((float)kHalf * 1.5f);  // the inverse's 1/N, and the Hann overlap's sum of squares
        for(int n = 0; n < kHalf; n++)
        {
            ola_[(start + 2 * n) & kMask] += re_[n] * norm * win_[2 * n];
            ola_[(start + 2 * n + 1) & kMask] += im_[n] * norm * win_[2 * n + 1];
        }
    }

    float    in_[kRing];
    float    ola_[kRing];
    float    win_[kFft];
    float    cos_[kHalf / 2], sin_[kHalf / 2];
    float    wcos_[kHalf + 1], wsin_[kHalf + 1];
    float    lfq_[kBins];
    float    re_[kHalf], im_[kHalf];
    float    xr_[kBins], xi_[kBins];
    float    g_[kBins];
    uint16_t rev_[kHalf];
    uint32_t t_ = 0, frame_end_ = 0, f_end_ = 0, frame_ = 0;
    int      stage_       = 0;
    bool     frame_ready_ = false;
    float    f_cut_ = 0.f, f_res_ = 0.f;
    Grain    grain_[2];
    Rng      rng_;
};

} // namespace gran
