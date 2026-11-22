// gesture_check — drive MultiOsc's B7 gestures (multiosc_core/gestures.h) with libDaisy's switch.
//
// v2.0's first build read every press as a tap: daisy::Switch says RisingEdge() a millisecond before it
// says Pressed(), and the host took that millisecond for a release (David, 2026-11-22: "not doing anything
// on b7 press"). Nothing on the PC exercised the gestures then. This does, millisecond by millisecond,
// with the switch's own debounce: an 8-bit history shifted once per update, RisingEdge at 0x7f,
// FallingEdge at 0x80, Pressed at 0xff (libDaisy src/hid/switch.{h,cpp}, v9.0.0).
//
//   make check
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "multiosc_core/gestures.h"

#include <cstdio>
#include <functional>

using multiosc::Gestures;

namespace {

int failures = 0;

void Expect(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok)
        failures++;
}

// libDaisy's Switch, as its Debounce() and the edge tests read (switch.cpp / switch.h).
struct SimSwitch {
    uint8_t state   = 0;
    bool    updated = false;
    void    Update(bool contact)
    {
        updated = true;
        state   = static_cast<uint8_t>((state << 1) | (contact ? 1 : 0));
    }
    bool RisingEdge() const { return updated && state == 0x7f; }
    bool FallingEdge() const { return updated && state == 0x80; }
    bool Pressed() const { return state == 0xff; }
};

struct Result {
    int      taps = 0, nexts = 0, layer_starts = 0, layer_ends = 0;
    uint32_t next_at = 0;
    bool     shown_ever = false, layer_ever = false;
};

// Run `ms` milliseconds. contact(t): B7 closed; knob(t, p): pot p; at_next(g, t): what the host does when a
// next fires (Reset the gestures for the new engine).
Result Run(uint32_t                                    ms,
           std::function<bool(uint32_t)>               contact,
           std::function<float(uint32_t, int)>         knob,
           std::function<void(Gestures&, uint32_t)>    at_next = nullptr,
           Gestures*                                   gp      = nullptr)
{
    Gestures  local;
    Gestures& g = gp ? *gp : local;
    SimSwitch sw;
    Result    r;
    for(uint32_t t = 0; t < ms; t++)
    {
        sw.Update(contact(t));
        float k[4];
        for(int p = 0; p < 4; p++)
            k[p] = knob(t, p);
        const Gestures::Out o = g.Update(sw.RisingEdge(), sw.FallingEdge(), t, k);
        r.taps += o.tap;
        r.layer_starts += o.layer_start;
        r.layer_ends += o.layer_end;
        r.shown_ever = r.shown_ever || g.shown(t);
        r.layer_ever = r.layer_ever || g.layer();
        if(o.next)
        {
            r.nexts++;
            r.next_at = t;
            if(at_next)
                at_next(g, t);
        }
    }
    return r;
}

auto Still = [](uint32_t, int) { return 0.5f; };

} // namespace

int main()
{
    std::printf("gesture_check — MultiOsc's B7 against libDaisy's switch debounce\n\n");

    {
        SimSwitch sw;
        for(int i = 0; i < 7; i++)
            sw.Update(true);
        Expect(sw.RisingEdge() && !sw.Pressed(),
               "the switch: RisingEdge comes an update before Pressed (the cause of the first build's bug)");
    }

    auto press = [](uint32_t from, uint32_t to) { return [=](uint32_t t) { return t >= from && t < to; }; };

    {
        Result r = Run(3000, press(1000, 1150), Still);
        Expect(r.taps == 1 && r.nexts == 0 && r.layer_starts == 0, "a 150 ms press is one tap");
        Expect(!r.shown_ever, "  ...and never shows the second bank");
    }
    {
        Result r = Run(3000, press(1000, 1030), Still);
        Expect(r.taps == 1, "a 30 ms press is one tap");
    }
    {
        // the contact bounces for 5 ms, then holds
        Result r = Run(3000, [](uint32_t t) { return (t >= 1000 && t < 1005) ? (t % 2) == 0 : (t >= 1005 && t < 1200); },
                       Still);
        Expect(r.taps == 1 && r.nexts == 0, "a bouncy 200 ms press is one tap");
    }
    {
        Result r = Run(5000, press(1000, 3500), Still);
        Expect(r.nexts == 1 && r.taps == 0, "a 2.5 s hold turning nothing is one next, no tap");
        Expect(r.next_at >= 3000 && r.next_at < 3020, "  ...2 s after the press");
        Expect(r.shown_ever, "  ...and shows the second bank while held, before the switch");
    }
    {
        // hold, turn pot 3 at 1.4 s, let go at 2.5 s
        Result r = Run(4000, press(1000, 2500), [](uint32_t t, int p) { return (p == 2 && t >= 1400) ? 0.7f : 0.5f; });
        Expect(r.layer_starts == 1 && r.layer_ends == 1, "hold and turn: the second bank, and back");
        Expect(r.taps == 0 && r.nexts == 0, "  ...with no tap and no next, though held past 2 s");
    }
    {
        // a turn smaller than the threshold isn't a turn: a 2.5 s hold is still a next
        Result r = Run(5000, press(1000, 3500), [](uint32_t t, int p) { return (p == 1 && t >= 1400) ? 0.52f : 0.5f; });
        Expect(r.nexts == 1 && r.layer_starts == 0, "a knob nudged 0.02 doesn't count as a turn");
    }
    {
        // a one-millisecond contact glitch at 2.0 s into a 2.5 s hold doesn't restart it
        Result r = Run(5000, [](uint32_t t) { return t >= 1000 && t < 3500 && t != 2000; }, Still);
        Expect(r.nexts == 1 && r.taps == 0 && r.next_at < 3020, "a glitch mid-hold: still one next, on time");
    }
    {
        // the engine changes while B7 is still down (the host resets with held() true); after letting
        // go, the next press is a plain tap
        Gestures g;
        Result   r = Run(6000, [](uint32_t t) { return (t >= 1000 && t < 3500) || (t >= 4500 && t < 4600); }, Still,
                         [](Gestures& gg, uint32_t) { gg.Reset(gg.held()); }, &g);
        Expect(r.nexts == 1 && r.taps == 1, "after a change of engine, the hold is ignored and the next press taps");
    }
    {
        // the host gets round to the change only after B7 is let go: held() is then false, and the next
        // press must not be swallowed
        Gestures g;
        bool     pending = false;
        uint32_t when    = 0;
        SimSwitch sw;
        int       taps = 0, nexts = 0;
        for(uint32_t t = 0; t < 6000; t++)
        {
            sw.Update((t >= 1000 && t < 3010) || (t >= 4500 && t < 4600));
            const float           k[4] = {0.5f, 0.5f, 0.5f, 0.5f};
            const Gestures::Out o    = g.Update(sw.RisingEdge(), sw.FallingEdge(), t, k);
            taps += o.tap;
            if(o.next)
            {
                nexts++;
                pending = true;
                when    = t + 30; // the main loop late
            }
            if(pending && t == when)
            {
                g.Reset(g.held());
                pending = false;
            }
        }
        Expect(nexts == 1 && taps == 1, "a change of engine handled after the release doesn't eat the next press");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "All gesture checks passed", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
