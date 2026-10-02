#pragma once
// Algorithm 3: Dijkstra's single-source shortest path on per-unit transport cost.
//
// Finds the cheapest route for ONE unit between two facilities. It ignores edge
// capacities and other shipments, so it cannot allocate goods network-wide:
// two customers may both pick the same cheap route that cannot carry both.
// Network-wide allocation is the job of min-cost flow (Phase 3).
//
// Complexity: O((V + E) log V) with a binary-heap priority queue; O(V) space.
// Requires non-negative costs (guaranteed by SupplyChainGraph).

#include <limits>
#include <vector>

#include "graph/SupplyChainGraph.h"

namespace scx {

struct ShortestPathResult {
    static constexpr long long kUnreachable = std::numeric_limits<long long>::max();

    int source = -1;
    std::vector<long long> distance;  // total per-unit cost, kUnreachable if no usable path
    std::vector<int> parentNode;
    std::vector<int> parentEdge;

    bool isReachable(int node) const { return distance[node] != kUnreachable; }
    // Nodes from source to `target` inclusive; empty if unreachable.
    std::vector<int> nodePathTo(int target) const;
    // Edge ids from source to `target`; empty if unreachable or target == source.
    std::vector<int> edgePathTo(int target) const;
};

// Throws std::out_of_range for an invalid source. An inactive source reaches nothing.
ShortestPathResult dijkstra(const SupplyChainGraph& graph, int source);

}  // namespace scx
