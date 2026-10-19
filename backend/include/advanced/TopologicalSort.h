#pragma once
// TOPOLOGICAL SORTING for an optional, acyclic manufacturing dependency graph.
//
// Edge (a, b) means "a must be done before b" (for example component a is needed to make part b). A valid
// order exists if and only if the graph has no directed cycle. The TRANSPORTATION network is NOT assumed to
// be acyclic (goods may legitimately move in loops of routes); topologicalSortOfSupplyChain exists to show that
// such a network is correctly rejected rather than silently mis-ordered.
//
// Algorithm (Kahn): repeatedly remove a node with no remaining prerequisites and "release" its dependents.
// If nodes remain but none is free, every remaining node lies on or behind a cycle. Then a DFS over the
// remaining nodes extracts one explicit cycle for the error message.
// Time O(V + E), space O(V + E). Deterministic: among free nodes the smallest id goes first.

#include <utility>
#include <vector>

#include "graph/SupplyChainGraph.h"

namespace scx {

struct TopologicalResult {
    bool acyclic = true;
    std::vector<int> order;     // valid order of all nodes when acyclic; the nodes ordered before the cycle blocked otherwise
    std::vector<int> cycle;     // when cyclic: nodes v0 -> v1 -> ... -> vk-1 -> v0 (each consecutive pair is an edge)
};

// Throws std::invalid_argument for a negative node count or an edge endpoint outside [0, nodeCount).
TopologicalResult topologicalSort(int nodeCount, const std::vector<std::pair<int, int>>& edges);

// True if `order` contains every node exactly once and respects every edge. Independent check for tests.
bool isValidTopologicalOrder(int nodeCount, const std::vector<std::pair<int, int>>& edges, const std::vector<int>& order);

// Applies the sort to the USABLE routes of a supply-chain graph (direction = shipping direction).
TopologicalResult topologicalSortOfSupplyChain(const SupplyChainGraph& graph);

}  // namespace scx
