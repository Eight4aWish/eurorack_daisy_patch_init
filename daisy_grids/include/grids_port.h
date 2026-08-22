#pragma once

#include <cstdint>

namespace daisy_grids::grids_port {

// Minimal, portable port of Mutable Instruments Grids drum pattern generator.
// Source reference: deps/mutable/eurorack/grids/pattern_generator.{h,cc}
// License: GPL-3.0-or-later

// Drum-map banks, in the order the long-press cycles them:
//   0  Original  - Emilie Gillet's Grids map
//   1  Club      - Lakh, selected by rhythmic signature (four-to-floor, breaks,
//                  half-time) rather than by genre label
//   2  Traditional - Groove MIDI, human drummers
//   3  Latin     - Groove MIDI, filtered to latin, jazz, afro-cuban, afrobeat,
//                  New Orleans, reggae and highlife. Latin styles are the
//                  plurality at 41%, hence the name; the source files keep the
//                  fuller "jazzlatin" corpus name. The tightest family of the
//                  four, and the most coherent map of the set.
//   4  User      - optional, read from the SD card at boot (see user_bank.h)
// All of them are 25 nodes of 96 bytes sharing the same per-lane value
// distribution, so everything downstream is unaffected by the choice.
// Four compiled in, plus one optional slot filled at boot from the SD card.
constexpr uint8_t kNumFactoryBanks = 4;
constexpr uint8_t kMaxBanks        = 5;

// Point the fourth bank at 25*96 bytes of node data and make it selectable.
// Passing nullptr removes it again.
void SetUserBank(const uint8_t* nodes);

// How many banks are actually available - 3, or 4 once a user bank is loaded.
uint8_t BankCount();

void    SetBank(uint8_t bank);
uint8_t GetBank();

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

} // namespace daisy_grids::grids_port
