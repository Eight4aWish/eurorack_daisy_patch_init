// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// voct_cal.h — shared 1V/oct calibration for the Joy family (Joy + Joy Lite).
//
// A simple two-point linear calibration in the same terms the pitch code already
// uses:  semitones = (adc - center) * slope, where `adc` is GetAdcValue(CV_5).
// The user feeds 1 V then 3 V (two octaves apart) and the two ADC captures fix
// the line. Falls back to the supplied defaults if the capture is implausible,
// so a botched run can never produce garbage tracking.

#pragma once
#include <cmath>

namespace joy {

struct VoctCal {
    float center; // ADC reading that maps to the base note (0 V)
    float slope;  // semitones per ADC unit
};

// Compute a calibration from two captures: ADC at 1 V and ADC at 3 V (which are
// +12 and +36 semitones above the 0 V base note). Returns `fallback` unchanged
// if the two points are too close or the resulting slope is out of a sane range
// (guards against an unpatched jack or a mis-pressed step).
inline VoctCal ComputeVoctCal(float adc_1v, float adc_3v, const VoctCal& fallback)
{
    const float d = adc_3v - adc_1v;          // spans 2 octaves = 24 semitones
    if(std::fabs(d) < 1e-3f)
        return fallback;
    const float slope  = 24.0f / d;           // semitones per ADC unit
    const float center = adc_1v - d * 0.5f;    // extrapolate down one octave to 0 V
    if(slope < 30.0f || slope > 120.0f)        // ~60 is normal; reject nonsense
        return fallback;
    return {center, slope};
}

inline float VoctToSemitones(const VoctCal& c, float adc)
{
    return (adc - c.center) * c.slope;
}

} // namespace joy
