#include "algorithms/SupplyChainFlow.h"

#include "algorithms/FlowNetwork.h"

namespace scx {

namespace {
// Layout shared by both algorithms: graph node i keeps index i; the super-source and super-sink
// are appended. `edgeArc[e]` maps a graph edge to its forward arc (-1 when the edge is unusable).
struct BuiltNetwork {
    FlowNetwork network;
    int superSource;
    int superSink;
    std::vector<int> edgeArc;
    std::vector<int> supplyArc;     // super-source -> node, purchased supply (-1 if none)
    std::vector<int> inventoryArc;  // super-source -> node, stock (-1 if none)
    std::vector<int> demandArc;     // customer -> super-sink (-1 if none)
    explicit BuiltNetwork(int graphNodes)
        : network(graphNodes + 2), superSource(graphNodes), superSink(graphNodes + 1) {}
};

BuiltNetwork buildNetwork(const SupplyChainGraph& graph, bool withCosts) {
    BuiltNetwork built(graph.nodeCount());
    built.edgeArc.assign(graph.edgeCount(), -1);
    built.supplyArc.assign(graph.nodeCount(), -1);
    built.inventoryArc.assign(graph.nodeCount(), -1);
    built.demandArc.assign(graph.nodeCount(), -1);

    for (const Edge& e : graph.edges()) {
        if (graph.isEdgeUsable(e.id)) {
            built.edgeArc[e.id] = built.network.addArc(e.from, e.to, e.capacity, withCosts ? e.costPerUnit : 0);
        }
    }
    for (const Node& node : graph.nodes()) {
        if (!node.active) continue;
        if (node.supply > 0) {
            built.supplyArc[node.id] = built.network.addArc(built.superSource, node.id, node.supply,
                                                            withCosts ? node.unitPurchaseCost : 0);
        }
        if (node.inventory > 0) {
            // Using stock is free, but it saves the holding cost of leaving it idle: cost -h.
            built.inventoryArc[node.id] = built.network.addArc(built.superSource, node.id, node.inventory,
                                                               withCosts ? -node.holdingCostPerUnit : 0);
        }
        if (node.type == NodeType::Customer && node.demand > 0) {
            // Delivering a unit avoids its shortage penalty p, i.e. it "earns" -p. Since
            // unmet = demand - delivered, minimising penalty = minimising (-p * delivered) + const.
            built.demandArc[node.id] = built.network.addArc(node.id, built.superSink, node.demand,
                                                            withCosts ? -node.shortagePenaltyPerUnit : 0);
        }
    }
    return built;
}

long long flowOrZero(const FlowNetwork& network, int arcId) {
    return arcId < 0 ? 0 : network.flowOnArc(arcId);
}
}  // namespace

MaxFlowResult computeMaxFlow(const SupplyChainGraph& graph) {
    BuiltNetwork built = buildNetwork(graph, /*withCosts=*/false);
    MaxFlowResult result;
    result.maxDeliverable = built.network.edmondsKarp(built.superSource, built.superSink);
    for (const Node& node : graph.nodes()) {
        if (node.type == NodeType::Customer) result.totalDemand += node.demand;
    }

    result.edgeFlow.assign(graph.edgeCount(), 0);
    for (const Edge& e : graph.edges()) result.edgeFlow[e.id] = flowOrZero(built.network, built.edgeArc[e.id]);

    // Minimum cut = arcs leaving the set of nodes still reachable from the source in the residual graph.
    const std::vector<bool> sourceSide = built.network.reachableInResidual(built.superSource);
    for (const Edge& e : graph.edges()) {
        if (built.edgeArc[e.id] >= 0 && sourceSide[e.from] && !sourceSide[e.to]) {
            result.minCutEdgeIds.push_back(e.id);
            result.minCutCapacity += e.capacity;
        }
    }
    for (const Node& node : graph.nodes()) {
        const bool hasSourceArc = built.supplyArc[node.id] >= 0 || built.inventoryArc[node.id] >= 0;
        if (hasSourceArc && !sourceSide[node.id]) {
            result.supplyLimitedNodeIds.push_back(node.id);
            if (built.supplyArc[node.id] >= 0) result.minCutCapacity += node.supply;
            if (built.inventoryArc[node.id] >= 0) result.minCutCapacity += node.inventory;
        }
        if (built.demandArc[node.id] >= 0 && sourceSide[node.id]) {
            result.demandLimitedNodeIds.push_back(node.id);
            result.minCutCapacity += node.demand;
        }
    }
    return result;
}

MinCostFlowResult solveMinCostFlow(const SupplyChainGraph& graph) {
    BuiltNetwork built = buildNetwork(graph, /*withCosts=*/true);
    // Cheapest flow over ALL flow values: stop once another unit would cost more than the penalty it avoids.
    const FlowNetwork::MinCostResult solved = built.network.minCostFlow(
        built.superSource, built.superSink, FlowNetwork::kInfinity, /*stopAtNonNegativePath=*/true);

    MinCostFlowResult result;
    result.solverObjective = solved.cost;
    result.plan = FlowSolution::empty(graph);
    for (const Edge& e : graph.edges()) result.plan.edgeFlow[e.id] = flowOrZero(built.network, built.edgeArc[e.id]);
    for (const Node& node : graph.nodes()) {
        result.plan.supplyDraw[node.id] = flowOrZero(built.network, built.supplyArc[node.id]);
        result.plan.inventoryDraw[node.id] = flowOrZero(built.network, built.inventoryArc[node.id]);
        if (node.type == NodeType::Customer) {
            // An inactive customer has no demand arc, so nothing reaches it and all its demand stays unmet.
            result.plan.unmetDemand[node.id] = node.demand - flowOrZero(built.network, built.demandArc[node.id]);
        }
    }
    result.cost = computeCost(graph, result.plan);
    return result;
}

}  // namespace scx
