#pragma once
/**
 * tanh_table.h — one table of tanh over ±8 in 2,048 steps, for both engines that need a
 * fast tanh: the GRU filters' gates (gru_runtime.h, gru::Tanh) and the A2 engine's tanh
 * not-amp (nam_a2_runtime.h, bend::FastTanh). Each keeps its own lookup.
 *
 * Held once because both sit in DTCM, beneath the stack: a second 8 KB table for the
 * not-amp took the stack's headroom from about 15 KB to 7 KB (2026-10-25). The values
 * and the GRU's lookup are unchanged from when the table was the GRU's alone.
 */
#include <cmath>

namespace tanh_table
{
inline constexpr int   kN     = 2048;
inline constexpr float kRange = 8.f;
inline float tab[kN + 1];  // tab[i] = tanh(-8 + 16 i / 2048), so tab[1024 + k] = tanh(k / 128)
inline const bool ready = [] {
    for(int i = 0; i <= kN; i++)
        tab[i] = std::tanh(-kRange + 2.f * kRange * (float)i / (float)kN);
    return true;
}();
} // namespace tanh_table
