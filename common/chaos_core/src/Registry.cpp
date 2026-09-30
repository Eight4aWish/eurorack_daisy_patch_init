// Storage for the algorithm instances and the panel-order table.

#include "chaos_core/Registry.h"

namespace chaos_core {

    ChaosRossler         algoRossler;
    ChaosVanDerPol       algoVanDerPol;
    ChaosLorenz          algoLorenz;
    ChaosChua            algoChua;
    ChaosDuffing         algoDuffing;
    ChaosCoupledRossler  algoCoupledRossler;

    ChaosBase* algos[N_ALGOS] = {
        &algoRossler, &algoVanDerPol, &algoLorenz,
        &algoChua, &algoDuffing, &algoCoupledRossler
    };

}  // namespace chaos_core
