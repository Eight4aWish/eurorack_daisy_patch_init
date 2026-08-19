// Host-side stand-in for the slice of libDaisy that oled_soft_i2c.h touches, so
// the real driver compiles with a PC compiler. Never built into firmware.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).
#pragma once
#include <cstddef>
#include <cstdint>

#define __NOP() ((void)0)

namespace daisy {
struct Pin { int p = 0; };
struct GPIO {
    enum class Mode { OUTPUT, INPUT };
    enum class Pull { NOPULL };
    enum class Speed { HIGH };
    void Init(Pin, Mode, Pull, Speed) {}
    void Write(bool) {}
    bool Read() { return true; }
};
struct System {
    static void     Delay(uint32_t) {}
    static uint32_t GetNow() { return 0; }
};
namespace patch_sm {
struct DaisyPatchSM { Pin A2, A3; };
} // namespace patch_sm
} // namespace daisy
