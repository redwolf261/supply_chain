#pragma once
// Named recovery strategies. Every strategy takes the same input (the disrupted network) and returns
// a RecoveryPlan, so they can be compared on identical scenarios. All of them answer the SAME
// question - "what do we ship, and which recovery actions do we pay for?" - and are scored by the
// same objective, which is why comparing their total cost is legitimate.
//
//   dijkstra_dispatch         shortest-path dispatch, no actions         (baseline, not optimal)
//   min_cost_flow             optimal shipments on the network AS IT IS, no actions
//   greedy                    greedy actions + min-cost flow
//   bottleneck_aware          experimental actions + min-cost flow        (uses `config`)
//   bottleneck_aware_lambda0  same with the risk term switched off        (ablation)
//   exact                     exhaustive search over action subsets       (small networks only)
//
// BFS, DFS, max flow and plain Dijkstra are analysis tools, not recovery strategies, so they are not
// listed here.

#include <string>
#include <vector>

#include "algorithms/RecoveryHeuristics.h"

namespace scx {

const std::vector<std::string>& strategyNames();

// Throws std::invalid_argument for an unknown name. `exact` also throws if there are too many candidate actions.
RecoveryPlan runStrategy(const std::string& name, const SupplyChainGraph& disrupted,
                         const BottleneckConfig& config = {});

}  // namespace scx
