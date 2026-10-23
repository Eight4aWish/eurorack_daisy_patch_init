/**
 * osc_layer — Swords' self-oscillation, added to its network's output (SWORDS_OSC, FLT7;
 * SWORDS.md, "SWORDS_OSC"). The network (Res CV 0-4 V) is where Swords repeats itself; past its onset Swords
 * oscillates, and that is this layer, built part by part from measurements rather than
 * learned:
 *
 *   when      on when Res CV passes the onset line, off when it falls under the stop line
 *             (both piecewise linear in V/OCT through three measured knots, step 0), so it
 *             starts lower at higher cutoffs and has hysteresis
 *   pitch     hz_at at V/OCT v_at, oct_per_v octaves per volt bending by oct_curve per volt
 *             squared (sw-glide and sw-a's riff takes, at Res r_ref: the top cutoffs fall
 *             under a straight line), and cents_per_res cents per volt of Res CV
 *   level     amp_on volts peak at the onset, rising amp_slope per volt to amp_max (step 0),
 *             ducked by the input's level, 1 / (1 + duck_k A): the input and the oscillation
 *             share Swords' clip (sw-a's riff at 5 V peak: the oscillation 7.5 dB down)
 *   shape     a sine plus its 2nd, 3rd and 5th harmonics at the measured levels, the odd
 *             ones growing from h*_lo to h*_hi dB between h_lo_dr and h_hi_dr volts past the
 *             onset (step 0)
 *   locking   Adler's model on the input's part near the pitch only: the input mixed down
 *             at the oscillator's own phase and low-passed (within lock_bw of the pitch)
 *             gives that part's level A and its phase against the oscillator's; dphi/dt =
 *             w + W(A) sin(that phase), so it locks when the detuning is under W(A) =
 *             lock_c * A^lock_p * w, the same fraction of the pitch in Hz either way
 *             (sw-glide: about 1.2%, 8.7% and 31% at 0.2, 1 and 3 V), and is barely moved
 *             by the rest of the input (a riff pulls it tens of cents, as measured)
 *   input     with the input sounding, Swords starts by Res CV on_with_input wherever its
 *             onset without input is higher (sw-a: every riff take at 4.5 V oscillates,
 *             none at 4.0 V below the top cutoff)
 *   start     a random phase each time it starts
 *
 * Volts in, volts out (u_volts and y_volts turn the network's units into volts and back).
 * Plain C++, so tools/osc_host.cpp runs the same code on the desktop. Six transcendentals a
 * sample: the harmonics come from sin(phase) by the multiple-angle identities.
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace osc
{

constexpr int kKnots  = 3;
constexpr int kParams = 34;  // the floats in the file's oscillator block (tools/osc_layer.py)

struct Params
{
    float on_v[kKnots], on_r[kKnots], off_r[kKnots];         // V/OCT knots; Res CV at onset, stop
    float hz_at, v_at, oct_per_v, oct_curve, cents_per_res, r_ref;  // pitch
    float amp_on, amp_slope, amp_max, duck_k;                // level, volts peak; ducking
    float h2_db, h3_lo_db, h3_hi_db, h5_lo_db, h5_hi_db, h_lo_dr, h_hi_dr;  // shape
    float lock_c, lock_p, lock_bw;                           // locking: fraction of w, exponent, bandwidth
    float on_with_input;                                     // Res CV it starts by with input
    float attack_s, release_s;                               // the level's rise and fall
    float u_volts, y_volts;                                  // volts per network unit, in and out
};

/** Params from the file's block of kParams floats, in the struct's order. */
inline void FromFloats(const float* f, Params& p)
{
    int i = 0;
    for(int k = 0; k < kKnots; k++)
        p.on_v[k] = f[i++];
    for(int k = 0; k < kKnots; k++)
        p.on_r[k] = f[i++];
    for(int k = 0; k < kKnots; k++)
        p.off_r[k] = f[i++];
    p.hz_at = f[i++], p.v_at = f[i++], p.oct_per_v = f[i++], p.oct_curve = f[i++];
    p.cents_per_res = f[i++], p.r_ref = f[i++];
    p.amp_on = f[i++], p.amp_slope = f[i++], p.amp_max = f[i++], p.duck_k = f[i++];
    p.h2_db = f[i++], p.h3_lo_db = f[i++], p.h3_hi_db = f[i++], p.h5_lo_db = f[i++], p.h5_hi_db = f[i++];
    p.h_lo_dr = f[i++], p.h_hi_dr = f[i++];
    p.lock_c = f[i++], p.lock_p = f[i++], p.lock_bw = f[i++];
    p.on_with_input = f[i++];
    p.attack_s = f[i++], p.release_s = f[i++];
    p.u_volts = f[i++], p.y_volts = f[i++];
}

class Layer
{
  public:
    void Init(const Params& p, float sr)
    {
        p_  = p;
        sr_ = sr;
        att_ = 1.f - std::exp(-1.f / (p.attack_s * sr));
        rel_ = 1.f - std::exp(-1.f / (p.release_s * sr));
        env_rel_ = 1.f - std::exp(-1.f / (0.05f * sr));  // the input's level, 50 ms release
        c2_      = Db(p.h2_db);
        Reset(1);
    }

    void Reset(uint32_t seed)
    {
        rng_   = seed ? seed : 1;
        on_    = false;
        amp_   = 0.f;
        phase_ = 0.f;
        a_in_  = 0.f;
        zs1_ = zs2_ = zc1_ = zc2_ = 0.f;
    }

    /** One sample: the network's audio input u and the controls (V/OCT volts, Res CV volts);
     *  returns what the layer adds to the network's output, in the network's units. */
    float Process(float u, float v_cut, float r_res)
    {
        const float uv = u * p_.u_volts;
        // The input's level (peak, fast attack, 50 ms release), for the locking.
        const float au = std::fabs(uv);
        a_in_ = au > a_in_ ? au : a_in_ + env_rel_ * (au - a_in_);

        // On and off, with the measured hysteresis; a random phase at each start. With the
        // input sounding, it starts by on_with_input where the no-input onset is higher.
        float       r_on  = Line(p_.on_r, v_cut);
        const float r_off = Line(p_.off_r, v_cut);
        if(a_in_ > 0.5f && r_on > p_.on_with_input)
            r_on = p_.on_with_input;
        const float r_stop = r_off < r_on - 0.05f ? r_off : r_on - 0.05f;
        if(!on_ && r_res >= r_on)
        {
            on_    = true;
            phase_ = 6.2831853f * Rand();
        }
        else if(on_ && r_res < r_stop)
            on_ = false;

        // The level it heads for, and how far past the onset it is (for its brightness).
        const float past   = r_res - r_on;
        float       target = 0.f;
        if(on_)
        {
            target = p_.amp_on + p_.amp_slope * (past > 0.f ? past : 0.f);
            target = target < p_.amp_max ? target : p_.amp_max;
            target /= 1.f + p_.duck_k * a_in_;
        }
        amp_ += (target > amp_ ? att_ : rel_) * (target - amp_);
        if(amp_ < 1e-5f && !on_)
            return 0.f;

        // Pitch, and the pull of the input's part near it (Adler): mixed down at the
        // oscillator's phase and low-passed twice within lock_bw of the pitch, zs and zc are
        // that part's (A/2) sin and (A/2) cos of its phase against the oscillator's.
        const float dv = v_cut - p_.v_at;
        const float hz = p_.hz_at
                         * std::exp2(p_.oct_per_v * dv + p_.oct_curve * dv * dv
                                     + p_.cents_per_res * (r_res - p_.r_ref) / 1200.f);
        const float w  = 6.2831853f * hz;
        const float g  = 1.f - std::exp(-w * p_.lock_bw / sr_);
        const float cs = std::cos(phase_), sn = std::sin(phase_);
        zs1_ += g * (uv * cs - zs1_), zs2_ += g * (zs1_ - zs2_);
        zc1_ += g * (uv * sn - zc1_), zc2_ += g * (zc1_ - zc2_);
        const float a    = 2.f * std::sqrt(zs2_ * zs2_ + zc2_ * zc2_);
        float       lock = p_.lock_c * std::pow(a, p_.lock_p);
        lock             = lock < 0.45f ? lock : 0.45f;
        phase_ += (w + (a > 1e-6f ? lock * w * 2.f * zs2_ / a : 0.f)) / sr_;
        if(phase_ > 6.2831853f)
            phase_ -= 6.2831853f;
        else if(phase_ < 0.f)
            phase_ += 6.2831853f;

        // Its shape, at the phase this sample started from: the odd harmonics brighten from
        // h_lo_dr to h_hi_dr past the onset. cos 2x = 1 - 2s^2, sin 3x = 3s - 4s^3,
        // sin 5x = 5s - 20s^3 + 16s^5, with s = sin x.
        float t = (past - p_.h_lo_dr) / (p_.h_hi_dr - p_.h_lo_dr);
        t       = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        const float c3 = Db(p_.h3_lo_db + t * (p_.h3_hi_db - p_.h3_lo_db));
        const float c5 = Db(p_.h5_lo_db + t * (p_.h5_hi_db - p_.h5_lo_db));
        const float s2 = sn * sn, s3 = s2 * sn;
        const float y  = sn + c2_ * (1.f - 2.f * s2) + c3 * (3.f * sn - 4.f * s3)
                        + c5 * (5.f * sn - 20.f * s3 + 16.f * s3 * s2);
        return amp_ * y / p_.y_volts;  // amp_on/amp_max are the waveform's measured peaks
    }

    bool On() const { return on_; }

  private:
    // Piecewise linear through the knots, extended past the ends by the end segments.
    float Line(const float* r, float v) const
    {
        const float* x = p_.on_v;
        const int    i = v < x[1] ? 0 : 1;
        return r[i] + (r[i + 1] - r[i]) * (v - x[i]) / (x[i + 1] - x[i]);
    }
    static float Db(float db) { return std::exp(db * 0.11512925f); }  // 10^(db/20)
    float Rand()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return (rng_ >> 8) * (1.f / 16777216.f);
    }

    Params   p_ {};
    float    sr_ = 48000.f, att_ = 0.f, rel_ = 0.f, env_rel_ = 0.f, c2_ = 0.f;
    float    amp_ = 0.f, phase_ = 0.f, a_in_ = 0.f;
    float    zs1_ = 0.f, zs2_ = 0.f, zc1_ = 0.f, zc2_ = 0.f;
    bool     on_  = false;
    uint32_t rng_ = 1;
};

} // namespace osc
