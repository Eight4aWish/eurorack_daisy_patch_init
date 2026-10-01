// Storage for the algorithm instances and the panel-order table.

#include "chaos_core/Registry.h"
#include "chaos_core/Bank2.h"

namespace chaos_core {

    ChaosRossler         algoRossler;
    ChaosVanDerPol       algoVanDerPol;
    ChaosLorenz          algoLorenz;
    ChaosChua            algoChua;
    ChaosDuffing         algoDuffing;
    ChaosCoupledRossler  algoCoupledRossler;

    ChaosPendulum        algoPendulum;
    ChaosUnified         algoUnified;
    ChaosMooreSpiegel    algoMooreSpiegel;
    ChaosBrusselator     algoBrusselator;
    ChaosColpitts        algoColpitts;
    ChaosHindmarshRose   algoHindmarshRose;

    ChaosBase* algos[N_ALGOS] = {
        &algoRossler, &algoVanDerPol, &algoLorenz,
        &algoChua, &algoDuffing, &algoCoupledRossler,
        &algoPendulum, &algoUnified, &algoMooreSpiegel,
        &algoBrusselator, &algoColpitts, &algoHindmarshRose,
    };

}  // namespace chaos_core
