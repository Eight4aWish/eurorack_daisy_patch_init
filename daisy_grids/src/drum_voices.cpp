// Implementation of Sorrow's drum voice pool — see drum_voices.h.
//
// License: GPL-3.0-or-later — see daisy_grids/LICENSE.
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 David Baghurst

#include "drum_voices.h"

#include "analog_snare_fast.h"
#include "hihat_fast.h"
#include "modal_voice_fast.h"
#include "daisysp.h"

#include <cmath>

using namespace daisysp;

namespace sorrow
{
namespace
{

// Model families. Rolling at low wildness keeps all three slots in one family
// so the kit hangs together; wildness is what lets them disagree.
// Named for the synthesis method, and in the order wildness unlocks them.
// "Acoustic" was the old name for the third one and it read as the tamest of the
// three when it is in fact the strangest - a plucked string standing in for a
// snare is not an acoustic drum, it is physical modelling being pushed.
enum class Family : uint8_t
{
    Synth = 0,  // Mutable's "synthetic" drum models - the safe defaults
    Analog,     // the 808-lineage circuit emulations
    Physical,   // struck and plucked physical models - modal, string
    kCount
};

// -----------------------------------------------------------------------------
// Random helpers
// -----------------------------------------------------------------------------

static inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Draw from a range that widens with wildness: [lo0,hi0] at w=0 opening out to
// [lo1,hi1] at w=1. Every model parameter is expressed this way, so "tame" and
// "unhinged" are tuned per parameter rather than by one global scale factor.
static inline float RndW(RandFn r, float w, float lo0, float hi0, float lo1, float hi1)
{
    const float lo = Lerp(lo0, lo1, w);
    const float hi = Lerp(hi0, hi1, w);
    return lo + r() * (hi - lo);
}

// -----------------------------------------------------------------------------
// Voice interface
// -----------------------------------------------------------------------------

class Voice
{
  public:
    virtual void  Init(float sample_rate)             = 0;
    virtual void  Trig(float accent)                  = 0;
    virtual float Process()                           = 0;
    virtual void  Randomize(float wildness, RandFn r) = 0;

    // Output trim, measured at boot so every model arrives at the mix at a
    // comparable level. Without it a roll could drop a voice that is simply
    // quieter by nature to the back of the mix.
    virtual void SetOutputGain(float g) = 0;
};

// Every DaisySP model used here shares the same contract — Init(sr), Trig(),
// SetAccent(), Process(bool) — so only Randomize() differs between them.
template <typename Model>
class ModelVoice : public Voice
{
  public:
    void Init(float sample_rate) override
    {
        model_.Init(sample_rate);
        model_.SetSustain(false);
        sample_rate_ = sample_rate;
        env_         = 0.0f;
        env_coef_    = 0.0f;
    }
    void Trig(float accent) override
    {
        model_.SetAccent(accent);
        model_.Trig();
        env_ = 1.0f;
    }
    float Process() override
    {
        const float v = model_.Process(false);
        if(env_coef_ <= 0.0f)
            return v * gain_;
        const float out = v * env_;
        env_ *= env_coef_;
        return out * gain_;
    }

    void SetOutputGain(float g) override { gain_ = g; }

  protected:
    // Optional output VCA, off unless a model asks for it. DaisySP's
    // AnalogSnareDrum needs one: its resonator bank is scaled such that the
    // model rings for seconds at *every* decay setting, including zero, so its
    // own decay control cannot make it stop. Gating the output keeps the 808
    // timbre while giving the decay parameter something real to do.
    // Argument is the time to fall 60 dB.
    void SetEnvDecay(float seconds)
    {
        if(seconds <= 0.0f)
        {
            env_coef_ = 0.0f;
            return;
        }
        env_coef_ = expf(-6.907755f / (seconds * sample_rate_));
    }

    Model model_;
    float gain_        = 1.0f;
    float sample_rate_ = 48000.0f;
    float env_         = 0.0f;
    float env_coef_    = 0.0f;  // 0 disables the VCA entirely
};

// -----------------------------------------------------------------------------
// Kick models
// -----------------------------------------------------------------------------

class SynthKick : public ModelVoice<SyntheticBassDrum>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        model_.SetFreq(RndW(r, w, 45.f, 70.f, 30.f, 150.f));
        model_.SetDecay(RndW(r, w, 0.25f, 0.45f, 0.05f, 0.62f));
        model_.SetTone(RndW(r, w, 0.15f, 0.45f, 0.00f, 0.90f));
        // v1 capped dirtiness at 0.40 and defaulted to 0.03, which is most of
        // why the kit sounded safe. At full wildness it now reaches the top.
        model_.SetDirtiness(RndW(r, w, 0.00f, 0.15f, 0.00f, 0.95f));
        model_.SetFmEnvelopeAmount(RndW(r, w, 0.05f, 0.25f, 0.00f, 1.00f));
        model_.SetFmEnvelopeDecay(RndW(r, w, 0.05f, 0.25f, 0.00f, 0.90f));
    }
};

class AnalogKick : public ModelVoice<AnalogBassDrum>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        model_.SetFreq(RndW(r, w, 45.f, 65.f, 30.f, 120.f));
        model_.SetTone(RndW(r, w, 0.30f, 0.70f, 0.05f, 1.00f));
        model_.SetDecay(RndW(r, w, 0.35f, 0.55f, 0.10f, 0.60f));
        model_.SetAttackFmAmount(RndW(r, w, 0.00f, 0.30f, 0.00f, 1.00f));
        model_.SetSelfFmAmount(RndW(r, w, 0.00f, 0.20f, 0.00f, 0.90f));
        SetEnvDecay(RndW(r, w, 0.16f, 0.40f, 0.08f, 0.70f));
    }
};

class ModalKick : public ModelVoice<ModalVoiceFast<12>>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        // ModalVoice's "damping" runs the opposite way to the name: higher
        // values ring *longer*. Measured at 220 Hz: 0.02 gives 59 ms, 0.12
        // gives 118 ms, 0.32 gives 457 ms, and 0.45 upward sustains for a
        // second or more. The ranges here used to sit at 0.25-0.60, so the
        // voice was a sustained pitched tone that only stopped because the VCA
        // cut it - which is exactly why it read as a pipe rather than a drum.
        // Structure matters too: low is less tonal (autocorrelation 0.56 at
        // 0.0 against 0.74 at 0.8), so the ranges lean low.
        model_.SetFreq(RndW(r, w, 50.f, 90.f, 35.f, 170.f));
        model_.SetStructure(RndW(r, w, 0.10f, 0.35f, 0.00f, 0.70f));
        model_.SetBrightness(RndW(r, w, 0.20f, 0.50f, 0.05f, 0.95f));
        model_.SetDamping(RndW(r, w, 0.08f, 0.22f, 0.03f, 0.55f));
        // Tame is percussive; wild reaches back up into the sustained,
        // pitched region deliberately - that ringing "pipe" is a good sound, it
        // just should not be the only thing a modal voice can be. The VCA is a
        // backstop now rather than the thing that stops the note.
        SetEnvDecay(RndW(r, w, 0.20f, 0.45f, 0.10f, 1.10f));
    }
};

// -----------------------------------------------------------------------------
// Snare models
// -----------------------------------------------------------------------------

class SynthSnare : public ModelVoice<SyntheticSnareDrum>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        model_.SetFreq(RndW(r, w, 150.f, 220.f, 90.f, 400.f));
        model_.SetDecay(RndW(r, w, 0.05f, 0.12f, 0.02f, 0.35f));
        model_.SetSnappy(RndW(r, w, 0.50f, 0.85f, 0.10f, 1.00f));
        model_.SetFmAmount(RndW(r, w, 0.00f, 0.20f, 0.00f, 0.90f));
    }
};

class AnalogSnare : public ModelVoice<AnalogSnareFast>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        model_.SetFreq(RndW(r, w, 160.f, 220.f, 100.f, 380.f));
        model_.SetTone(RndW(r, w, 0.30f, 0.70f, 0.00f, 1.00f));
        model_.SetDecay(RndW(r, w, 0.20f, 0.45f, 0.05f, 0.90f));
        model_.SetSnappy(RndW(r, w, 0.40f, 0.80f, 0.05f, 1.00f));
        // The model still never stops on its own; the VCA is what makes it a
        // snare rather than a drone. Unchanged by the fork.
        SetEnvDecay(RndW(r, w, 0.09f, 0.20f, 0.04f, 0.55f));
    }
};

class ModalSnare : public ModelVoice<ModalVoiceFast<12>>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        model_.SetFreq(RndW(r, w, 180.f, 300.f, 120.f, 600.f));
        model_.SetStructure(RndW(r, w, 0.15f, 0.45f, 0.00f, 0.80f));
        model_.SetBrightness(RndW(r, w, 0.35f, 0.65f, 0.05f, 1.00f));
        model_.SetDamping(RndW(r, w, 0.05f, 0.18f, 0.02f, 0.50f));
        SetEnvDecay(RndW(r, w, 0.15f, 0.35f, 0.08f, 0.95f));
    }
};

class StringSnare : public ModelVoice<StringVoice>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        model_.SetFreq(RndW(r, w, 200.f, 350.f, 120.f, 800.f));
        model_.SetStructure(RndW(r, w, 0.20f, 0.50f, 0.00f, 0.95f));
        model_.SetBrightness(RndW(r, w, 0.35f, 0.65f, 0.05f, 1.00f));
        model_.SetDamping(RndW(r, w, 0.30f, 0.60f, 0.30f, 0.95f));
        SetEnvDecay(RndW(r, w, 0.09f, 0.25f, 0.05f, 0.60f));
    }
};

// -----------------------------------------------------------------------------
// Hat models
//
// HiHat is a template: the noise source and VCA are compile-time choices, so
// one class yields several genuinely different hats. SquareNoise is the 808's
// six-square-oscillator cluster; RingModNoise is the ring-modulated variant.
// -----------------------------------------------------------------------------

template <typename Hat>
class HatVoice : public ModelVoice<Hat>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        // Full range again at 48 kHz: the metallic cluster can reach the top of
        // the band without folding back. At 32 kHz this has to come down to
        // about 11 kHz to stay under a 16 kHz Nyquist, which costs the hats
        // their top octave.
        this->model_.SetFreq(RndW(r, w, 6000.f, 10000.f, 2500.f, 16000.f));
        // HiHat::SetDecay maps its argument through decay*1.7-1.2, so anything
        // below about 0.71 goes negative and the envelope collapses in a few
        // milliseconds. Measured: 0.20 gives a 6 ms tick, 0.50 gives 17 ms -
        // the previous range lived entirely in click territory, which is why
        // every hat sounded the same and abrupt. The argument is not clamped at
        // 1.0 either, and above it is where open hats live:
        //
        //   0.65 ->  32 ms   0.90 ->  96 ms   1.10 -> 307 ms
        //   0.80 ->  63 ms   1.00 -> 172 ms   1.20 -> 590 ms
        //
        // Tame is closed-hat variety; wild reaches from a tick to an open hat.
        this->model_.SetDecay(RndW(r, w, 0.70f, 0.95f, 0.60f, 1.15f));
        // Capped at 0.92, not 1.0: measured on the host, both HiHat variants go
        // non-finite at tone >= 0.98 - the filter loses stability up there - and
        // a NaN reaching the mix takes the whole output with it. Predates the
        // pool; it was simply never rolled until the model list changed.
        this->model_.SetTone(RndW(r, w, 0.50f, 0.85f, 0.10f, 0.92f));
        this->model_.SetNoisiness(RndW(r, w, 0.80f, 1.00f, 0.20f, 1.00f));
    }
};

class ModalHat : public ModelVoice<ModalVoiceFast<12>>
{
  public:
    void Randomize(float w, RandFn r) override
    {
        model_.SetFreq(RndW(r, w, 2200.f, 5000.f, 900.f, 9000.f));
        model_.SetStructure(RndW(r, w, 0.20f, 0.50f, 0.00f, 0.85f));
        model_.SetBrightness(RndW(r, w, 0.45f, 0.75f, 0.10f, 1.00f));
        model_.SetDamping(RndW(r, w, 0.03f, 0.14f, 0.02f, 0.45f));
        SetEnvDecay(RndW(r, w, 0.10f, 0.25f, 0.05f, 0.85f));
    }
};

// -----------------------------------------------------------------------------
// The pool
// -----------------------------------------------------------------------------

struct Entry
{
    Voice*      voice;
    Family      family;
    float       exotic;  // 0 = safe default, 1 = only reachable at full wildness
    const char* name;
};

// Measured at boot: what fraction of one sample period each model costs.
// Indexed [slot][model].
float s_cost[3][8] = {};

// Measured output trim per model, so a naturally quiet model isn't buried.
float s_gain[3][8] = {};

// Most the three voices together may cost, as a fraction of one sample period.
// The rest of the period pays for the sequencer, panel, clock and mix. If the
// callback ever runs long it starves SysTick, which silently freezes both
// switches while audio carries on - so this is a hard ceiling, not a target.
//
// Budgeting the kit as a whole rather than per slot matters: an expensive model
// is perfectly affordable next to two cheap ones, and a per-slot cap cannot say
// so. The first attempt capped each slot at 18% and squeezed two of the three
// slots down to a single model.
// Measured on hardware at 32 kHz: a 53%-of-budget kit reported 56% total audio
// load, so everything outside the three voices costs only about 3%. The most
// expensive possible kit is 75%, which would land near 78% total - survivable,
// but with no margin for the Time page's trigger scheduler or anything added
// later. The pool is 3 kicks x 4 snares x 3 hats, so 36 possible kits; 72%
// admits all but the most expensive combinations.
constexpr float kKitCostBudget = 0.72f;

// Model instances. Static storage, no allocation: selection is just an index.
SynthKick   s_kick_synth;
AnalogKick  s_kick_analog;
ModalKick   s_kick_modal;

SynthSnare  s_snare_synth;
AnalogSnare s_snare_analog;
ModalSnare  s_snare_modal;
StringSnare s_snare_string;

HatVoice<HiHatFast<SquareNoise, LinearVCA>>  s_hat_square;
HatVoice<HiHatFast<RingModNoise, SwingVCA>>  s_hat_ringmod;
ModalHat                                 s_hat_modal;

const Entry kKickModels[] = {
    {&s_kick_synth, Family::Synth, 0.00f, "synth BD"},
    {&s_kick_analog, Family::Analog, 0.10f, "analog BD"},
    {&s_kick_modal, Family::Physical, 0.50f, "modal BD"},
};

const Entry kSnareModels[] = {
    {&s_snare_synth, Family::Synth, 0.00f, "synth SD"},
    {&s_snare_analog, Family::Analog, 0.10f, "analog SD"},
    {&s_snare_modal, Family::Physical, 0.45f, "modal SD"},
    {&s_snare_string, Family::Physical, 0.80f, "string SD"},
};

const Entry kHatModels[] = {
    {&s_hat_square, Family::Synth, 0.00f, "square HH"},
    {&s_hat_ringmod, Family::Analog, 0.20f, "ringmod HH"},
    {&s_hat_modal, Family::Physical, 0.60f, "modal HH"},
};

struct SlotState
{
    const Entry* models;
    uint8_t      count;
    uint8_t      active;
    float        pan;
};

float s_sample_rate = 48000.0f;

SlotState s_slots[static_cast<uint8_t>(Slot::kCount)] = {
    {kKickModels, 3, 0, 0.5f},
    {kSnareModels, 4, 0, 0.5f},
    {kHatModels, 3, 0, 0.5f},
};

inline SlotState& StateOf(Slot slot)
{
    return s_slots[static_cast<uint8_t>(slot)];
}

// How likely a model is to be picked, given how far wildness has passed its
// exotic rating. This used to be a hard threshold, which made wildness behave
// like a switch rather than a dial: measured over 4000 rolls, the modal models
// went from never appearing to a third of all rolls across about 0.1 of knob
// travel, so most of the pool stayed invisible for most of the range. A ramp
// fades each model in gradually instead.
float ModelWeight(float exotic, float wildness)
{
    constexpr float kRamp = 0.30f;
    const float     t     = (wildness + 0.25f - exotic) / kRamp;
    return t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
}

// Cheapest model in a slot, used to reserve budget for slots not yet chosen.
float MinSlotCost(uint8_t slot)
{
    float best = s_cost[slot][0];
    for(uint8_t m = 1; m < s_slots[slot].count; ++m)
        if(s_cost[slot][m] < best)
            best = s_cost[slot][m];
    return best;
}

// Choose a model for one slot: roulette over the wanted family by weight, then
// over any family, then the tamest model - so a slot always ends up with
// something even when wildness excludes everything it would have preferred.
uint8_t PickModel(uint8_t         slot,
                  const SlotState& s,
                  Family           want,
                  float            wildness,
                  float            allowance,
                  RandFn           r)
{
    for(uint8_t pass = 0; pass < 2; ++pass)
    {
        float weight[8];
        float total = 0.0f;
        for(uint8_t i = 0; i < s.count; ++i)
        {
            const bool family_ok = (pass == 1) || (s.models[i].family == want);
            const bool ok        = family_ok && s_cost[slot][i] <= allowance;
            weight[i] = ok ? ModelWeight(s.models[i].exotic, wildness) : 0.0f;
            total += weight[i];
        }
        if(total <= 0.0f)
            continue;

        float pick = r() * total;
        for(uint8_t i = 0; i < s.count; ++i)
        {
            pick -= weight[i];
            if(pick <= 0.0f)
                return i;
        }
        return static_cast<uint8_t>(s.count - 1);
    }

    // Nothing affordable in any family: fall back to the cheapest measured
    // model rather than the tamest, since affordability is the binding
    // constraint here.
    uint8_t best = 0;
    for(uint8_t i = 1; i < s.count; ++i)
        if(s_cost[slot][i] < s_cost[slot][best])
            best = i;
    return best;
}

} // namespace

// -----------------------------------------------------------------------------
// Public interface
// -----------------------------------------------------------------------------

namespace
{
// Peak of one triggered hit at a given wildness, used to calibrate output level.
// The window is long enough to catch a slow attack, not just the first
// milliseconds. Uses its own deterministic generator so boot is repeatable.
constexpr float   kTargetPeak = 0.60f;
constexpr uint8_t kLevelDraws = 5;

float MeasurePeak(Voice* v, float sample_rate, float wildness)
{
    static uint32_t seed = 0x9E3779B9u;
    auto            det  = []() -> float {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed & 0xFFFFFFu) / static_cast<float>(0x1000000);
    };

    v->Init(sample_rate);
    v->SetOutputGain(1.0f);
    v->Randomize(wildness, det);
    v->Trig(1.0f);

    const int   n    = static_cast<int>(sample_rate * 0.12f);
    float       peak = 0.0f;
    for(int i = 0; i < n; ++i)
    {
        const float a = v->Process();
        const float m = a < 0.0f ? -a : a;
        if(m > peak)
            peak = m;
    }
    return peak;
}

// Time one model over a burst of samples and express it as a fraction of one
// sample period. Deterministic parameters so the measurement is repeatable.
float MeasureCost(Voice* v, float sample_rate, MicrosFn micros, float* peak_out)
{
    constexpr int kBurst = 1024;

    // A fixed mid-range randomize, using a deterministic generator so the
    // benchmark does not depend on the kit RNG's state.
    static uint32_t seed = 0x2545F491u;
    auto            det  = []() -> float {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed & 0xFFFFFFu) / static_cast<float>(0x1000000);
    };

    v->Init(sample_rate);
    v->Randomize(0.5f, det);
    v->Trig(1.0f);

    // Warm up so first-call effects don't land in the measurement.
    for(int i = 0; i < 64; ++i)
        v->Process();

    const uint32_t t0   = micros();
    float          acc  = 0.0f;
    float          peak = 0.0f;
    for(int i = 0; i < kBurst; ++i)
    {
        const float x = v->Process();
        acc += x;
        const float a = x < 0.0f ? -x : x;
        if(a > peak)
            peak = a;
    }
    const uint32_t t1 = micros();

    if(peak_out)
        *peak_out = peak;

    // Keep the loop from being optimised away.
    if(acc == 1.2345e-9f)
        return 1.0f;

    const float elapsed_us = static_cast<float>(t1 - t0);
    const float budget_us   = (1.0e6f / sample_rate) * static_cast<float>(kBurst);
    return budget_us > 0.0f ? elapsed_us / budget_us : 0.0f;
}
} // namespace

void VoicesInit(float sample_rate, MicrosFn micros)
{
    s_sample_rate = sample_rate;
    for(uint8_t i = 0; i < static_cast<uint8_t>(Slot::kCount); ++i)
    {
        SlotState& s = s_slots[i];
        for(uint8_t m = 0; m < s.count; ++m)
        {
            s.models[m].voice->Init(sample_rate);
            s.models[m].voice->SetOutputGain(1.0f);

            float peak   = 0.0f;
            s_cost[i][m] = micros
                               ? MeasureCost(s.models[m].voice, sample_rate, micros, &peak)
                               : 0.0f;

            // One draw is not enough to calibrate a level. The first attempt
            // measured a single mid-wildness randomize, but a model's loudness
            // varies enormously with its parameters, so kits routinely came out
            // five or six times full scale and the output saturator spent its
            // life acting as a hard limiter - which flattened the mix balance
            // into meaninglessness. Sample several draws across the wildness
            // range and trim to the loudest.
            if(micros)
            {
                for(uint8_t pass = 0; pass < kLevelDraws; ++pass)
                {
                    const float w = static_cast<float>(pass)
                                    / static_cast<float>(kLevelDraws - 1);
                    const float p = MeasurePeak(s.models[m].voice, sample_rate, w);
                    if(p > peak)
                        peak = p;
                }
            }

            float gain = 1.0f;
            if(peak > 1.0e-4f)
            {
                gain = kTargetPeak / peak;
                // Wide enough to actually tame a loud model. The old floor of
                // 0.25 could not: a model peaking at 6 stayed at 1.5.
                if(gain < 0.02f)
                    gain = 0.02f;
                if(gain > 8.0f)
                    gain = 8.0f;
            }
            s_gain[i][m] = gain;
            s.models[m].voice->SetOutputGain(gain);
        }
        s.active = 0;
        s.pan    = 0.5f;
    }
}

uint8_t VoiceModelCount(Slot slot)
{
    return s_slots[static_cast<uint8_t>(slot)].count;
}

const char* VoiceModelNameAt(Slot slot, uint8_t model)
{
    const SlotState& s = s_slots[static_cast<uint8_t>(slot)];
    return s.models[model < s.count ? model : 0].name;
}

float VoiceCostAt(Slot slot, uint8_t model)
{
    return s_cost[static_cast<uint8_t>(slot)][model];
}

float VoiceCheapestKitCost()
{
    float total = 0.0f;
    for(uint8_t i = 0; i < static_cast<uint8_t>(Slot::kCount); ++i)
        total += MinSlotCost(i);
    return total;
}

float VoiceCost(Slot slot)
{
    const uint8_t i = static_cast<uint8_t>(slot);
    return s_cost[i][s_slots[i].active];
}

void VoicesRoll(float wildness, RandFn rnd)
{

    // One family for the kit to agree on, then each slot may defect from it
    // with probability = wildness. At 0 the kit is coherent; at 1 the three
    // slots are chosen independently and can mix families freely.
    const uint8_t fam_count = static_cast<uint8_t>(Family::kCount);
    const uint8_t base_idx  = static_cast<uint8_t>(rnd() * fam_count) % fam_count;
    const Family  base      = static_cast<Family>(base_idx);

    // Spend the kit budget slot by slot, always holding back enough for the
    // slots not yet chosen so the last one is never left unaffordable.
    float remaining = kKitCostBudget;

    for(uint8_t i = 0; i < static_cast<uint8_t>(Slot::kCount); ++i)
    {
        SlotState& s = s_slots[i];

        Family want = base;
        if(rnd() < wildness)
        {
            const uint8_t idx = static_cast<uint8_t>(rnd() * fam_count) % fam_count;
            want              = static_cast<Family>(idx);
        }

        float reserve = 0.0f;
        for(uint8_t j = i + 1; j < static_cast<uint8_t>(Slot::kCount); ++j)
            reserve += MinSlotCost(j);

        s.active = PickModel(i, s, want, wildness, remaining - reserve, rnd);
        remaining -= s_cost[i][s.active];

        // Re-initialise before randomizing. A model that isn't selected never
        // gets processed, so its envelope state is frozen wherever it was left;
        // selecting it again would otherwise resume a stale tail as a click.
        // Init also gives every roll a clean, silent start.
        s.models[s.active].voice->Init(s_sample_rate);
        s.models[s.active].voice->SetOutputGain(s_gain[i][s.active]);
        s.models[s.active].voice->Randomize(wildness, rnd);

        // Pan spreads wider as wildness rises, but never fully hard-panned:
        // a drum stuck in one ear is a novelty rather than a kit.
        s.pan = RndW(rnd, wildness, 0.35f, 0.65f, 0.12f, 0.88f);
    }
}

void VoiceTrig(Slot slot, float accent)
{
    SlotState& s = StateOf(slot);
    s.models[s.active].voice->Trig(accent);
}

float VoiceProcess(Slot slot)
{
    SlotState& s = StateOf(slot);
    return s.models[s.active].voice->Process();
}

float VoicePan(Slot slot)
{
    return StateOf(slot).pan;
}

uint8_t VoiceModelIndex(Slot slot)
{
    return StateOf(slot).active;
}

const char* VoiceModelName(Slot slot)
{
    const SlotState& s = StateOf(slot);
    return s.models[s.active].name;
}

} // namespace sorrow
