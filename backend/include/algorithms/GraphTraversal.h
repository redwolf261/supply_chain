#pragma once
// Algorithm 1 (BFS) and Algorithm 2 (DFS): directed reachability over USABLE edges.
//
// Both follow edge direction only: "B is reached from A" means a chain of usable
// edges A -> ... -> B exists. That is NOT the same as undirected connectivity.
// Both run in O(V + E) time and O(V) extra space.

#include <vector>

#include "graph/SupplyChainGraph.h"

namespace scx {

struct TraversalResult {
    int source = -1;
    std::vector<int> visitOrder;        // nodes in the order they were first discovered
    std::vector<int> finishOrder;       // DFS only: nodes in post-order (empty for BFS)
    std::vector<bool> reached;          // reached[v] == true if v is reachable from source
    std::vector<int> parentNode;        // parentNode[v] = node we came from, -1 for source/unreached
    std::vector<int> parentEdge;        // parentEdge[v] = edge id used to reach v, -1 otherwise
    std::vector<int> distanceInHops;    // BFS only: fewest edges from source (-1 if unreached)
};

// Queue-based BFS. An inactive source reaches nothing (not even itself).
// Throws std::out_of_range for an invalid source id.
TraversalResult breadthFirstSearch(const SupplyChainGraph& graph, int source);

// Iterative DFS with an explicit stack (no recursion-depth limit on big networks).
// Produces the same discovery order as the textbook recursive version.
TraversalResult depthFirstSearch(const SupplyChainGraph& graph, int source);

// Nodes that were reachable from `source` in `before` but are no longer in `after`.
// Used for failure-impact analysis (what did a disruption cut off from this source?).
std::vector<int> nodesLostBetween(const TraversalResult& before, const TraversalResult& after);

}  // namespace scx
