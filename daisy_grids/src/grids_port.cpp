// Minimal, portable port of the Mutable Instruments Grids drum pattern
// generator — the implementation for grids_port.h.
//
// Derived from deps/mutable/eurorack/grids/pattern_generator.{h,cc}: the drum
// map layout, the Galois LFSR, the fixed-point helpers and the step logic are
// Emilie Gillet's. The port to portable C++ is by David Baghurst.
//
// Original author: Emilie Gillet (Mutable Instruments)
// License: GPL-3.0-or-later — see daisy_grids/LICENSE. Because this file is
// derived from Grids, the whole of daisy_grids (Sorrow) is copyleft and cannot
// be relicensed; distributing it, source or binary, obliges you to pass on the
// complete corresponding source.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "grids_port.h"

#include "grids_nodes.h"
#include "grids_nodes_club.h"
#include "user_bank.h"
#include "grids_nodes_groove.h"
#include "grids_nodes_jazzlatin.h"

namespace daisy_grids::grids_port {

namespace {
// This mapping matches the upstream Grids drum_map[][] pointer table. The order
// is deliberately not sequential: neighbouring cells are musically related, and
// X/Y interpolates between adjacent cells, so the arrangement is as much a part
// of the design as the patterns.
const uint8_t* const drum_map_grids[5][5] = {
    {node_10, node_8, node_0, node_9, node_11},
    {node_15, node_7, node_13, node_12, node_6},
    {node_18, node_14, node_4, node_5, node_3},
    {node_23, node_16, node_21, node_1, node_2},
    {node_24, node_19, node_17, node_20, node_22},
};

// The Groove bank needs no such table: it comes off a self-organising map, so
// the grid position *is* the arrangement and row-major order is already
// topology-preserving.
const uint8_t* const drum_map_groove[5][5] = {
    {groove_node_0, groove_node_1, groove_node_2, groove_node_3, groove_node_4},
    {groove_node_5, groove_node_6, groove_node_7, groove_node_8, groove_node_9},
    {groove_node_10, groove_node_11, groove_node_12, groove_node_13, groove_node_14},
    {groove_node_15, groove_node_16, groove_node_17, groove_node_18, groove_node_19},
    {groove_node_20, groove_node_21, groove_node_22, groove_node_23, groove_node_24},
};

// Same story as the Groove bank: off a self-organising map, so row-major order
// is already the arrangement.
const uint8_t* const drum_map_club[5][5] = {
    {club_node_0, club_node_1, club_node_2, club_node_3, club_node_4},
    {club_node_5, club_node_6, club_node_7, club_node_8, club_node_9},
    {club_node_10, club_node_11, club_node_12, club_node_13, club_node_14},
    {club_node_15, club_node_16, club_node_17, club_node_18, club_node_19},
    {club_node_20, club_node_21, club_node_22, club_node_23, club_node_24},
};

// Same story again: off a self-organising map, so row-major order is already
// the arrangement. Announced as "Latin"; the corpus is wider than that.
const uint8_t* const drum_map_jazzlatin[5][5] = {
    {jazzlatin_node_0, jazzlatin_node_1, jazzlatin_node_2, jazzlatin_node_3, jazzlatin_node_4},
    {jazzlatin_node_5, jazzlatin_node_6, jazzlatin_node_7, jazzlatin_node_8, jazzlatin_node_9},
    {jazzlatin_node_10, jazzlatin_node_11, jazzlatin_node_12, jazzlatin_node_13, jazzlatin_node_14},
    {jazzlatin_node_15, jazzlatin_node_16, jazzlatin_node_17, jazzlatin_node_18, jazzlatin_node_19},
    {jazzlatin_node_20, jazzlatin_node_21, jazzlatin_node_22, jazzlatin_node_23, jazzlatin_node_24},
};

using MapRow = const uint8_t* const (*)[5];
// The user bank's 5x5 of pointers is built at runtime into the loaded buffer.
const uint8_t* drum_map_user[5][5] = {};

const MapRow kFactoryBanks[kNumFactoryBanks]
    = {drum_map_grids, drum_map_club, drum_map_groove, drum_map_jazzlatin};

uint8_t s_bank       = 0;
bool    s_have_user  = false;
} // namespace

void SetUserBank(const uint8_t* nodes)
{
    if(nodes == nullptr)
    {
        s_have_user = false;
        if(s_bank >= kNumFactoryBanks)
            s_bank = 0;
        return;
    }
    for(uint8_t r = 0; r < 5; ++r)
        for(uint8_t c = 0; c < 5; ++c)
            drum_map_user[r][c] = nodes + (r * 5 + c) * 96;
    s_have_user = true;
}

uint8_t BankCount()
{
    return s_have_user ? kMaxBanks : kNumFactoryBanks;
}

void SetBank(uint8_t bank)
{
    s_bank = bank < BankCount() ? bank : 0;
}

uint8_t GetBank()
{
    return s_bank;
}

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

    const MapRow map = (s_bank < kNumFactoryBanks)
                           ? kFactoryBanks[s_bank]
                           : static_cast<MapRow>(drum_map_user);
    const uint8_t* const a_map = map[i][j];
    const uint8_t* const b_map = map[i + 1][j];
    const uint8_t* const c_map = map[i][j + 1];
    const uint8_t* const d_map = map[i + 1][j + 1];

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

} // namespace daisy_grids::grids_port
