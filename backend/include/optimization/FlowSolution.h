#pragma once
// A shipment plan for the single-commodity, single-period model in
// docs/MATHEMATICAL_MODEL.md, plus two checkers that do NOT depend on whichever
// algorithm produced the plan:
//   validateFlowSolution - are all constraints of section 4 satisfied?
//   computeCost          - objective of section 5, recomputed from raw numbers.
// Every optimizer's output is run through both in tests.

#include <string>
#include <vector>

#include "graph/SupplyChainGraph.h"

namespace scx {

struct FlowSolution {
    std::vector<long long> edgeFlow;        // x_e, indexed by edge id
    std::vector<long long> supplyDraw;      // y_i, indexed by node id (purchased units)
    std::vector<long long> inventoryDraw;   // z_i, indexed by node id
    std::vector<long long> unmetDemand;     // r_j, indexed by node id (0 for non-customers)

    // All-zero plan sized for `graph`, with unmet demand set to full demand.
    static FlowSolution empty(const SupplyChainGraph& graph);
};

struct CostBreakdown {
    long long purchase = 0;
    long long transport = 0;
    long long holding = 0;
    long long shortagePenalty = 0;
    long long total() const { return purchase + transport + holding + shortagePenalty; }
};

struct PlanMetrics {
    long long totalDemand = 0;
    long long unmetDemand = 0;
    long long deliveredDemand = 0;     // totalDemand - unmetDemand; shortage is never "delivered"
    double fulfilmentRatio = 1.0;      // delivered / demand (1.0 when there is no demand)
    long long totalUnitsShipped = 0;       // sum of x_e (total units moved over edges)
    double networkUtilisation = 0.0;   // sum x_e / sum available capacity over usable edges
};

// Returns human-readable violations (empty = valid).
std::vector<std::string> validateFlowSolution(const SupplyChainGraph& graph, const FlowSolution& plan);

CostBreakdown computeCost(const SupplyChainGraph& graph, const FlowSolution& plan);
PlanMetrics computeMetrics(const SupplyChainGraph& graph, const FlowSolution& plan);

}  // namespace scx
