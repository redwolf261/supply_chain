#include "optimization/FlowSolution.h"

namespace scx {

FlowSolution FlowSolution::empty(const SupplyChainGraph& graph) {
    FlowSolution plan;
    plan.edgeFlow.assign(graph.edgeCount(), 0);
    plan.supplyDraw.assign(graph.nodeCount(), 0);
    plan.inventoryDraw.assign(graph.nodeCount(), 0);
    plan.unmetDemand.assign(graph.nodeCount(), 0);
    for (const Node& n : graph.nodes()) {
        if (n.type == NodeType::Customer) plan.unmetDemand[n.id] = n.demand;
    }
    return plan;
}

std::vector<std::string> validateFlowSolution(const SupplyChainGraph& graph, const FlowSolution& plan) {
    std::vector<std::string> problems;
    const int n = graph.nodeCount();
    const int m = graph.edgeCount();
    if (static_cast<int>(plan.edgeFlow.size()) != m || static_cast<int>(plan.supplyDraw.size()) != n ||
        static_cast<int>(plan.inventoryDraw.size()) != n || static_cast<int>(plan.unmetDemand.size()) != n) {
        problems.push_back("plan vectors do not match the graph size");
        return problems;
    }

    std::vector<long long> inflow(n, 0), outflow(n, 0);
    for (const Edge& e : graph.edges()) {
        const long long flow = plan.edgeFlow[e.id];
        if (flow < 0) problems.push_back("edge " + std::to_string(e.id) + " has negative flow");
        const long long allowed = graph.isEdgeUsable(e.id) ? e.capacity : 0;
        if (flow > allowed) {
            problems.push_back("edge " + std::to_string(e.id) + " flow " + std::to_string(flow) +
                               " exceeds available capacity " + std::to_string(allowed));
        }
        outflow[e.from] += flow;
        inflow[e.to] += flow;
    }

    for (const Node& node : graph.nodes()) {
        const long long supply = plan.supplyDraw[node.id];
        const long long stock = plan.inventoryDraw[node.id];
        const long long unmet = plan.unmetDemand[node.id];
        const std::string who = "node '" + node.name + "'";
        const long long supplyLimit = node.active ? node.supply : 0;
        const long long inventoryLimit = node.active ? node.inventory : 0;
        if (supply < 0 || supply > supplyLimit) problems.push_back(who + " supply draw outside [0, supply]");
        if (stock < 0 || stock > inventoryLimit) problems.push_back(who + " inventory draw outside [0, inventory]");
        if (unmet < 0 || unmet > node.demand) problems.push_back(who + " unmet demand outside [0, demand]");
        if (node.type != NodeType::Customer && unmet != 0) problems.push_back(who + " is not a customer but has unmet demand");

        // Flow conservation: draws + inflow - outflow must equal what the node consumes.
        const long long consumed = (node.type == NodeType::Customer) ? node.demand - unmet : 0;
        if (supply + stock + inflow[node.id] - outflow[node.id] != consumed) {
            problems.push_back(who + " violates flow conservation");
        }
    }
    return problems;
}

CostBreakdown computeCost(const SupplyChainGraph& graph, const FlowSolution& plan) {
    CostBreakdown cost;
    for (const Edge& e : graph.edges()) cost.transport += plan.edgeFlow[e.id] * e.costPerUnit;
    for (const Node& node : graph.nodes()) {
        cost.purchase += plan.supplyDraw[node.id] * node.unitPurchaseCost;
        // Inventory is a sunk cost: drawing it is free, and only what is LEFT UNUSED is charged holding.
        if (node.active) cost.holding += (node.inventory - plan.inventoryDraw[node.id]) * node.holdingCostPerUnit;
        cost.shortagePenalty += plan.unmetDemand[node.id] * node.shortagePenaltyPerUnit;
    }
    return cost;
}

PlanMetrics computeMetrics(const SupplyChainGraph& graph, const FlowSolution& plan) {
    PlanMetrics metrics;
    long long usedCapacity = 0, availableCapacity = 0;
    for (const Node& node : graph.nodes()) {
        if (node.type != NodeType::Customer) continue;
        metrics.totalDemand += node.demand;
        metrics.unmetDemand += plan.unmetDemand[node.id];
    }
    metrics.deliveredDemand = metrics.totalDemand - metrics.unmetDemand;
    if (metrics.totalDemand > 0) {
        metrics.fulfilmentRatio = static_cast<double>(metrics.deliveredDemand) / metrics.totalDemand;
    }
    for (const Edge& e : graph.edges()) {
        metrics.totalUnitsShipped += plan.edgeFlow[e.id];
        if (graph.isEdgeUsable(e.id)) {
            availableCapacity += e.capacity;
            usedCapacity += plan.edgeFlow[e.id];
        }
    }
    if (availableCapacity > 0) metrics.networkUtilisation = static_cast<double>(usedCapacity) / availableCapacity;
    return metrics;
}

}  // namespace scx
