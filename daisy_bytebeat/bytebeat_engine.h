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

#pragma once
#include <cstdint>
#include "formulas.h"

// Dual-voice bytebeat engine. One master phase accumulator (clocked by voice 1 /
// Out1, the primary) produces two independent voices:
//   Out1 = formula1(t)  (primary; CV/gate are derived from this)
//   Out2 = formula2(t)  (rides the same master phase, unless decoupled -- see
//                        SetOut2Decoupled -- into an independent drone)
class BytebeatEngine {
public:
    void Init();

    // Advance the master phase by one output sample (48kHz) and produce both
    // voices, each linearly interpolated and converted to [-1, 1].
    void Process(float& out1, float& out2);

    // Tick-changed flags: true for the one Process() sample where the voice's
    // t actually advanced (vs. just being linearly interpolated toward it).
    // TChanged() is Out1/the master phase; TChanged2() is Out2's OWN t when
    // decoupled (drone) -- it free-runs independently of the master, so the
    // master's tick doesn't correspond to Out2's actual step rate. While
    // coupled, Out2 rides the master phase, so TChanged2() mirrors TChanged().
    bool TChanged() const { return tChanged_; }
    bool TChanged2() const { return out2Decoupled_ ? tChanged2_ : tChanged_; }

    // How many whole t-steps this ONE Process() sample crossed (0 if none).
    // Normally 0 or 1, but above Rate ~6x on an 8kHz formula (and at almost
    // any Rate on a 44.1kHz one) the phase increment exceeds one t-step per
    // output sample and t jumps several at once. Anything counting ticks must
    // use THIS, not TChanged() -- a bool saturates at one per sample, which
    // made CV Out's hold window stop shrinking past Rate ~6x and pinned the
    // LFO frequency (daisy-*).
    uint32_t GetTStep() const { return tStep_; }
    uint32_t GetTStep2() const { return out2Decoupled_ ? tStep2_ : tStep_; }
    // Raw (un-interpolated) 0-255 sample, the formula's actual value at t --
    // as opposed to Process()'s output, which is linearly interpolated toward
    // it across the samples between ticks. GetRawSample() is Out1; GetRawSample2()
    // is Out2's own curB2_ when decoupled (drone), else the shared curB_
    // (mirrors TChanged2()'s coupled/decoupled dispatch).
    uint8_t GetRawSample() const { return curA_; }  // Out1 raw (0-255)
    uint8_t GetRawSample2() const { return out2Decoupled_ ? curB2_ : curB_; }
    uint32_t GetT() const { return t_; }
    // Out2's own t when decoupled (drone), else the shared master t -- mirrors
    // GetRawSample2()/GetTStep2(). CV Out's Hold anchors its capture phase to
    // this, so it must follow the same coupled/decoupled dispatch.
    uint32_t GetT2() const { return out2Decoupled_ ? t2_ : t_; }

    // Output samples per bytebeat tick, i.e. 48000 / (tick rate). The CV path
    // needs this to size the capture interval -- see CvOutput. May be BELOW 1
    // (more than one t-step per output sample) at high Rate; callers that need
    // a realisable CV update interval floor it themselves. Returns a large
    // finite value rather than infinity when the engine is effectively stopped.
    float GetSamplesPerTick() const;
    float GetSamplesPerTick2() const;  // Out2's own rate when decoupled

    // Formula selection (voice 1 = Out1 primary, voice 2 = Out2)
    void SetFormula1(int index);
    void SetFormula2(int index);
    int GetFormula1Index() const { return formula1Index_; }
    int GetFormula2Index() const { return formula2Index_; }

    // Shared parameters A and B (both voices use these)
    void SetParamA(int32_t a) { paramA_ = a; }
    void SetParamB(int32_t b) { paramB_ = b; }
    int32_t GetParamA() const { return paramA_; }
    int32_t GetParamB() const { return paramB_; }

    // Param interpolation grid step (daisy-gac): 0/1 = off (exact eval), else
    // quantise A,B to a grid of this step and bilinearly crossfade the formula
    // outputs at the surrounding grid corners. Tames A/B discontinuity.
    void SetParamQuant(int q) { paramQuant_ = q; }
    int GetParamQuant() const { return paramQuant_; }

    // Rate multiplier (1/64x to 64x; also scaled by external clock)
    void SetRate(float rate);
    float GetRate() const { return rateMultiplier_; }

    // V/oct pitch (daisy-c5i): internally hard-sync the master phase to freqHz so
    // the output is periodic at freqHz -> a clean tuned tone (the Rate knob then
    // sets timbre: how far t advances per cycle). 0 = disabled (Clock/normal mode).
    void SetPitchSync(float freqHz);

    // Where `t` restarts from on a sync reset, instead of 0.
    //
    // Bytebeat formulas commonly open with a long constant run — a sweep or a
    // riser does nothing for hundreds or thousands of ticks — so a sync window
    // anchored at 0 can loop a stretch that never leaves silence. The offset
    // chooses which part of the formula the window lands on.

    // Out2 decouple / drone (daisy-pcq). When enabled, Out2 FORKS off the master:
    // on the coupled->decoupled edge it snapshots the current phase, rate and A/B,
    // then free-runs on its own accumulator at the frozen rate with the frozen A/B
    // -- unaffected by Clock/V-oct/Rate/gate or live A/B thereafter. Re-coupling
    // snaps Out2 back to the shared master phase + live params. Idempotent: only
    // the false->true transition snapshots, so this may be called every loop.
    void SetOut2Decoupled(bool decoupled);
    bool GetOut2Decoupled() const { return out2Decoupled_; }

    // Frozen drone state accessors (for persistence). Valid once decoupled; these
    // are the exact phase increment (rate) + A/B captured at the decouple edge.
    uint64_t GetDroneInc()    const { return phaseIncrement2_; }
    int32_t  GetDroneParamA() const { return paramA2_; }
    int32_t  GetDroneParamB() const { return paramB2_; }

    // Restore a decoupled drone from persisted state (used at power-on instead of
    // the live snapshot, so pitch + A/B come back bit-exact across a power cycle).
    // Call after SetFormula2(). Restarts the drone phase from t=0.
    void RestoreOut2Drone(uint64_t inc, int32_t a, int32_t b);

    // Reset master t to 0
    void Reset();

    // Request a sample-accurate hard-sync reset. Called from the gate EXTI ISR;
    // only sets a flag (no shared-state race) -- applied at the start of the
    // next audio sample in Process(), which restarts the waveform at t=0.
    void SyncReset() { syncPending_ = true; }

private:
    void UpdatePhaseIncrement();  // from voice 1's base sample rate

    // Master phase accumulator (32.32 fixed point)
    uint64_t phase_ = 0;
    uint64_t phaseIncrement_ = 0;
    uint32_t t_ = 0;
    bool tChanged_ = false;
    uint32_t tStep_ = 0;    // t-steps crossed this sample (0, 1, or a skipped run)
    volatile bool syncPending_ = false;  // hard-sync reset requested by gate EXTI

    // V/oct hard-sync pitch: internal per-sample reset accumulator (0 inc = off).
    volatile float pitchSyncInc_ = 0.0f;  // freqHz / OUTPUT_SAMPLE_RATE
    float pitchSyncPhase_ = 0.0f;

    // Out2 decouple / drone (daisy-pcq): Out2's independent time base + frozen
    // params, all used only while out2Decoupled_. Seeded from the master on the
    // couple->decouple edge so there's no discontinuity at the switch.
    volatile bool out2Decoupled_ = false;
    uint64_t phase2_ = 0;
    uint64_t phaseIncrement2_ = 0;   // frozen master increment at decouple time
    uint32_t t2_ = 0;
    bool     tChanged2_ = false;     // true the one sample Out2's OWN t advances
    uint32_t tStep2_ = 0;            // Out2's own t-steps crossed this sample
    uint8_t  prevB2_ = 0, curB2_ = 0;
    int32_t  paramA2_ = 128, paramB2_ = 128;  // frozen A/B at decouple time

    // Voice 1 (Out1, primary) -- drives the master clock
    volatile int formula1Index_ = 0;
    volatile const FormulaInfo* formula1_ = nullptr;
    uint8_t prevA_ = 0, curA_ = 0;

    // Voice 2 (Out2)
    volatile int formula2Index_ = 0;
    volatile const FormulaInfo* formula2_ = nullptr;
    uint8_t prevB_ = 0, curB_ = 0;

    // Shared parameters (volatile: written by main loop, read by audio ISR)
    volatile int32_t paramA_ = 1;
    volatile int32_t paramB_ = 1;
    volatile int paramQuant_ = 0;  // 0/1 = off; else A/B interpolation grid step

    float rateMultiplier_ = 1.0f;
};
