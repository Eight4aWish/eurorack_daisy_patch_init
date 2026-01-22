#pragma once

#include <cstdint>

namespace drumseq_mi::grids_port {

// Minimal, portable port of Mutable Instruments Grids drum pattern generator.
// Source reference: deps/mutable/eurorack/grids/pattern_generator.{h,cc}
// License: GPL-3.0-or-later

struct GridsStep
{
    bool bd = false;
    bool sd = false;
    bool hh = false;

    bool bd_accent = false;
    bool sd_accent = false;
    bool hh_accent = false;
};

class GridsDrumGenerator
{
  public:
    void Init(uint16_t seed = 1);
    void Reset();

    // Parameters are 0..255 in original Grids firmware.
    // - x,y select a location on the pattern map
    // - density controls per-part density (we expose a single density externally; caller may fan out)
    // - randomness controls perturbation and accent distribution
    GridsStep Tick(uint8_t x,
                   uint8_t y,
                   uint8_t density_bd,
                   uint8_t density_sd,
                   uint8_t density_hh,
                   uint8_t randomness);

    uint8_t step() const { return step_; }

  private:
    uint16_t rng_state_ = 1;
    uint8_t  step_      = 0;
    uint8_t  perturb_[3]{};

    uint8_t RandByte();

    static uint8_t U8Mix(uint8_t a, uint8_t b, uint8_t balance);
    static uint8_t U8U8MulShift8(uint8_t a, uint8_t b);

    static uint8_t ReadDrumMap(uint8_t step, uint8_t instrument, uint8_t x, uint8_t y);
};

} // namespace drumseq_mi::grids_port
