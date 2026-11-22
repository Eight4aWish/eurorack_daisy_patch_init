// gestures.h — B7's gestures for MultiOsc, decoded from the switch's debounced edges.
//
// Pure (no hardware), so tools/gesture_check drives it with libDaisy's switch behaviour on a PC.
//
// Why from the edges: daisy::Switch reports RisingEdge() when its 8-bit history reads 0x7f, but
// Pressed() only at 0xff, one update (a millisecond) later; and it reports FallingEdge() at 0x80, seven
// updates after the contact opens. v2.0's first build tracked the press with Pressed(), so in the very
// update that saw the rising edge the button read as not held, and every press was taken as a tap at
// once: holds never registered and B7 did nothing in three engines (David, 2026-11-22). Here the button
// counts as down from its rising edge to its falling edge.
//
//   tap                     released before anything else happened: the engine's
//   hold and turn a knob    the second bank, from the turn until the release
//   hold, turning nothing   kNextMs in: the next engine
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include <cstdint>

namespace multiosc {

class Gestures {
  public:
    static constexpr float    kTurnThresh  = 0.04f; // how far a knob moves to count as turned
    static constexpr uint32_t kNextMs      = 2000;  // hold without turning, as Wish's SETTINGS
    static constexpr uint32_t kShowLayerMs = 300;   // a held B7 shows the second bank on screen

    struct Out {
        bool tap         = false; // one-shot
        bool next        = false; // one-shot: change to the next engine
        bool layer_start = false; // one-shot: the second bank has just taken the knobs
        bool layer_end   = false; // one-shot: B7 let go after the second bank
    };

    // An engine starting. `held`: B7 is still down (from the hold that changed engine), so this press is
    // done with: it is ignored until it lets go.
    void Reset(bool held)
    {
        ignore_  = held;
        down_    = held;
        pressed_ = turned_ = switched_ = false;
    }

    // Once per switch update. rose / fell: daisy::Switch::RisingEdge() / FallingEdge(). now: ms.
    Out Update(bool rose, bool fell, uint32_t now, const float* knobs)
    {
        Out o;
        if(ignore_)
        {
            if(fell)
                ignore_ = down_ = false;
            return o;
        }
        // A rising edge while already down is a contact glitch mid-hold (one bounce shifts round to 0x7f
        // again): it doesn't start a new press.
        if(rose && !down_)
        {
            down_ = pressed_ = true;
            turned_ = switched_ = false;
            t0_                 = now;
            for(int p = 0; p < 4; p++)
                k0_[p] = knobs[p];
        }
        if(fell)
            down_ = false;

        if(pressed_ && down_)
        {
            if(!turned_ && !switched_)
            {
                for(int p = 0; p < 4; p++)
                {
                    const float d = knobs[p] - k0_[p];
                    if(d > kTurnThresh || -d > kTurnThresh)
                        turned_ = true;
                }
                if(turned_)
                    o.layer_start = true;
                else if(now - t0_ >= kNextMs)
                {
                    switched_ = true;
                    o.next    = true;
                }
            }
        }
        else if(pressed_ && !down_)
        {
            if(turned_)
                o.layer_end = true;
            else if(!switched_)
                o.tap = true;
            pressed_ = turned_ = false;
        }
        return o;
    }

    bool held() const { return down_; }                // B7 is down, as far as the edges say
    bool layer() const { return pressed_ && turned_; } // the second bank has the knobs
    bool pressed() const { return pressed_; }          // a press is in progress: the main page holds
    bool shown(uint32_t now) const                     // show the second bank on screen
    {
        return pressed_ && !switched_ && (turned_ || now - t0_ >= kShowLayerMs);
    }

  private:
    bool     ignore_   = false;
    bool     down_     = false;
    bool     pressed_  = false;
    bool     turned_   = false;
    bool     switched_ = false;
    uint32_t t0_       = 0;
    float    k0_[4]    = {0, 0, 0, 0};
};

} // namespace multiosc
