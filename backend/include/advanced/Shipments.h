#pragma once
// Shipment records derived from a plan, and ways to order them for reports (uses merge sort).

#include <string>
#include <vector>

#include "optimization/FlowSolution.h"

namespace scx {

struct ShipmentRecord {
    int edgeId = -1;
    std::string from;
    std::string to;
    long long quantity = 0;     // units shipped on the route
    long long cost = 0;         // transport cost of this shipment (quantity * cost per unit)
    int transitHours = 0;
};

enum class ShipmentOrder { ByCostDescending, ByQuantityDescending, ByRoute };

// One record per route with positive flow, in edge-id order.
std::vector<ShipmentRecord> extractShipments(const SupplyChainGraph& graph, const FlowSolution& plan);

// Sorts with scx::mergeSort. Ties keep their edge-id order (stable).
void sortShipments(std::vector<ShipmentRecord>& records, ShipmentOrder order);

}  // namespace scx
