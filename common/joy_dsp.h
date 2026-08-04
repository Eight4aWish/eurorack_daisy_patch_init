// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// joy_dsp.h — front-end-agnostic DSP helpers shared by the Joy family.
// Extracted from daisy_braids_oled so Joy Lite (and a future refactor of Joy)
// can reuse the same envelope and fixed-point conversions rather than
// duplicating them. Pure; no hardware or UI dependencies.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace joy {

inline float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
inline float Clamp11(float v) { return std::max(-1.0f, std::min(1.0f, v)); }

inline int16_t Float01ToParamQ15(float v)
{
    v = Clamp01(v);
    return static_cast<int16_t>(std::lround(v * 32767.0f));
}

inline int16_t SemitonesToQ7(float semitones)
{
    return static_cast<int16_t>(std::lround(semitones * 128.0f));
}

inline int16_t ClampI16(int32_t v)
{
    if(v > 32767)
        return 32767;
    if(v < -32768)
        return -32768;
    return static_cast<int16_t>(v);
}

// Simple attack/decay(/sustain) envelope with millisecond times, gate-driven.
//
// Starts in Drone: until the first gate is seen the envelope sits wide open, so
// a module with nothing patched to GATE IN 1 sings continuously the way a stock
// Braids does (its AD->VCA depth defaults to zero, see braids/settings.cc
// kInitSettings). The first rising gate hands control to the envelope for good,
// and because Drone leaves the level at 1.0 that handover is click-free: the
// attack segment is already at full scale and passes straight to Sustain.
struct AdEnvelope
{
    enum class Stage : uint8_t { Drone, Dead, Attack, Sustain, Decay };

    void Init(float sample_rate_hz)
    {
        sample_rate_hz_ = std::max(1.0f, sample_rate_hz);
        dt_ms_          = 1000.0f / sample_rate_hz_;
        stage_          = Stage::Drone;
        level_          = 1.0f;
        attack_ms_      = 10.0f;
        decay_ms_       = 100.0f;
    }

    void SetGate(bool gate)
    {
        const bool rising  = gate && !gate_;
        const bool falling = !gate && gate_;
        gate_              = gate;
        if(rising)
            stage_ = Stage::Attack;  // also the one-way exit from Drone
        else if(falling && stage_ != Stage::Dead && stage_ != Stage::Drone)
            stage_ = Stage::Decay;
    }

    void SetAttackDecayMs(float attack_ms, float decay_ms)
    {
        attack_ms_ = std::max(0.0f, attack_ms);
        decay_ms_  = std::max(0.0f, decay_ms);
    }

    float Process()
    {
        switch(stage_)
        {
            case Stage::Drone: level_ = 1.0f; break;
            case Stage::Dead: level_ = 0.0f; break;
            case Stage::Attack:
                if(attack_ms_ <= 0.0f)
                    level_ = 1.0f;
                else
                {
                    level_ += dt_ms_ / attack_ms_;
                    if(level_ >= 1.0f)
                        level_ = 1.0f;
                }
                if(level_ >= 1.0f)
                    stage_ = gate_ ? Stage::Sustain : Stage::Decay;
                break;
            case Stage::Sustain:
                level_ = 1.0f;
                if(!gate_)
                    stage_ = Stage::Decay;
                break;
            case Stage::Decay:
                if(decay_ms_ <= 0.0f)
                    level_ = 0.0f;
                else
                {
                    level_ -= dt_ms_ / decay_ms_;
                    if(level_ <= 0.0f)
                        level_ = 0.0f;
                }
                if(level_ <= 0.0f)
                    stage_ = Stage::Dead;
                break;
        }
        return level_;
    }

  private:
    float sample_rate_hz_ = 48000.0f;
    float dt_ms_          = 1000.0f / 48000.0f;
    Stage stage_          = Stage::Drone;
    float level_          = 1.0f;
    float attack_ms_      = 10.0f;
    float decay_ms_       = 100.0f;
    bool  gate_           = false;
};

} // namespace joy
