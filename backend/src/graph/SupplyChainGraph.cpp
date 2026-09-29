#include "graph/SupplyChainGraph.h"

#include <stdexcept>

namespace scx {

const char* toString(NodeType type) {
    switch (type) {
        case NodeType::Supplier: return "supplier";
        case NodeType::Factory: return "factory";
        case NodeType::Warehouse: return "warehouse";
        case NodeType::DistributionCenter: return "distribution_center";
        case NodeType::Customer: return "customer";
    }
    return "unknown";
}

NodeType nodeTypeFromString(const std::string& text) {
    for (NodeType type : {NodeType::Supplier, NodeType::Factory, NodeType::Warehouse,
                          NodeType::DistributionCenter, NodeType::Customer}) {
        if (text == toString(type)) return type;
    }
    throw std::invalid_argument("unknown node type: " + text);
}

int SupplyChainGraph::addNode(const Node& prototype) {
    if (prototype.supply < 0 || prototype.demand < 0 || prototype.inventory < 0 ||
        prototype.processingCapacity < 0 || prototype.unitPurchaseCost < 0 ||
        prototype.holdingCostPerUnit < 0 || prototype.shortagePenaltyPerUnit < 0 ||
        prototype.activationCost < 0) {
        throw std::invalid_argument("node '" + prototype.name + "' has a negative quantity");
    }
    Node stored = prototype;
    stored.id = nodeCount();
    nodes_.push_back(stored);
    outgoing_.emplace_back();
    incoming_.emplace_back();
    return stored.id;
}

int SupplyChainGraph::addEdge(const Edge& prototype) {
    if (prototype.from < 0 || prototype.from >= nodeCount() || prototype.to < 0 ||
        prototype.to >= nodeCount()) {
        throw std::invalid_argument("edge endpoint does not exist");
    }
    if (prototype.from == prototype.to) throw std::invalid_argument("self-loop edges are not allowed");
    if (prototype.baseCapacity < 0 || prototype.costPerUnit < 0 || prototype.transitTime < 0 ||
        prototype.reopenCost < 0) {
        throw std::invalid_argument("edge has a negative quantity");
    }
    Edge stored = prototype;
    stored.id = edgeCount();
    stored.capacity = stored.baseCapacity;
    edges_.push_back(stored);
    outgoing_[stored.from].push_back(stored.id);
    incoming_[stored.to].push_back(stored.id);
    return stored.id;
}

void SupplyChainGraph::checkNodeId(int nodeId) const {
    if (nodeId < 0 || nodeId >= nodeCount()) throw std::out_of_range("invalid node id " + std::to_string(nodeId));
}

void SupplyChainGraph::checkEdgeId(int edgeId) const {
    if (edgeId < 0 || edgeId >= edgeCount()) throw std::out_of_range("invalid edge id " + std::to_string(edgeId));
}

const Node& SupplyChainGraph::node(int nodeId) const { checkNodeId(nodeId); return nodes_[nodeId]; }
const Edge& SupplyChainGraph::edge(int edgeId) const { checkEdgeId(edgeId); return edges_[edgeId]; }
const std::vector<int>& SupplyChainGraph::outgoingEdgeIds(int nodeId) const { checkNodeId(nodeId); return outgoing_[nodeId]; }
const std::vector<int>& SupplyChainGraph::incomingEdgeIds(int nodeId) const { checkNodeId(nodeId); return incoming_[nodeId]; }

bool SupplyChainGraph::isEdgeUsable(int edgeId) const {
    const Edge& e = edge(edgeId);
    return e.active && e.capacity > 0 && nodes_[e.from].active && nodes_[e.to].active;
}

void SupplyChainGraph::setNodeActive(int nodeId, bool active) { checkNodeId(nodeId); nodes_[nodeId].active = active; }
void SupplyChainGraph::setEdgeActive(int edgeId, bool active) { checkEdgeId(edgeId); edges_[edgeId].active = active; }

void SupplyChainGraph::setEdgeCapacity(int edgeId, int newCapacity) {
    checkEdgeId(edgeId);
    if (newCapacity < 0 || newCapacity > edges_[edgeId].baseCapacity) {
        throw std::invalid_argument("capacity must be within [0, baseCapacity]");
    }
    edges_[edgeId].capacity = newCapacity;
}

void SupplyChainGraph::setNodeDemand(int nodeId, int newDemand) {
    checkNodeId(nodeId);
    if (newDemand < 0) throw std::invalid_argument("demand must be non-negative");
    nodes_[nodeId].demand = newDemand;
}

std::vector<std::string> SupplyChainGraph::validate() const {
    std::vector<std::string> problems;
    for (const Node& n : nodes_) {
        if (n.type == NodeType::Customer && n.supply > 0)
            problems.push_back("customer '" + n.name + "' must not have supply");
        if (n.type != NodeType::Customer && n.demand > 0)
            problems.push_back("non-customer '" + n.name + "' must not have demand");
        if (n.type == NodeType::Customer && n.shortagePenaltyPerUnit == 0 && n.demand > 0)
            problems.push_back("customer '" + n.name + "' has demand but a zero shortage penalty");
        if (n.isBackup && n.type != NodeType::Supplier)
            problems.push_back("only suppliers can be backup nodes: '" + n.name + "'");
    }
    for (const Edge& e : edges_) {
        if (e.capacity > e.baseCapacity)
            problems.push_back("edge " + std::to_string(e.id) + " capacity exceeds its base capacity");
        if (nodes_[e.from].type == NodeType::Customer && nodes_[e.to].type == NodeType::Supplier)
            problems.push_back("edge " + std::to_string(e.id) + " flows from a customer to a supplier");
    }
    return problems;
}

}  // namespace scx
