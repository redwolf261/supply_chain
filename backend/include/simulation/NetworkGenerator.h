#pragma once
// Reproducible network and disruption generation. The same parameters and seed always give the same
// output: randomness comes from std::mt19937 with a hand-written range mapping (not
// std::uniform_int_distribution, whose output may differ between standard libraries).
//
// Generated networks are layered: suppliers (regular + backup) -> warehouses -> distribution centers
// -> customers, and every node is guaranteed at least one connection to the next layer / from the
// previous one, so nothing is born isolated.

#include <cstdint>
#include <string>
#include <vector>

#include "simulation/ScenarioIO.h"

namespace scx {

struct GeneratorParams {
    int suppliers = 4;
    int backupSuppliers = 2;
    int warehouses = 3;
    int distributionCenters = 3;
    int customers = 6;
    int edges = 0;                  // total edge target; 0 = a sparse default (minimum connectivity + about half again)
    double capacityFactor = 1.2;    // initial scale of supply and route capacity relative to total demand (bigger = looser).
                                    // The generator raises it just enough that the HEALTHY network can serve all demand.
    int demandMin = 20;
    int demandMax = 60;
    std::uint64_t seed = 1;
};

// Throws std::invalid_argument for non-positive layer sizes, an edge target that cannot connect the
// layers, or a non-positive capacity factor.
Scenario generateNetwork(const GeneratorParams& params);

// severity in [0, 1]: share of regular suppliers failed, plus route failures, capacity cuts and demand
// spikes proportional to severity. The returned list is valid when applied in order to `graph`.
std::vector<Disruption> generateDisruptions(const SupplyChainGraph& graph, double severity, std::uint64_t seed);

const std::vector<std::string>& standardScenarioNames();
GeneratorParams stressTestParams();

// Builds one of the eight standard scenarios on a seeded generated network:
//   normal_operations, supplier_failure, route_failure, capacity_reduction, demand_spike,
//   multiple_disruptions, recovery_after_failure, stress_test (larger, tighter, severity 0.5).
// Throws std::invalid_argument for an unknown name.
Scenario buildStandardScenario(const std::string& name, std::uint64_t seed = 1);

}  // namespace scx
