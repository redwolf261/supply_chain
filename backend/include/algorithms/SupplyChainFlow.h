#pragma once
// Algorithms 5 and 6 applied to a SupplyChainGraph.
//
//   computeMaxFlow     - Edmonds-Karp: the MOST goods the network can deliver, ignoring cost.
//   solveMinCostFlow   - successive shortest paths with potentials: the CHEAPEST plan under
//                        docs/MATHEMATICAL_MODEL.md section 7 (shortage penalised, never delivered).
//
// These answer different questions: max flow says how much is possible; min-cost flow decides
// how much is worth shipping and along which routes. Only active nodes/usable edges take part.
// Recovery actions (activating backups, reopening routes) are NOT decided here: the graph is
// solved as it currently stands, so min-cost flow is optimal *for the given active set*.

#include <vector>

#include "graph/SupplyChainGraph.h"
#include "optimization/FlowSolution.h"

namespace scx {

struct MaxFlowResult {
    long long maxDeliverable = 0;            // units that can reach customers (capped by demand)
    long long totalDemand = 0;
    std::vector<long long> edgeFlow;         // one max-flow assignment, per graph edge id
    std::vector<int> minCutEdgeIds;          // saturated graph edges crossing the min cut (bottlenecks)
    std::vector<int> supplyLimitedNodeIds;   // sources whose own supply/stock is the limiting factor
    std::vector<int> demandLimitedNodeIds;   // customers whose demand is already fully met in the cut
    long long minCutCapacity = 0;            // equals maxDeliverable (max-flow min-cut theorem)
};

MaxFlowResult computeMaxFlow(const SupplyChainGraph& graph);

struct MinCostFlowResult {
    FlowSolution plan;
    CostBreakdown cost;
    long long solverObjective = 0;   // raw network objective (before adding back the constants)
};

MinCostFlowResult solveMinCostFlow(const SupplyChainGraph& graph);

}  // namespace scx
