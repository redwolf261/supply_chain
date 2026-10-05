#pragma once
// Small builders so test networks stay readable.
#include <string>

#include "graph/SupplyChainGraph.h"

namespace scx_test {

inline int addSupplier(scx::SupplyChainGraph& g, const std::string& name, int supply, int purchaseCost = 0) {
    scx::Node n;
    n.name = name; n.type = scx::NodeType::Supplier; n.supply = supply; n.unitPurchaseCost = purchaseCost;
    return g.addNode(n);
}
inline int addWarehouse(scx::SupplyChainGraph& g, const std::string& name, int inventory = 0, int holdingCost = 0) {
    scx::Node n;
    n.name = name; n.type = scx::NodeType::Warehouse; n.inventory = inventory; n.holdingCostPerUnit = holdingCost;
    return g.addNode(n);
}
inline int addCustomer(scx::SupplyChainGraph& g, const std::string& name, int demand, int penalty) {
    scx::Node n;
    n.name = name; n.type = scx::NodeType::Customer; n.demand = demand; n.shortagePenaltyPerUnit = penalty;
    return g.addNode(n);
}
inline int addRoute(scx::SupplyChainGraph& g, int from, int to, int capacity, int cost) {
    scx::Edge e;
    e.from = from; e.to = to; e.baseCapacity = capacity; e.costPerUnit = cost;
    return g.addEdge(e);
}

}  // namespace scx_test
