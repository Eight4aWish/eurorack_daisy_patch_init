#pragma once
#include <cstddef>
namespace daisy {
struct AudioHandle {
    typedef const float* const* InputBuffer;
    typedef float**             OutputBuffer;
};
struct GateIn {
    bool pending = false;
    bool Trig() { bool t = pending; pending = false; return t; }
    bool State() { return false; }
};
namespace patch_sm {
struct DaisyPatchSM {
    float  adc[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f, 0.f, 0.f};
    float  GetAdcValue(int i) { return adc[i]; }
    GateIn gate_in_1, gate_in_2;
};
} // namespace patch_sm
} // namespace daisy
