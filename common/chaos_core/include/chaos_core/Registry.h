#pragma once
// The shipping algorithm set, in panel order. Defined once in Registry.cpp so
// every platform cycles the same list without redeclaring the instances.

#include <stdint.h>
#include "chaos_core/Attractors.h"

namespace chaos_core {

constexpr uint8_t N_ALGOS = 6;
extern ChaosBase* algos[N_ALGOS];

}  // namespace chaos_core
