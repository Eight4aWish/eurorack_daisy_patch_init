#include "grids_port.h"

#include "grids_nodes.h"

namespace drumseq_mi::grids_port {

namespace {
// This mapping matches the upstream Grids drum_map[][] pointer table.
const uint8_t* const drum_map[5][5] = {
    {node_10, node_8, node_0, node_9, node_11},
    {node_15, node_7, node_13, node_12, node_6},
    {node_18, node_14, node_4, node_5, node_3},
    {node_23, node_16, node_21, node_1, node_2},
    {node_24, node_19, node_17, node_20, node_22},
};
} // namespace

void GridsDrumGenerator::Init(uint16_t seed)
{
    rng_state_ = seed ? seed : 1;
    Reset();
}

void GridsDrumGenerator::Reset()
{
    step_ = 0;
    perturb_[0] = perturb_[1] = perturb_[2] = 0;
}

uint8_t GridsDrumGenerator::RandByte()
{
    // Same 16-bit Galois LFSR as avrlib::Random (period 65535).
    rng_state_ = (rng_state_ >> 1) ^ (static_cast<uint16_t>(-(rng_state_ & 1u)) & 0xB400u);
    return static_cast<uint8_t>(rng_state_ >> 8);
}

uint8_t GridsDrumGenerator::U8U8MulShift8(uint8_t a, uint8_t b)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(a) * static_cast<uint16_t>(b)) >> 8);
}

uint8_t GridsDrumGenerator::U8Mix(uint8_t a, uint8_t b, uint8_t balance)
{
    // Portable equivalent of avrlib::U8Mix.
    // balance=0 -> a, balance=255 -> almost b
    const uint16_t inv = static_cast<uint16_t>(255u - balance);
    const uint16_t sum = static_cast<uint16_t>(a) * inv + static_cast<uint16_t>(b) * balance;
    return static_cast<uint8_t>(sum >> 8);
}

uint8_t GridsDrumGenerator::ReadDrumMap(uint8_t step, uint8_t instrument, uint8_t x, uint8_t y)
{
    const uint8_t i = x >> 6; // 0..3
    const uint8_t j = y >> 6; // 0..3

    const uint8_t* const a_map = drum_map[i][j];
    const uint8_t* const b_map = drum_map[i + 1][j];
    const uint8_t* const c_map = drum_map[i][j + 1];
    const uint8_t* const d_map = drum_map[i + 1][j + 1];

    const uint8_t offset = static_cast<uint8_t>(instrument * 32u + step);

    const uint8_t a = a_map[offset];
    const uint8_t b = b_map[offset];
    const uint8_t c = c_map[offset];
    const uint8_t d = d_map[offset];

    return U8Mix(U8Mix(a, b, static_cast<uint8_t>(x << 2)),
                 U8Mix(c, d, static_cast<uint8_t>(x << 2)),
                 static_cast<uint8_t>(y << 2));
}

GridsStep GridsDrumGenerator::Tick(uint8_t x,
                                  uint8_t y,
                                  uint8_t density_bd,
                                  uint8_t density_sd,
                                  uint8_t density_hh,
                                  uint8_t randomness)
{
    // At the beginning of a pattern, decide on perturbation levels.
    if(step_ == 0)
    {
        const uint8_t r = static_cast<uint8_t>(randomness >> 2); // matches upstream (when swing disabled)
        perturb_[0]     = U8U8MulShift8(RandByte(), r);
        perturb_[1]     = U8U8MulShift8(RandByte(), r);
        perturb_[2]     = U8U8MulShift8(RandByte(), r);
    }

    const uint8_t densities[3] = {density_bd, density_sd, density_hh};

    bool    trig[3]   = {false, false, false};
    bool    accent[3] = {false, false, false};

    for(uint8_t part = 0; part < 3; ++part)
    {
        uint8_t level = ReadDrumMap(step_, part, x, y);

        // Apply per-part perturbation (upstream's clipping rule).
        const uint8_t p = perturb_[part];
        if(level < static_cast<uint8_t>(255u - p))
            level = static_cast<uint8_t>(level + p);
        else
            level = 255;

        const uint8_t threshold = static_cast<uint8_t>(~densities[part]);
        if(level > threshold)
        {
            trig[part] = true;
            if(level > 192)
                accent[part] = true;
        }
    }

    GridsStep s;
    s.bd        = trig[0];
    s.sd        = trig[1];
    s.hh        = trig[2];
    s.bd_accent = accent[0];
    s.sd_accent = accent[1];
    s.hh_accent = accent[2];

    // Advance step (32-step pattern).
    step_ = static_cast<uint8_t>((step_ + 1u) & 0x1Fu);

    return s;
}

} // namespace drumseq_mi::grids_port
