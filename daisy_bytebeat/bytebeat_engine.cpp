// -----------------------------------------------------------------------------
// Ogham — a dual-voice bytebeat synthesizer for Eurorack
//
// Author:     Steven Collins, 2026, Keeos.io
// Copyright:  (c) 2026 Steven Collins
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
//
// This file is part of the Ogham firmware. See LICENSE-firmware.txt at the
// repository root for the full licence text.
// https://github.com/keeos-io/ogham
// -----------------------------------------------------------------------------

#include "bytebeat_engine.h"

static constexpr float OUTPUT_SAMPLE_RATE = 48000.0f;
static constexpr uint64_t PHASE_ONE = (uint64_t)1 << 32;

// Param interpolation (daisy-gac). Quantise A,B to a grid of step q and
// bilinearly crossfade the formula's OUTPUTS at the 4 surrounding grid corners
// (all evaluated at the same t -> contemporaneous samples, so the stream's
// natural time-evolution is preserved). q<=1 => off (exact eval). Landing on a
// grid point (fa=fb=0) returns the exact formula. Returns a 0-255 sample.
static inline uint8_t EvalInterp(const FormulaInfo* f, uint32_t t,
                                 int32_t A, int32_t B, int q) {
    if (!f || !f->func) return 0;
    if (q <= 1) return f->func(t, A, B);

    int32_t Alo = (A / q) * q, Ahi = Alo + q; if (Ahi > 255) Ahi = 255;
    int32_t Blo = (B / q) * q, Bhi = Blo + q; if (Bhi > 255) Bhi = 255;
    float fa = (Ahi > Alo) ? (float)(A - Alo) / (float)(Ahi - Alo) : 0.0f;
    float fb = (Bhi > Blo) ? (float)(B - Blo) / (float)(Bhi - Blo) : 0.0f;

    float v00 = f->func(t, Alo, Blo), v10 = f->func(t, Ahi, Blo);
    float v01 = f->func(t, Alo, Bhi), v11 = f->func(t, Ahi, Bhi);
    float vA0 = v00 + fa * (v10 - v00);
    float vA1 = v01 + fa * (v11 - v01);
    float v = vA0 + fb * (vA1 - vA0);
    if (v < 0.0f) v = 0.0f; if (v > 255.0f) v = 255.0f;
    return (uint8_t)(v + 0.5f);
}

// Output samples per tick = 2^32 / phaseIncrement. Guarded both ends: a zero
// increment (engine stopped) would divide by zero, and a huge increment at
// high Rate would round to zero.
static inline float SamplesPerTickFrom(uint64_t inc) {
    if (inc == 0) return 1.0e6f;                 // stopped: treat as very slow
    float s = (float)((double)((uint64_t)1 << 32) / (double)inc);
    // Deliberately NOT floored at 1: above Rate ~6x there really is more than
    // one t-step per output sample, and clamping here is what pinned the CV
    // hold window (and so the LFO frequency) past that point.
    if (s < 1.0e-3f) s = 1.0e-3f;
    if (s > 1.0e6f)  s = 1.0e6f;
    return s;
}

float BytebeatEngine::GetSamplesPerTick() const {
    return SamplesPerTickFrom(phaseIncrement_);
}

float BytebeatEngine::GetSamplesPerTick2() const {
    return SamplesPerTickFrom(out2Decoupled_ ? phaseIncrement2_ : phaseIncrement_);
}

void BytebeatEngine::Init() {
    phase_ = 0;
    t_ = 0;
    tChanged_ = false;
    prevA_ = curA_ = 0;
    prevB_ = curB_ = 0;
    rateMultiplier_ = 1.0f;
    pitchSyncInc_ = 0.0f;
    pitchSyncPhase_ = 0.0f;
    out2Decoupled_ = false;
    phase2_ = 0;
    phaseIncrement2_ = 0;
    t2_ = 0;
    tChanged2_ = false;
    prevB2_ = curB2_ = 0;
    paramA2_ = paramB2_ = 128;
    SetFormula1(0);
    SetFormula2(1);  // distinct default so Out2 differs from Out1 (overwritten by persisted value later)
    // Params are driven live by the pots (fixed 0-255 range, no per-formula
    // defaults); seed a neutral mid value until the first pot read.
    paramA_ = 128;
    paramB_ = 128;
}

void BytebeatEngine::Reset() {
    phase_ = 0;
    t_ = 0;
    prevA_ = curA_ = 0;
    prevB_ = curB_ = 0;
    tChanged_ = false;
}

void BytebeatEngine::SetFormula1(int index) {
    int count = GetFormulaCount();
    if (count == 0) return;
    if (index < 0) index = 0;            // clamp (encoder no longer wraps)
    if (index >= count) index = count - 1;
    const bool changed = (index != formula1Index_);
    formula1Index_ = index;
    formula1_ = GetFormulaAt(index);
    UpdatePhaseIncrement();  // voice 1 clocks the master phase

    // Restart the waveform when the formula actually changes, so selecting a
    // one-shot fires it.
    //
    // `t` is otherwise free-running and only a Sync edge resets it, so after a
    // few minutes it sits in the millions. Roughly a fifth of the bank is
    // percussive one-shots that make their sound in the first few thousand ticks
    // and are static ever after -- so selecting one gave silence, at every A and
    // B, with nothing to indicate why. Measured lifetimes run from 0.38 s to
    // about a minute (tools/formula_life.cpp).
    //
    // Requesting the reset rather than doing it here means Process() performs it
    // on the audio thread through the same path Sync uses, including honouring
    // the start offset.
    if (changed) syncPending_ = true;
}

void BytebeatEngine::SetFormula2(int index) {
    int count = GetFormulaCount();
    if (count == 0) return;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    const bool changed = (index != formula2Index_);
    formula2Index_ = index;
    formula2_ = GetFormulaAt(index);

    // Same restart as voice 1, so a one-shot picked on Out 2 also fires. Both
    // voices share the master phase, so this restarts Out 1 too — they are
    // already locked to each other and stay that way.
    //
    // Skipped while Out 2 is a decoupled drone: the drone runs on its own
    // accumulator and would not restart anyway, so all this would do is
    // interrupt Out 1 for a change the user made to a frozen voice.
    if (changed && !out2Decoupled_) syncPending_ = true;
}

void BytebeatEngine::SetRate(float rate) {
    if (rate < 0.001f) rate = 0.001f;
    if (rate > 100.0f) rate = 100.0f;
    rateMultiplier_ = rate;
    UpdatePhaseIncrement();
}

void BytebeatEngine::SetPitchSync(float freqHz) {
    // Per-sample increment; when the phase wraps in Process() we reset t=0.
    // Phase itself free-runs (not reset here) so live pitch updates don't click.
    pitchSyncInc_ = (freqHz > 0.0f) ? (freqHz / (float)OUTPUT_SAMPLE_RATE) : 0.0f;
}

void BytebeatEngine::SetOut2Decoupled(bool decoupled) {
    if (decoupled && !out2Decoupled_) {
        // Coupled -> decoupled: snapshot & freeze Out2 at the current state, seeding
        // its independent accumulator from the master so there's no click. From here
        // Out2 advances at the frozen rate with the frozen A/B, ignoring the master.
        phase2_          = phase_;
        phaseIncrement2_ = phaseIncrement_;
        t2_              = t_;
        prevB2_          = prevB_;
        curB2_           = curB_;
        paramA2_         = paramA_;
        paramB2_         = paramB_;
    }
    out2Decoupled_ = decoupled;
}

void BytebeatEngine::RestoreOut2Drone(uint64_t inc, int32_t a, int32_t b) {
    // Boot-time restore: reinstate the frozen rate + A/B exactly (no live snapshot),
    // and restart the drone waveform from t=0. formula2_ must be set (SetFormula2).
    phaseIncrement2_ = inc;
    paramA2_ = a;
    paramB2_ = b;
    phase2_ = 0;
    t2_ = 0;
    const FormulaInfo* f2 = (const FormulaInfo*)formula2_;
    curB2_ = EvalInterp(f2, 0u, a, b, paramQuant_);
    prevB2_ = curB2_;
    out2Decoupled_ = true;
}

void BytebeatEngine::UpdatePhaseIncrement() {
    if (!formula1_) return;
    float baseSR = (float)formula1_->baseSampleRate;
    double inc = ((double)baseSR / (double)OUTPUT_SAMPLE_RATE)
                 * (double)rateMultiplier_ * (double)PHASE_ONE;
    phaseIncrement_ = (uint64_t)inc;
}

void BytebeatEngine::Process(float& out1, float& out2) {
    const FormulaInfo* f1 = (const FormulaInfo*)formula1_;
    const FormulaInfo* f2 = (const FormulaInfo*)formula2_;

    // Hard sync: restart the waveform at t=0 this sample, re-evaluating both voices
    // at their start so the output begins cleanly (not silence) until t advances.
    // Two sources: (1) a gate edge (EXTI) requested a reset; (2) V/oct pitch sync
    // -- an internal accumulator wraps at f_pitch to make the tone periodic/tuned.
    bool doReset = false;
    if (syncPending_) { syncPending_ = false; doReset = true; }
    if (pitchSyncInc_ > 0.0f) {
        pitchSyncPhase_ += pitchSyncInc_;
        if (pitchSyncPhase_ >= 1.0f) { pitchSyncPhase_ -= 1.0f; doReset = true; }
    }
    if (doReset) {
        phase_ = 0;
        t_ = 0;
        curA_ = EvalInterp(f1, 0, paramA_, paramB_, paramQuant_);
        prevA_ = curA_;
        curB_ = EvalInterp(f2, 0u, paramA_, paramB_, paramQuant_);
        prevB_ = curB_;
    }

    tChanged_ = false;
    tStep_ = 0;

    {
        uint32_t prevT = t_;
        phase_ += phaseIncrement_;
        uint32_t newT = (uint32_t)(phase_ >> 32);

        if (newT != prevT) {
            tChanged_ = true;
            tStep_ = newT - prevT;   // usually 1; >1 when this sample skipped steps
            t_ = newT;
            prevA_ = curA_;
            curA_ = EvalInterp(f1, newT, paramA_, paramB_, paramQuant_);
            // Out2 rides the master phase + live A/B while COUPLED. When
            // decoupled it's driven by its own phase below.
            if (!out2Decoupled_) {
                prevB_ = curB_;
                curB_ = EvalInterp(f2, newT, paramA_, paramB_, paramQuant_);
            }
        }

        // Decoupled drone: Out2 free-runs on its own accumulator at the frozen rate
        // with the frozen A/B -- untouched by the master phase, gate or pitch-sync.
        if (out2Decoupled_) {
            tChanged2_ = false;
            tStep2_ = 0;
            uint32_t prevT2 = t2_;
            phase2_ += phaseIncrement2_;
            uint32_t newT2 = (uint32_t)(phase2_ >> 32);
            if (newT2 != prevT2) {
                tChanged2_ = true;
                tStep2_ = newT2 - prevT2;
                t2_ = newT2;
                prevB2_ = curB2_;
                curB2_ = EvalInterp(f2, newT2, paramA2_, paramB2_, paramQuant_);
            }
        }
    }

    // Out1 (master sub-phase).
    float frac = (float)(phase_ & 0xFFFFFFFF) / (float)PHASE_ONE;
    float sa = (float)prevA_ + frac * ((float)curA_ - (float)prevA_);
    out1 = (sa / 127.5f) - 1.0f;

    // Out2: its own sub-phase when decoupled, else the shared master sub-phase.
    float sb;
    if (out2Decoupled_) {
        float frac2 = (float)(phase2_ & 0xFFFFFFFF) / (float)PHASE_ONE;
        sb = (float)prevB2_ + frac2 * ((float)curB2_ - (float)prevB2_);
    } else {
        sb = (float)prevB_ + frac * ((float)curB_ - (float)prevB_);
    }
    out2 = (sb / 127.5f) - 1.0f;
}
