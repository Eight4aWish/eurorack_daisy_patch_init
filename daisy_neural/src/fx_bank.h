// FX — Mirth's fourth bank: the conventional effects that were Daisy MultiFX.
//
// David, 2026-11-09: MultiFX retires and its effects become a bonus bank on Mirth,
// trimmed from MultiFX's four banks of four to one list of eleven. Dropped, because
// Mirth or the rack already covers them: Reverb CLASSIC (PLATE is the same reverb
// with a predelay), the LADDER, SVF morph and DUAL LP filters (the FILTERS bank and
// the rack's filters), and DRIVE (the AMPS bank). Putting one back is a line in kDefs.
//
// The DSP is unchanged: common/multifx_core, the same banks MultiFX ran. This file
// only flattens them into one list and holds their state. Unlike the neural banks the
// effects are stereo: IN_L and IN_R in (IN_R is normalled to IN_L, so a mono source
// feeds both), OUT_L and OUT_R out.
//
// Knobs in this bank: 1 MIX as in every bank; 2, 3 and 4 the effect's three controls
// (labels in kDefs, shown on the RUN page). DRIVE has no meaning here, so knob 4 is the
// third control. Gate In 1 clocks the delay time of PING and ECHOVERB, as on MultiFX.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisy_patch_sm.h"
#include "multifx_core/reverb_bank.h"
#include "multifx_core/delay_bank.h"
#include "multifx_core/tone_bank.h"
#include "multifx_core/misc_bank.h"
#include "multifx_core/voicing.h"

namespace fx {

enum class Kind : uint8_t
{
    Reverb,
    Delay,
    Tone,
    Misc,
};

struct Def
{
    const char* name;    // the RUN page's name line and the menu, up to ten characters
    Kind        kind;    // which multifx_core bank runs it
    int         mode;    // that bank's mode
    const char* lbl[3];  // knobs 2, 3 and 4, three characters each
};

inline constexpr Def kDefs[] = {
    {"PLATE",    Kind::Reverb, (int)mfx::ReverbMode::Plate,     {"Pre", "Ton", "Siz"}},
    {"TANK",     Kind::Reverb, (int)mfx::ReverbMode::Tank,      {"Pre", "Dmp", "Siz"}},
    {"SHIMMER",  Kind::Reverb, (int)mfx::ReverbMode::Shimmer,   {"Fbk", "Ton", "Shm"}},
    {"PING",     Kind::Delay,  (int)mfx::DelayMode::Ping,       {"Tim", "Fbk", "Dmp"}},
    {"TAPE",     Kind::Delay,  (int)mfx::DelayMode::Tape,       {"Tim", "Fbk", "Wow"}},
    {"MULTITAP", Kind::Delay,  (int)mfx::DelayMode::MultiTap,   {"Tim", "Pan", "Fbk"}},
    {"ECHOVERB", Kind::Delay,  (int)mfx::DelayMode::EchoVerb,   {"Tim", "Fbk", "Rvb"}},
    {"COMB",     Kind::Tone,   (int)mfx::ToneMode::Comb,        {"Frq", "Fbk", "Brt"}},
    {"RESONATR", Kind::Misc,   (int)mfx::MiscMode::Resonator,   {"Frq", "Dmp", "InL"}},
    {"PITCH",    Kind::Misc,   (int)mfx::MiscMode::Pitch,       {"Sem", "Siz", "Fun"}},
    {"CRUSH",    Kind::Misc,   (int)mfx::MiscMode::Crush,       {"Bit", "Rat", "Ton"}},
};
inline constexpr int kCount = (int)(sizeof(kDefs) / sizeof(kDefs[0]));

// Clock on Gate In 1, for the delays: the interval between rising edges, trusted for 2 s
// after the last one, as on MultiFX. Once per audio block.
class Clock
{
  public:
    void Process(bool gate, uint32_t now_ms)
    {
        if(gate && !gate_)
        {
            if(last_ != 0 && now_ms != last_)
                interval_s_ = (float)(now_ms - last_) * 0.001f;
            last_ = now_ms;
        }
        gate_   = gate;
        active_ = interval_s_ > 0.f && now_ms - last_ < 2000u;
    }
    bool  Active() const { return active_; }
    float Samples(float sr) const { return active_ ? interval_s_ * sr : 0.f; }

  private:
    bool     gate_       = false;
    bool     active_     = false;
    uint32_t last_       = 0;
    float    interval_s_ = 0.f;
};

// The four multifx_core banks behind one index. The banks themselves are placed by the
// caller (big ones in SDRAM; see main.cpp), so this holds pointers.
class Bank
{
  public:
    void Init(mfx::ReverbBank* r, mfx::DelayBank* d, mfx::ToneBank* t, mfx::MiscBank* m,
              float sr)
    {
        rev_ = r;
        del_ = d;
        ton_ = t;
        mis_ = m;
        rev_->Init(sr);
        del_->Init(sr);
        ton_->Init(sr);
        mis_->Init(sr);
        out_.Init(sr, 0.f);  // DC block and soft clamp; no low-pass, as on MultiFX
    }

    // Clears the effect's buffers (the pitch shifter's alone is ~128 kB). Main loop
    // only, with the output muted.
    void Reset(int i)
    {
        const Def& d = kDefs[i];
        switch(d.kind)
        {
            case Kind::Reverb: rev_->Reset((mfx::ReverbMode)d.mode); break;
            case Kind::Delay: del_->Reset((mfx::DelayMode)d.mode); break;
            case Kind::Tone: ton_->Reset((mfx::ToneMode)d.mode); break;
            case Kind::Misc: mis_->Reset((mfx::MiscMode)d.mode); break;
        }
    }

    // One stereo sample of fully wet effect; p1..p3 are 0..1.
    inline void Process(int i, float in_l, float in_r, float p1, float p2, float p3,
                        const Clock& clk, float sr, float& wl, float& wr)
    {
        const Def& d = kDefs[i];
        switch(d.kind)
        {
            case Kind::Reverb:
                rev_->Process((mfx::ReverbMode)d.mode, in_l, in_r, p1, p2, p3, wl, wr);
                break;
            case Kind::Delay:
                del_->Process((mfx::DelayMode)d.mode, in_l, in_r, p1, p2, p3,
                              clk.Active(), clk.Samples(sr), wl, wr);
                break;
            case Kind::Tone:
                ton_->Process((mfx::ToneMode)d.mode, in_l, in_r, p1, p2, p3, wl, wr);
                break;
            case Kind::Misc:
                mis_->Process((mfx::MiscMode)d.mode, in_l, in_r, p1, p2, p3, wl, wr);
                break;
        }
    }

    // The mixed output, DC-blocked and soft-clamped, in place.
    inline void Voice(float& l, float& r) { out_.Process(l, r); }

  private:
    mfx::ReverbBank* rev_ = nullptr;
    mfx::DelayBank*  del_ = nullptr;
    mfx::ToneBank*   ton_ = nullptr;
    mfx::MiscBank*   mis_ = nullptr;
    mfx::OutputStage out_;
};

} // namespace fx
