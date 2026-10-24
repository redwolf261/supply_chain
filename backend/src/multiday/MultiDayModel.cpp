#include "multiday/MultiDayModel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scx {

const char* toString(ForecastMode mode) {
    return mode == ForecastMode::Perfect ? "perfect" : "persistence";
}

void validateMultiDayScenario(const MultiDayScenario& s) {
    const int n = s.network.nodeCount();
    if (s.horizon < 1 || s.horizon > 365) throw std::invalid_argument("horizon must be between 1 and 365 days");
    if (static_cast<int>(s.transitDays.size()) != s.network.edgeCount()) throw std::invalid_argument("transitDays needs one entry per route");
    for (int d : s.transitDays) if (d < 0 || d > 60) throw std::invalid_argument("transit days must be between 0 and 60");
    if (static_cast<int>(s.demand.size()) != n) throw std::invalid_argument("demand needs one row per node");
    for (const Node& node : s.network.nodes()) {
        const std::vector<int>& row = s.demand[node.id];
        if (static_cast<int>(row.size()) != s.horizon) throw std::invalid_argument("demand needs one value per day");
        for (int v : row) {
            if (v < 0) throw std::invalid_argument("demand must be non-negative");
            if (v > 0 && node.type != NodeType::Customer) throw std::invalid_argument("only customers have demand");
        }
    }
    int previous = 0;
    for (const ScheduledEvent& e : s.events) {
        if (e.day < 0 || e.day >= s.horizon) throw std::invalid_argument("event day outside the horizon");
        if (e.day < previous) throw std::invalid_argument("events must be sorted by day");
        previous = e.day;
        if (e.event.type == DisruptionType::DemandSpike) throw std::invalid_argument("demand spikes belong in the demand sequence, not in the event schedule");
        const bool nodeEvent = e.event.type == DisruptionType::NodeFailure || e.event.type == DisruptionType::RestoreNode;
        if (nodeEvent && (e.event.targetId < 0 || e.event.targetId >= n)) throw std::invalid_argument("event refers to a missing node");
        if (!nodeEvent && (e.event.targetId < 0 || e.event.targetId >= s.network.edgeCount())) throw std::invalid_argument("event refers to a missing route");
        if (e.event.type == DisruptionType::CapacityReduction && (e.event.value < 0 || e.event.value > 100)) throw std::invalid_argument("capacity percent out of range");
    }
    if (!s.normalDemand.empty()) {
        if (static_cast<int>(s.normalDemand.size()) != n) throw std::invalid_argument("normal demand needs one row per node");
        for (const Node& node : s.network.nodes()) {
            const std::vector<int>& row = s.normalDemand[node.id];
            if (static_cast<int>(row.size()) != s.horizon) throw std::invalid_argument("normal demand needs one value per day");
            for (int v : row) if (v < 0 || (v > 0 && node.type != NodeType::Customer)) throw std::invalid_argument("invalid normal demand");
        }
    }
    for (const Shipment& sh : s.initialPipeline) {
        if (sh.edgeId < 0 || sh.edgeId >= s.network.edgeCount()) throw std::invalid_argument("initial shipment refers to a missing route");
        const Edge& e = s.network.edge(sh.edgeId);
        if (sh.from != e.from || sh.to != e.to) throw std::invalid_argument("initial shipment endpoints do not match its route");
        if (sh.quantity <= 0) throw std::invalid_argument("initial shipment quantity must be positive");
        if (sh.departureDay >= 0 || sh.arrivalDay < 0 || sh.arrivalDay != sh.departureDay + s.transitDays[sh.edgeId])
            throw std::invalid_argument("initial shipment must depart before day 0 and arrive after exactly its transit time");
    }
    const std::vector<std::string> problems = s.network.validate();
    if (!problems.empty()) throw std::invalid_argument("invalid network: " + problems.front());
}

std::string applyScheduledEvent(SupplyChainGraph& graph, const Disruption& event) {
    switch (event.type) {
        case DisruptionType::NodeFailure:
            if (!graph.node(event.targetId).active) return "";
            graph.setNodeActive(event.targetId, false);
            return describe(graph, event);
        case DisruptionType::RestoreNode:
            if (graph.node(event.targetId).active) return "";
            graph.setNodeActive(event.targetId, true);
            return describe(graph, event);
        case DisruptionType::RouteFailure:
            if (!graph.edge(event.targetId).active) return "";
            graph.setEdgeActive(event.targetId, false);
            return describe(graph, event);
        case DisruptionType::RestoreRoute: {
            const Edge& e = graph.edge(event.targetId);
            if (e.active && e.capacity == e.baseCapacity) return "";
            graph.setEdgeActive(event.targetId, true);
            graph.setEdgeCapacity(event.targetId, e.baseCapacity);
            return describe(graph, event);
        }
        case DisruptionType::CapacityReduction: {
            const Edge& e = graph.edge(event.targetId);
            graph.setEdgeCapacity(event.targetId, static_cast<int>(static_cast<long long>(e.baseCapacity) * event.value / 100));
            return describe(graph, event);
        }
        case DisruptionType::DemandSpike:
            break;
    }
    throw std::invalid_argument("demand spikes are not network events in the multi-day model");
}

std::vector<long long> backlogCostPerDay(const SupplyChainGraph& graph, const MultiDayConfig& config) {
    std::vector<long long> cost(graph.nodeCount(), 0);
    for (const Node& n : graph.nodes()) {
        if (n.type == NodeType::Customer) cost[n.id] = std::max(1LL, std::llround(config.backlogPenaltyFraction * n.shortagePenaltyPerUnit));
    }
    return cost;
}

}  // namespace scx
