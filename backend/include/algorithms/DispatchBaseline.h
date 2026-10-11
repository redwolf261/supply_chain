#pragma once
// Baseline: sequential shortest-path dispatch (Dijkstra applied repeatedly).
//
// Customers are served in order of decreasing shortage penalty (ties: lower id). For the current
// customer we repeatedly find the cheapest path from ANY source with stock left (path cost =
// source purchase cost + edge costs, inventory counts as free), push as many units as the path's
// remaining capacity, the source's stock and the customer's unmet demand allow, and stop serving
// that customer when the cheapest unit costs at least its shortage penalty.
//
// It never undoes an earlier shipment (no reverse arcs), so an early choice can block a better
// overall allocation. That is precisely the weakness min-cost flow removes; this baseline exists
// so the comparison "shortest path only" vs "network-wide optimisation" is real. It always
// returns a plan that satisfies every constraint of the model, but it is NOT cost-optimal.
//
// Complexity: at most (sum of demand) augmentations, each one Dijkstra: O(D (V + E) log V).

#include "optimization/FlowSolution.h"

namespace scx {

FlowSolution dispatchByShortestPaths(const SupplyChainGraph& graph);

}  // namespace scx
