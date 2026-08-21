// Optional user drum-map bank.
//
// The repo ships the tool for deriving banks, not every bank worth having -
// see tools/groove_nodes/. That matters because the best corpus for a given
// person is usually one they already own and cannot redistribute: a commercial
// MIDI library, their own sequencer's patterns, a genre collection. Deriving a
// bank from those locally is fine; shipping it is not.
//
// So src/user_bank.cpp is gitignored, and the build picks it up only if it
// exists. Generate one with:
//
//     python3 tools/groove_nodes/extract.py ~/your/midi patterns.npy
//     python3 tools/groove_nodes/som.py --user
//
// and it becomes the last bank in the long-press cycle, announced as "User
// patterns".
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#if SORROW_USER_BANK

namespace daisy_grids::grids_port {
extern const uint8_t user_node_0[96];
extern const uint8_t user_node_1[96];
extern const uint8_t user_node_2[96];
extern const uint8_t user_node_3[96];
extern const uint8_t user_node_4[96];
extern const uint8_t user_node_5[96];
extern const uint8_t user_node_6[96];
extern const uint8_t user_node_7[96];
extern const uint8_t user_node_8[96];
extern const uint8_t user_node_9[96];
extern const uint8_t user_node_10[96];
extern const uint8_t user_node_11[96];
extern const uint8_t user_node_12[96];
extern const uint8_t user_node_13[96];
extern const uint8_t user_node_14[96];
extern const uint8_t user_node_15[96];
extern const uint8_t user_node_16[96];
extern const uint8_t user_node_17[96];
extern const uint8_t user_node_18[96];
extern const uint8_t user_node_19[96];
extern const uint8_t user_node_20[96];
extern const uint8_t user_node_21[96];
extern const uint8_t user_node_22[96];
extern const uint8_t user_node_23[96];
extern const uint8_t user_node_24[96];
} // namespace daisy_grids::grids_port

#endif
