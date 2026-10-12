// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Wish's pot and CV conditioning, after Beads' cv_reader.h (Emilie Gillet, MIT).
// Pure arithmetic, so tools/host_render.cpp checks it on the Mac.
//
// Beads filters every pot at its 4 kHz control rate (one-pole 0.05), with
// hysteresis on TIME, and bends three of them: the attenuverters get a dead
// zone and a square law, PITCH snaps through 0/1/3/4/7/12/24 semitones.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/hysteresis_filter.h"

namespace wish {

struct PotFilter
{
    enum Law : uint8_t { kNormal, kAttenuverter, kDetune };

    Law                      law = kNormal;
    float                    lp  = 0.0f;
    stmlib::HysteresisFilter hysteresis;

    /** Start settled on @p initial (a 0..1 knob), so nothing glides in. */
    void Init(Law l, float threshold, float initial)
    {
        law = l;
        hysteresis.Init(threshold);
        lp = Apply(initial);
        hysteresis.Process(lp, 0.0f);
    }

    /** Beads' law for this pot: 0..1 in; 0..1, -1..+1 or +-24 semitones out. */
    float Apply(float v) const
    {
        if (law == kAttenuverter)
        {
            v -= 0.5f;
            return (2.0f / 0.47f) * v * std::max(fabsf(v) - 0.03f, 0.0f);
        }
        if (law == kDetune)
        {
            float       detune = 2.0f * v - 1.0f;
            const float sign   = detune < 0.0f ? -1.0f : 1.0f;
            detune             = detune * sign * 6.0f;
            MAKE_INTEGRAL_FRACTIONAL(detune);
            static const float kIntervals[8] = {0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f};
            detune_fractional = std::max((detune_fractional - 0.33333333f) * 1.5f, 0.0f);
            const float a = kIntervals[detune_integral];
            const float b = kIntervals[detune_integral + 1];
            return stmlib::Crossfade(a, b, stmlib::SmoothStep(detune_fractional)) * sign;
        }
        return v;
    }

    /** One 4 kHz control block. */
    float Process(float v)
    {
        ONE_POLE(lp, Apply(v), 0.05f);
        return hysteresis.Process(lp);
    }
};

/** Beads' per-channel CV one-pole (coefficient per channel, cv_reader.cc). */
struct CvFilter
{
    float lp = 0.0f;
    float Process(float v, float coefficient)
    {
        ONE_POLE(lp, v, coefficient);
        return lp;
    }
};

}  // namespace wish
