#pragma once
// The models a host tool works over: the shipping six (Registry.h), plus the
// bank-2 candidates when the tool is built with -DCHAOS_CANDIDATES and
// src/Candidates.cpp. Indices 0..N_ALGOS-1 are the shipping six, so every
// existing command line means what it did; candidates follow from N_ALGOS.

#include "chaos_core/Registry.h"
#ifdef CHAOS_CANDIDATES
#include "chaos_core/Candidates.h"
#endif

namespace chaos_core {

#ifdef CHAOS_CANDIDATES
    inline int nModels() { return N_ALGOS + N_CANDIDATES; }
    inline ChaosBase* model(int i) { return i < N_ALGOS ? algos[i] : candidates[i - N_ALGOS]; }
#else
    inline int nModels() { return N_ALGOS; }
    inline ChaosBase* model(int i) { return algos[i]; }
#endif

}  // namespace chaos_core
