// multiosc_core — shared engine interface
//
// One boot-selectable host (see host.h) drives several voice "engines" over the
// universal panel defined in docs/PANEL.md. Each engine implements this
// interface; the host owns the hardware, OLED, button gestures and Play/Edit
// page state, and dispatches audio to the active engine.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include <cmath>
#include "daisy_patch_sm.h"

namespace multiosc {

using daisy::AudioHandle;
using daisy::patch_sm::DaisyPatchSM;

// Universal control map (ADC indices). See docs/PANEL.md.
//   Knobs : TUNE + MOD1..3        CV jacks : V/OCT + MOD1..3 CV
enum AdcChan {
    KNOB_TUNE = 0, // CV_1
    KNOB_MOD1 = 1, // CV_2
    KNOB_MOD2 = 2, // CV_3
    KNOB_MOD3 = 3, // CV_4
    CV_VOCT   = 4, // CV_5
    CV_MOD1   = 5, // CV_6
    CV_MOD2   = 6, // CV_7
    CV_MOD3   = 7, // CV_8
};

// Per-block state the host hands to the active engine.
struct EngineContext {
    bool edit_page   = false; // false = Play page, true = Edit page
    bool short_press = false; // one-shot: B7 short press fired this block
    bool page_changed = false; // one-shot: Play<->Edit toggled this block
};

// A selectable voice. Engines read the universal controls from `hw` themselves
// (hw.GetAdcValue(KNOB_*/CV_*), hw.gate_in_1, audio in), so they keep their own
// native scaling. The host only supplies page/gesture context and the labels
// shown on screen.
class Engine {
  public:
    virtual ~Engine() {}

    // Called once before audio starts, after the host picks this engine.
    virtual void Init(DaisyPatchSM& hw, float sample_rate) = 0;

    // Audio-rate render. Runs in the audio callback.
    virtual void Process(DaisyPatchSM&             hw,
                         const EngineContext&      ctx,
                         AudioHandle::InputBuffer  in,
                         AudioHandle::OutputBuffer out,
                         size_t                    size) = 0;

    // --- Screen legend (docs/PANEL.md control reminder) ---
    virtual const char* Name() const = 0;
    // Label for MOD1..3 (idx 0..2) on the given page.
    virtual const char* ModLabel(int idx, bool edit) const = 0;
    // What a short press does, and the current selection it cycles.
    virtual const char* ShortLabel() const = 0;   // e.g. "ALGO", "MODEL"
    virtual const char* Selection() const = 0;     // e.g. "Parallel"

    // Native audio block size for this engine.
    virtual int BlockSize() const { return 48; }
};

// Soft-takeover for a knob that changes meaning between pages: after Reset() the
// knob is "uncaught" and the parameter holds until the physical knob sweeps
// through the stored value, avoiding jumps. See docs/PANEL.md.
class SoftTakeover {
  public:
    void Reset(float param_value)
    {
        value_  = param_value;
        caught_ = false;
    }
    // Force the parameter to a value and mark it already "caught" (live).
    void Set(float v)
    {
        value_  = v;
        caught_ = true;
    }
    float Process(float knob)
    {
        if(!caught_ && std::fabs(knob - value_) <= kThresh)
            caught_ = true;
        if(caught_)
            value_ = knob;
        return value_;
    }
    float value() const { return value_; }

  private:
    static constexpr float kThresh = 0.02f;
    float                  value_  = 0.5f;
    bool                   caught_ = true;
};

} // namespace multiosc
