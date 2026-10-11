#include "simulation/Disruption.h"

#include <stdexcept>

namespace scx {

const char* toString(DisruptionType type) {
    switch (type) {
        case DisruptionType::NodeFailure: return "node_failure";
        case DisruptionType::RouteFailure: return "route_failure";
        case DisruptionType::CapacityReduction: return "capacity_reduction";
        case DisruptionType::DemandSpike: return "demand_spike";
        case DisruptionType::RestoreNode: return "restore_node";
        case DisruptionType::RestoreRoute: return "restore_route";
    }
    return "unknown";
}

void applyDisruption(SupplyChainGraph& graph, const Disruption& event) {
    switch (event.type) {
        case DisruptionType::NodeFailure: {
            const Node& node = graph.node(event.targetId);  // throws out_of_range for a bad id
            if (!node.active) throw std::invalid_argument("node '" + node.name + "' is already inactive");
            graph.setNodeActive(event.targetId, false);
            return;
        }
        case DisruptionType::RestoreNode: {
            const Node& node = graph.node(event.targetId);
            if (node.active) throw std::invalid_argument("node '" + node.name + "' is not failed");
            if (node.isBackup) {
                throw std::invalid_argument("backup supplier '" + node.name +
                                            "' must be brought online by a recovery action, not restored");
            }
            graph.setNodeActive(event.targetId, true);
            return;
        }
        case DisruptionType::RouteFailure: {
            if (!graph.edge(event.targetId).active) throw std::invalid_argument("route is already inactive");
            graph.setEdgeActive(event.targetId, false);
            return;
        }
        case DisruptionType::RestoreRoute: {
            const Edge& edge = graph.edge(event.targetId);
            if (edge.active && edge.capacity == edge.baseCapacity) throw std::invalid_argument("route is not failed or reduced");
            graph.setEdgeActive(event.targetId, true);
            graph.setEdgeCapacity(event.targetId, edge.baseCapacity);
            return;
        }
        case DisruptionType::CapacityReduction: {
            const Edge& edge = graph.edge(event.targetId);
            if (event.value < 0 || event.value > 100) throw std::invalid_argument("remaining capacity percent must be in [0, 100]");
            // Always measured against the design capacity, so repeating the event is idempotent.
            graph.setEdgeCapacity(event.targetId, static_cast<int>(static_cast<long long>(edge.baseCapacity) * event.value / 100));
            return;
        }
        case DisruptionType::DemandSpike: {
            const Node& node = graph.node(event.targetId);
            if (node.type != NodeType::Customer) throw std::invalid_argument("demand spikes apply to customers only");
            if (event.value < 0) throw std::invalid_argument("demand increase percent must be non-negative");
            const long long grown = (static_cast<long long>(node.demand) * (100 + event.value) + 50) / 100;  // round half up
            if (grown > 1000000000LL) throw std::invalid_argument("resulting demand is too large");
            graph.setNodeDemand(event.targetId, static_cast<int>(grown));
            return;
        }
    }
    throw std::invalid_argument("unknown disruption type");
}

std::string describe(const SupplyChainGraph& graph, const Disruption& event) {
    auto edgeLabel = [&](int id) {
        const Edge& e = graph.edge(id);
        return graph.node(e.from).name + " -> " + graph.node(e.to).name;
    };
    switch (event.type) {
        case DisruptionType::NodeFailure: return std::string(toString(graph.node(event.targetId).type)) + " " + graph.node(event.targetId).name + " fails";
        case DisruptionType::RestoreNode: return graph.node(event.targetId).name + " is restored";
        case DisruptionType::RouteFailure: return "route " + edgeLabel(event.targetId) + " fails";
        case DisruptionType::RestoreRoute: return "route " + edgeLabel(event.targetId) + " is restored";
        case DisruptionType::CapacityReduction:
            return "route " + edgeLabel(event.targetId) + " drops to " + std::to_string(event.value) + "% capacity";
        case DisruptionType::DemandSpike:
            return "demand at " + graph.node(event.targetId).name + " rises " + std::to_string(event.value) + "%";
    }
    return "unknown event";
}

}  // namespace scx
