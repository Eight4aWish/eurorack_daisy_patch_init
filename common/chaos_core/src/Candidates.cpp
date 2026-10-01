// Storage for the candidates in reserve (include/chaos_core/Candidates.h). Host
// tools only: the firmware builds src/Registry.cpp and nothing here.

#include "chaos_core/Candidates.h"

// Measured pitch tables, once tools/pitchmap.cpp --emit-candidates has written
// them. Until then every candidate falls back to fNatDefault, which is enough
// to measure with but not to play in tune.
#if __has_include("chaos_core/PitchTablesCandidates.h")
#include "chaos_core/PitchTablesCandidates.h"
#define CHAOS_HAVE_CANDIDATE_TABLES 1
#endif

namespace chaos_core {

    CandRikitake        candRikitake;
    CandShimizuMorioka  candShimizuMorioka;
    CandGenesioTesi     candGenesioTesi;

    ChaosBase* candidates[N_CANDIDATES] = {
        &candRikitake, &candShimizuMorioka, &candGenesioTesi,
    };

#ifdef CHAOS_HAVE_CANDIDATE_TABLES
    // Bound after construction, in definition order: the instances above are
    // constructed before this object in the same translation unit.
    static struct BindTables {
        BindTables() {
            candRikitake.pitchGrid       = &kPitchGrid_RIKITAKE;
            candShimizuMorioka.pitchGrid = &kPitchGrid_SHIMIZU_MORIOKA;
            candGenesioTesi.pitchGrid    = &kPitchGrid_GENESIO_TESI;
        }
    } bindTables_;
#endif

}  // namespace chaos_core
