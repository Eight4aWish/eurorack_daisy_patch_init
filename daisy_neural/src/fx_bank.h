// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Mirth's FX bank: eight effects, four reverbs and four delays, as a fourth bank after
// FILTERS. Built only with `make FX=1`; unreleased (David, 2026-11-11). The design,
// and why each effect is what it is, is FX.md.
//
// The effects are in fx/: the reverbs (CLASSIC, TANK, SHIMMER on one feedback delay
// network, PLATE on Dattorro's plate) in fx/reverb.h, the delays in fx/delays.h, the building
// blocks in fx/dsp.h. All written for Mirth: MIT, no DaisySP, no LGPL.
//
// The host (main.cpp) owns everything the effects share: reading knobs 2-4 once a block,
// smoothing them, the tap tempo off gate in 1, the MIX, the swap fade and the DC blocker.
// An effect sees stereo in, stereo out, three controls in 0..1 and the tap.

#pragma once
#include <cstddef>
#include "fx/delays.h"
#include "fx/reverb.h"

namespace fx {

// Knob labels are two letters, for the panel's ten characters a row; "" is a knob that
// does nothing on that effect, and the display shows nothing for it (Mirth's rule).
struct Def
{
    const char* name;    // up to ten characters
    const char* lab[3];  // knobs 2, 3, 4
};

inline constexpr Def kDefs[] = {
    {"CLASSIC", {"De", "To", "Pr"}},   // decay, tone, pre-delay
    {"PLATE", {"Pr", "To", "De"}},     // pre-delay, tone, decay
    {"TANK", {"Sz", "Dp", "Md"}},      // size, damping, modulation
    {"SHIMMER", {"De", "To", "Sh"}},   // decay, tone, shimmer
    {"PING", {"Tm", "Fb", "Dp"}},      // time, feedback, damping
    {"TAPE", {"Tm", "Fb", "Wr"}},      // time, feedback, wear
    {"MULTITAP", {"Tm", "Pt", "Fb"}},  // time, pattern, feedback
    {"ECHOVERB", {"Tm", "Fb", "Rv"}},  // time, feedback, reverb
};
inline constexpr int kCount = sizeof(kDefs) / sizeof(kDefs[0]);

// The tap tempo (Tap) is in fx/delays.h, with the delays that use it.

class Bank
{
  public:
    void Init(float samplerate, size_t block)
    {
        sr_ = samplerate;
        classic_.Init(samplerate, block);
        plate_.Init(samplerate, block);
        tank_.Init(samplerate, block);
        shimmer_.Init(samplerate, block);
        ping_.Init(samplerate);
        tape_.Init(samplerate);
        multi_.Init(samplerate);
        echo_.Init(samplerate, block);
        current_ = 0;
    }

    // On a change of effect, from the main loop while the output is muted (Mirth's swap
    // fade): the new effect starts from silence, its delay memory cleared, which is too slow
    // for the audio callback.
    void Reset(int index)
    {
        current_ = index;
        switch(index)
        {
            case 0: classic_.Clear(); break;
            case 1: plate_.Clear(); break;
            case 2: tank_.Clear(); break;
            case 3: shimmer_.Clear(); break;
            case 4: ping_.Clear(); break;
            case 5: tape_.Clear(); break;
            case 6: multi_.Clear(); break;
            case 7: echo_.Clear(); break;
            default: break;
        }
    }

    int Current() const { return current_; }

    // One block. p[0..2] are knobs 2-4, already smoothed, 0..1.
    void Process(const float* inL,
                 const float* inR,
                 float*       outL,
                 float*       outR,
                 size_t       n,
                 const float  p[3],
                 const Tap&   tap)
    {
        const float inv_n = 1.f / (float)n;
#define FX_FRAC ((float)k + 0.5f) * inv_n
        switch(current_)
        {
            case 0:
                classic_.Block(p);
                for(size_t k = 0; k < n; k++)
                    classic_.Process(inL[k], inR[k], FX_FRAC, outL[k], outR[k]);
                break;
            case 1:
                plate_.Block(p);
                for(size_t k = 0; k < n; k++)
                    plate_.Process(inL[k], inR[k], FX_FRAC, outL[k], outR[k]);
                break;
            case 2:
                tank_.Block(p);
                for(size_t k = 0; k < n; k++)
                    tank_.Process(inL[k], inR[k], FX_FRAC, outL[k], outR[k]);
                break;
            case 3:
                shimmer_.Block(p);
                for(size_t k = 0; k < n; k++)
                    shimmer_.Process(inL[k], inR[k], FX_FRAC, outL[k], outR[k]);
                break;
            case 4:
                ping_.Block(p, tap);
                for(size_t k = 0; k < n; k++)
                    ping_.Process(inL[k], inR[k], outL[k], outR[k]);
                break;
            case 5:
                tape_.Block(p, tap);
                for(size_t k = 0; k < n; k++)
                    tape_.Process(inL[k], inR[k], outL[k], outR[k]);
                break;
            case 6:
                multi_.Block(p, tap);
                for(size_t k = 0; k < n; k++)
                    multi_.Process(inL[k], inR[k], outL[k], outR[k]);
                break;
            default:
                echo_.Block(p, tap);
                for(size_t k = 0; k < n; k++)
                    echo_.Process(inL[k], inR[k], FX_FRAC, outL[k], outR[k]);
                break;
        }
#undef FX_FRAC
    }

  private:
    float    sr_;
    int      current_;
    Classic  classic_;
    Plate    plate_;
    Tank     tank_;
    Shimmer  shimmer_;
    Ping     ping_;
    Tape     tape_;
    MultiTap multi_;
    EchoVerb echo_;
};

}  // namespace fx
