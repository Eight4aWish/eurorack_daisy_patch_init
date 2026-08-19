// Bytebeat oscillator engine for daisy_multiosc — see bytebeat_voice.h.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).
// Vendored DSP (bytebeat_engine.*, formulas.*) is (c) 2026 Steven Collins, MIT;
// its headers are intact and unmodified.

#include "bytebeat_voice.h"

#include <cmath>

#include "daisysp.h"
#include "formulas.h"

using daisysp::fclamp;

namespace multiosc {

namespace {

// Rate spans Ogham's 1/64x..64x, six octaves either side of unity. TUNE covers
// the whole span; V/OCT adds on top at the repo's 5-octaves-per-ADC-unit scale
// (the `* 60.f` semitone convention used by SCAN and INTVL).
constexpr float kRateOctSpan = 12.0f; // TUNE, end to end
constexpr float kVoctOct     = 5.0f;  // octaves per ADC unit
constexpr float kRateMin     = 1.0f / 64.0f;
constexpr float kRateMax     = 64.0f;

// A/B interpolation grid (engine SetParamQuant): 0/1 = exact evaluation, larger
// values crossfade the formula between grid corners and tame the knobs' steps.
const int kGrids[]  = {0, 2, 4, 8, 16, 32, 64};
const int kNumGrids = sizeof(kGrids) / sizeof(kGrids[0]);

// Knob 0..1 -> an index in [0, n).
inline int KnobToIndex(float knob, int n)
{
    if(n <= 1)
        return 0;
    int i = (int)(knob * (float)n);
    if(i < 0)
        i = 0;
    if(i >= n)
        i = n - 1;
    return i;
}

} // namespace

const BytebeatVoice::Bank& BytebeatVoice::BankAt(int i)
{
    // Inside a member function, so the private nested type is in scope. The
    // boundaries are checked against the generated table, not assumed — see
    // README.md.
    static const Bank kBanks[] = {
        {"TEXT", 0, 20},   // textural
        {"NOISE", 20, 20}, // noise
        {"PERC", 40, 20},  // percussive
        {"RHYTM", 60, 20}, // rhythmic
        {"MELOD", 80, 20}, // melodic
        {"REF", 100, 1},   // A440 tuning reference
    };
    if(i < 0)
        i = 0;
    if(i >= kNumBanks)
        i = kNumBanks - 1;
    return kBanks[i];
}

void BytebeatVoice::Init(DaisyPatchSM& hw, float sample_rate)
{
    (void)hw;
    engine_.Init();

    // Keep ticks-per-second right if the host ever runs at anything but the
    // 48 kHz the vendored engine assumes.
    sr_scale_ = (sample_rate > 1.0f) ? (48000.0f / sample_rate) : 1.0f;

    bank_  = 0;
    func1_ = BankAt(0).first;
    func2_ = BankAt(0).first + 1;
    engine_.SetFormula1(func1_);
    engine_.SetFormula2(func2_);

    st_func2_.Set((float)func2_ / (float)(GetFormulaCount() - 1));
    st_grid_.Set(0.0f);
    st_drone_.Set(0.0f);
}

const char* BytebeatVoice::ModLabel(int idx, bool edit) const
{
    if(edit)
    {
        switch(idx)
        {
            case 0: return "Func2";
            case 1: return "Grid";
            case 2: return "Drone";
            default: return "-";
        }
    }
    switch(idx)
    {
        case 0: return "A";
        case 1: return "B";
        case 2: return "Func";
        default: return "-";
    }
}

const char* BytebeatVoice::Selection() const
{
    // The formula's own name, which is more use while scrubbing MOD3 than the
    // bank name and still changes on a short press. These are string literals in
    // the generated table, so the pointer is stable — which the host's redraw
    // check relies on (it compares Selection() by pointer, not by content).
    const FormulaInfo* f = GetFormulaAt(func1_);
    return (f && f->name) ? f->name : "?";
}

void BytebeatVoice::Process(DaisyPatchSM&             hw,
                            const EngineContext&      ctx,
                            AudioHandle::InputBuffer  in,
                            AudioHandle::OutputBuffer out,
                            size_t                    size)
{
    (void)in;

    // Short press cycles the bank. func1_ is clamped into the new family so the
    // screen and voice 2 are coherent straight away; on the Play page MOD 3
    // re-asserts the slot on the very next block.
    if(ctx.short_press)
    {
        const int off = func1_ - BankAt(bank_).first;
        bank_         = (bank_ + 1) % kNumBanks;
        const Bank& b = BankAt(bank_);
        func1_        = b.first + (off < b.count ? off : b.count - 1);
        engine_.SetFormula1(func1_);
    }

    // Entering Edit: the three MOD knobs change meaning, so hold their new
    // parameters until each knob sweeps through the stored value.
    if(ctx.page_changed && ctx.edit_page)
    {
        st_func2_.Reset((float)func2_ / (float)(GetFormulaCount() - 1));
        st_grid_.Reset(st_grid_.value());
        st_drone_.Reset(st_drone_.value());
    }

    if(ctx.edit_page)
    {
        // Voice 2 picks from the whole bank, not just the current family — two
        // voices from different families is most of the point of having two.
        const float f2n = st_func2_.Process(hw.GetAdcValue(KNOB_MOD1));
        const int   f2  = KnobToIndex(f2n, GetFormulaCount());
        if(f2 != func2_)
        {
            func2_ = f2;
            engine_.SetFormula2(func2_);
        }

        const float gn = st_grid_.Process(hw.GetAdcValue(KNOB_MOD2));
        engine_.SetParamQuant(kGrids[KnobToIndex(gn, kNumGrids)]);

        const float dn = st_drone_.Process(hw.GetAdcValue(KNOB_MOD3));
        engine_.SetOut2Decoupled(dn >= 0.5f); // idempotent; only the edge freezes
    }
    else
    {
        // Rate: TUNE sweeps 1/64x..64x, V/OCT transposes on top. Advancing `t`
        // faster raises every partial together, so this is varispeed and tracks
        // 1 V/oct on the periodic formulas.
        const float oct = (hw.GetAdcValue(KNOB_TUNE) - 0.5f) * kRateOctSpan
                          + hw.GetAdcValue(CV_VOCT) * kVoctOct;
        float rate = powf(2.0f, oct);
        rate       = fclamp(rate, kRateMin, kRateMax);
        engine_.SetRate(rate * sr_scale_);

        // A and B, knob summed with its matching CV jack.
        const float a
            = fclamp(hw.GetAdcValue(KNOB_MOD1) + hw.GetAdcValue(CV_MOD1), 0.f, 1.f);
        const float b
            = fclamp(hw.GetAdcValue(KNOB_MOD2) + hw.GetAdcValue(CV_MOD2), 0.f, 1.f);
        engine_.SetParamA((int32_t)(a * 255.0f + 0.5f));
        engine_.SetParamB((int32_t)(b * 255.0f + 0.5f));

        // Formula within the current bank.
        const Bank& bk = BankAt(bank_);
        const int   f1 = bk.first + KnobToIndex(hw.GetAdcValue(KNOB_MOD3), bk.count);
        if(f1 != func1_)
        {
            func1_ = f1;
            engine_.SetFormula1(func1_); // restarts t, so one-shots fire on select
        }
    }

    // TRIG = hard sync. Polled per block, so the restart lands within one block
    // (~1 ms at 48 kHz / 48 samples) rather than on the sample, as it does on
    // Ogham's own hardware where the gate raises an EXTI interrupt.
    if(hw.gate_in_1.Trig())
        engine_.SyncReset();

    for(size_t n = 0; n < size; n++)
    {
        float o1 = 0.f, o2 = 0.f;
        engine_.Process(o1, o2);

        // A formula parked on a constant value is a DC level at the jack, not
        // silence — Ogham AC-couples in hardware, so block it here instead.
        const float d1 = o1 - dc_x1_[0] + 0.999f * dc_y1_[0];
        dc_x1_[0]      = o1;
        dc_y1_[0]      = d1;
        const float d2 = o2 - dc_x1_[1] + 0.999f * dc_y1_[1];
        dc_x1_[1]      = o2;
        dc_y1_[1]      = d2;

        out[0][n] = d1;
        out[1][n] = d2;
    }
}

} // namespace multiosc
