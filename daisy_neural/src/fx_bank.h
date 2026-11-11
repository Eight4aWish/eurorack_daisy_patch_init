// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Mirth's FX bank: eight effects, four reverbs and four delays, as a fourth bank after
// FILTERS. Built only with `make FX=1`; unreleased (David, 2026-11-11). The design,
// and why each effect is what it is, is FX.md.
//
// Step 2 of FX.md, the plumbing: every effect passes its input straight through, so
// the bank, the switching, the knobs, the display and the stereo path can be proved on
// the module before any effect is written. Each effect then replaces its passthrough.
//
// The host (main.cpp) owns everything the effects share: reading knobs 2-4 once a block,
// smoothing them, the tap tempo off gate in 1, the MIX, the swap fade and the DC blocker.
// An effect sees stereo in, stereo out, three controls in 0..1 and the tap.

#pragma once
#include <cstddef>
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

// The tap tempo from gate in 1: active while clocks keep arriving, and the interval in
// samples. The delays use it in place of knob 2's time.
struct Tap
{
    bool  active;
    float samples;
};

class Bank
{
  public:
    void Init(float samplerate, size_t block)
    {
        sr_      = samplerate;
        classic_.Init(samplerate, block);
        current_ = 0;
    }

    // On a change of effect, from the main loop while the output is muted (Mirth's swap
    // fade): this is where an effect clears its delay memory, which is too slow for the
    // audio callback.
    void Reset(int index)
    {
        current_ = index;
        switch(index)
        {
            case 0: classic_.Clear(); break;
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
        (void)tap;
        const float inv_n = 1.f / (float)n;
        switch(current_)
        {
            case 0:
                classic_.Block(p);
                for(size_t k = 0; k < n; k++)
                    classic_.Process(inL[k], inR[k], ((float)k + 0.5f) * inv_n, outL[k], outR[k]);
                break;
            default:  // not written yet: the plumbing's passthrough
                for(size_t k = 0; k < n; k++)
                {
                    outL[k] = inL[k];
                    outR[k] = inR[k];
                }
                break;
        }
    }

  private:
    float   sr_;
    int     current_;
    Classic classic_;
};

}  // namespace fx
