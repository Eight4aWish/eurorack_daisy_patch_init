// multiosc_core — shared engine interface
//
// One host (host.h) drives several voice engines over one panel, the contract agreed with David on
// 2026-11-22 (docs/PANEL.md). The host owns the hardware, the OLED, B7, the envelope and the volume, and
// hands the active engine its controls already read, so an engine touches no hardware:
//
//   pot 1        the engine's selector. Knob only: the jack below it is V/OCT.
//   pots 2-4     each knob plus the MOD 1-3 jack below it.
//   hold + turn  a second bank: pot 1 Volume, pot 2 free, pot 3 Attack, pot 4 Decay (the host's).
//   tap          the engine's, if it has a use for one.
//   V/OCT        C3 at 0 V, through the module's calibration (common/unit_cal.h).
//   GATE IN 1    the envelope: a drone until the first gate, then gated (Joy's AdEnvelope).
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include <cstddef>
#include <cstdint>
#include "daisy_patch_sm.h"

namespace multiosc {

using daisy::AudioHandle;

// The patch.Init's ADC channels as this panel uses them.
enum AdcChan {
    POT1    = 0, // CV_1
    POT2    = 1, // CV_2
    POT3    = 2, // CV_3
    POT4    = 3, // CV_4
    CV_VOCT = 4, // CV_5, under pot 1
    CV_POT2 = 5, // CV_6, the MOD 1 jack
    CV_POT3 = 6, // CV_7, the MOD 2 jack
    CV_POT4 = 7, // CV_8, the MOD 3 jack
};

constexpr float kBaseNote = 48.0f; // C3 at 0 V, as Joy (David, 2026-11-22)

// What the host hands an engine each block.
struct Controls {
    float pot[4];  // 0..1. pot[0] is the knob alone; pot[1..3] the knob plus its jack, clamped
    float decay;   // hold + pot 4, 0..1: the envelope's decay, and SCAN's damping
    float note;    // MIDI note: kBaseNote at 0 V plus V/OCT
    bool  gate;    // GATE IN 1, high or an edge this block
    bool  gated;   // a gate has arrived since this engine started: the drone has ended
    bool  tap;     // one-shot: B7 tapped this block
};

class Engine {
  public:
    virtual ~Engine() {}

    // Before audio starts, each time the engine is chosen: it resets its own state.
    virtual void Init(float sample_rate) = 0;

    // Audio rate, in the callback. Writes both outputs at the engine's own level, peaking near 1; the host
    // then applies the envelope (if UsesEnvelope) and the volume.
    virtual void Process(const Controls& c, AudioHandle::OutputBuffer out, size_t size) = 0;

    // --- the screen. Every string a stable pointer: the redraw check compares pointers.
    virtual const char* Name() const = 0;           // up to five characters (the large splash)
    virtual const char* Choice() const = 0;         // pot 1's current choice, up to five characters
    virtual const char* PotLabel(int pot) const = 0; // pots 1-3 as indices 1..3, up to four characters
    virtual const char* DecayLabel() const { return "Dec"; }
    virtual const char* TapSelection() const { return nullptr; } // nullptr: the tap does nothing

    // The tap's state, kept with the engine's settings across power cycles.
    virtual uint8_t TapState() const { return 0; }
    virtual void    SetTapState(uint8_t) {}

    // Whether the host's attack/decay envelope shapes the output. SCAN's decay is its own damping.
    virtual bool  UsesEnvelope() const { return true; }
    virtual float DefaultDecay() const { return 0.25f; } // 0..1, before the first time it is set

    virtual int BlockSize() const { return 48; }
};

// A stepped choice from a pot (pot 1's selectors, FM4OP's ratio sets), with hysteresis so a knob resting on
// a boundary doesn't flicker between two.
class Stepper {
  public:
    explicit Stepper(int steps) : steps_(steps) {}
    int Process(float x)
    {
        const float pos = x * steps_;
        if(pos < index_ - kHyst || pos > index_ + 1 + kHyst)
        {
            int i = static_cast<int>(pos);
            index_ = i < 0 ? 0 : i >= steps_ ? steps_ - 1 : i;
        }
        return index_;
    }
    int index() const { return index_; }

  private:
    static constexpr float kHyst = 0.2f; // a fifth of a step either side
    int                    steps_;
    int                    index_ = 0;
};

// Soft-takeover ("catch"): after Reset() a knob doesn't move its parameter until it sweeps through the
// value the parameter holds, so changing layer never makes anything jump (as Wish).
class SoftTakeover {
  public:
    void Reset(float param_value)
    {
        value_  = param_value;
        caught_ = false;
    }
    void Set(float v)
    {
        value_  = v;
        caught_ = true;
    }
    float Process(float knob)
    {
        if(!caught_ && (knob - value_ <= kThresh && value_ - knob <= kThresh))
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
