// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// voct_cal.h — the two-point 1V/oct calibration (Joy, Joy Lite, Wish on patch.Init(), MultiOsc). Each unit keeps
// one, shared by those apps: common/unit_cal.h.
//
// A simple two-point linear calibration in the same terms the pitch code already
// uses:  semitones = (adc - center) * slope, where `adc` is GetAdcValue(CV_5).
// The user feeds 1 V then 3 V (two octaves apart) and the two ADC captures fix
// the line. A capture that doesn't make sense is refused, and the caller saves
// nothing and says so, so a botched run can never be stored as a calibration.

#pragma once
#include <cmath>

namespace joy {

struct VoctCal {
    float center; // ADC reading that maps to the base note (0 V)
    float slope;  // semitones per ADC unit
};

// The nominal law: GetAdcValue reads +-1 for +-5 V, so 60 semitones per unit. A unit's own input differs by a
// few percent; a capture further off than kSlopeTolerance was mis-patched (an unpatched jack, the wrong
// voltage, a slip), and is refused.
constexpr float kNominalSlope   = 60.0f;
constexpr float kSlopeTolerance = 0.10f;
constexpr float kMaxCenter      = 0.10f;  // 0.5 V: the 0 V reading is a few hundredths at most

// Compute a calibration from two captures: ADC at 1 V and ADC at 3 V (which are
// +12 and +36 semitones above the 0 V base note). False, and `out` untouched, if
// the line is implausible.
inline bool ComputeVoctCal(float adc_1v, float adc_3v, VoctCal& out)
{
    const float d = adc_3v - adc_1v;          // spans 2 octaves = 24 semitones
    if(std::fabs(d) < 1e-3f)
        return false;
    const float slope  = 24.0f / d;           // semitones per ADC unit
    const float center = adc_1v - d * 0.5f;    // extrapolate down one octave to 0 V
    if(std::fabs(slope / kNominalSlope - 1.0f) > kSlopeTolerance || std::fabs(center) > kMaxCenter)
        return false;
    out = {center, slope};
    return true;
}

inline float VoctToSemitones(const VoctCal& c, float adc)
{
    return (adc - c.center) * c.slope;
}

// How far this unit's input is from the nominal law, in cents per octave: what the calibration corrects.
// Shown after a calibration so units can be compared; a few tens of cents is normal.
inline int CentsPerOctaveFromNominal(const VoctCal& c)
{
    return static_cast<int>(std::lround(1200.0f * (kNominalSlope / c.slope - 1.0f)));
}

// libDaisy before v9.0.0 read every ADC 31/32 of its true value: its oversampling set the HAL's ratio to 31
// where 32 was meant, against a shift of 5 bits (libDaisy #695, fixed in v9.0.0). The Patch SM's CV inputs
// read (0.5 - raw) * 2, so a reading from then, v8, relates to today's, v9, as v9 = a * v8 - b with
// a = 32/31 and b = 1/31: 3.2 % more gain, and 0 V about 0.03 lower. A calibration measured then maps to
// today's terms as below. Every calibration kept in an app's own settings was measured on libDaisy v8.0.0
// (this repo's, 17 Jan to 11 Nov 2026), so it is converted when carried over.
constexpr float kLibDaisy8Gain   = 32.0f / 31.0f;
constexpr float kLibDaisy8Offset = 1.0f / 31.0f;
constexpr VoctCal FromLibDaisy8(const VoctCal& v8)
{
    return {kLibDaisy8Gain * v8.center - kLibDaisy8Offset, v8.slope / kLibDaisy8Gain};
}

} // namespace joy
