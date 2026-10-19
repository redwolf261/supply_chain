#include "advanced/Shipments.h"

#include "advanced/MergeSort.h"

namespace scx {

std::vector<ShipmentRecord> extractShipments(const SupplyChainGraph& graph, const FlowSolution& plan) {
    std::vector<ShipmentRecord> records;
    for (const Edge& e : graph.edges()) {
        const long long flow = plan.edgeFlow[e.id];
        if (flow <= 0) continue;
        records.push_back({e.id, graph.node(e.from).name, graph.node(e.to).name, flow, flow * e.costPerUnit, e.transitTime});
    }
    return records;
}

void sortShipments(std::vector<ShipmentRecord>& records, ShipmentOrder order) {
    switch (order) {
        case ShipmentOrder::ByCostDescending:
            mergeSort(records, [](const ShipmentRecord& a, const ShipmentRecord& b) { return a.cost > b.cost; });
            break;
        case ShipmentOrder::ByQuantityDescending:
            mergeSort(records, [](const ShipmentRecord& a, const ShipmentRecord& b) { return a.quantity > b.quantity; });
            break;
        case ShipmentOrder::ByRoute:
            mergeSort(records, [](const ShipmentRecord& a, const ShipmentRecord& b) {
                return a.from != b.from ? a.from < b.from : a.to < b.to;
            });
            break;
    }
}

}  // namespace scx
