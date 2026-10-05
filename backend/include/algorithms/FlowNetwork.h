#pragma once
// Residual-graph engine shared by Edmonds-Karp (max flow) and successive-shortest-path
// min-cost flow.
//
// RESIDUAL GRAPH: every arc u->v with capacity c gets a paired reverse arc v->u with
// capacity 0 and cost -cost. Pushing f units along an arc lowers its residual capacity
// by f and raises the reverse arc's by f, which lets later augmentations "undo" flow.
// Arcs are stored in pairs so the reverse of arc `a` is always `a ^ 1`.

#include <limits>
#include <vector>

namespace scx {

class FlowNetwork {
public:
    static constexpr long long kInfinity = std::numeric_limits<long long>::max() / 4;

    explicit FlowNetwork(int nodeCount);

    // Adds forward arc (and its hidden reverse). Returns the forward arc id.
    // Throws std::invalid_argument for a bad endpoint, negative capacity, or self-loop.
    int addArc(int from, int to, long long capacity, long long costPerUnit = 0);

    int nodeCount() const { return static_cast<int>(adjacentArcIds_.size()); }
    long long flowOnArc(int forwardArcId) const;          // = original capacity - residual
    long long residualCapacity(int arcId) const { return arcs_[arcId].residualCapacity; }
    long long originalCapacity(int forwardArcId) const { return arcs_[forwardArcId].originalCapacity; }
    int arcFrom(int forwardArcId) const { return arcs_[forwardArcId ^ 1].to; }
    int arcTo(int forwardArcId) const { return arcs_[forwardArcId].to; }

    // Edmonds-Karp: Ford-Fulkerson where every augmenting path is a BFS (fewest-edge) path.
    // O(V E^2) time. Returns the maximum flow value from source to sink.
    long long edmondsKarp(int source, int sink);

    // After edmondsKarp: true for nodes still reachable from `source` in the residual graph.
    // Arcs from a reachable node to an unreachable one are exactly the minimum cut.
    std::vector<bool> reachableInResidual(int source) const;

    struct MinCostResult {
        long long flow = 0;
        long long cost = 0;
    };
    // Successive shortest augmenting paths with node potentials.
    // Arc costs may be negative as long as there is no negative-cost cycle (checked via
    // Bellman-Ford for the initial potentials). If stopAtNonNegativePath is true we stop
    // once the cheapest path costs >= 0, i.e. we minimise cost over ALL flow values;
    // otherwise we push up to maxFlow units whatever they cost.
    MinCostResult minCostFlow(int source, int sink, long long maxFlow, bool stopAtNonNegativePath);

private:
    struct Arc {
        int to;
        long long residualCapacity;
        long long originalCapacity;
        long long cost;
    };
    void checkNode(int nodeId) const;

    std::vector<Arc> arcs_;
    std::vector<std::vector<int>> adjacentArcIds_;
};

}  // namespace scx
