#pragma once
// The shipping algorithm set, in panel order: bank 1 (Attractors.h), then bank 2
// (Bank2.h). Defined once in Registry.cpp so every platform cycles the same list
// without redeclaring the instances.

#include <stdint.h>
#include "chaos_core/Attractors.h"

namespace chaos_core {

// Banks of six, one model per LED ring (docs/SECRET.md). Each bank's pitch tables
// are generated into a file of their own, so measuring one bank never touches
// another's: pitchmap --emit writes PitchTables.h (bank 1), --emit-bank2
// PitchTablesBank2.h.
constexpr uint8_t kBankSize = 6;
constexpr uint8_t N_ALGOS   = 2 * kBankSize;
extern ChaosBase* algos[N_ALGOS];

}  // namespace chaos_core
