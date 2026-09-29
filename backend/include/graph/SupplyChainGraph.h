#pragma once
// Supply-chain network stored as a directed, weighted, capacitated graph.
//
// Representation: ADJACENCY LISTS. For every node we keep the ids of its
// outgoing edges. Memory is O(V + E) (an adjacency matrix would be O(V^2)),
// and visiting all neighbours of a node costs O(out-degree) instead of O(V).
//
// Identifiers are stable: a node/edge id is its index in the internal vector and
// is never reused or shifted. Disruptions never delete anything; they flip
// `active` or lower `capacity`, so the original state can always be restored.

#include <string>
#include <vector>

namespace scx {

enum class NodeType { Supplier, Factory, Warehouse, DistributionCenter, Customer };

const char* toString(NodeType type);
NodeType nodeTypeFromString(const std::string& text);  // throws std::invalid_argument

// Units: goods are counted in whole "units"; cost in currency units per unit;
// time in hours. Every quantity is a non-negative integer.
struct Node {
    int id = -1;
    std::string name;
    NodeType type = NodeType::Warehouse;
    int supply = 0;                  // units this node can draw per period (suppliers/factories)
    int demand = 0;                  // units requested (customers)
    int inventory = 0;               // units already in stock (warehouses/DCs)
    int processingCapacity = 0;      // max throughput; 0 = unlimited (reserved for later phases)
    int unitPurchaseCost = 0;        // cost per unit drawn from `supply`
    int holdingCostPerUnit = 0;      // cost per unit of inventory left unused at period end
    int shortagePenaltyPerUnit = 0;  // cost per unit of unmet demand (customers)
    int activationCost = 0;          // one-off cost to activate a backup supplier
    bool isBackup = false;           // backup suppliers start inactive and need an activation action
    bool active = true;
};

struct Edge {
    int id = -1;
    int from = -1;
    int to = -1;
    int baseCapacity = 0;  // design capacity: never changed by disruptions
    int capacity = 0;      // currently AVAILABLE capacity, 0 <= capacity <= baseCapacity
    int costPerUnit = 0;
    int transitTime = 1;   // hours
    int reopenCost = 0;    // one-off cost to reopen a failed route
    bool active = true;
};

class SupplyChainGraph {
public:
    // Both throw std::invalid_argument on negative values or bad endpoints.
    int addNode(const Node& prototype);  // `prototype.id` is ignored; the assigned id is returned
    int addEdge(const Edge& prototype);  // `prototype.id` ignored; capacity is forced to baseCapacity

    int nodeCount() const { return static_cast<int>(nodes_.size()); }
    int edgeCount() const { return static_cast<int>(edges_.size()); }
    const Node& node(int nodeId) const;  // throws std::out_of_range
    const Edge& edge(int edgeId) const;
    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<Edge>& edges() const { return edges_; }
    const std::vector<int>& outgoingEdgeIds(int nodeId) const;
    const std::vector<int>& incomingEdgeIds(int nodeId) const;

    // An edge can carry goods only if it is active, has capacity left, and both endpoints are active.
    bool isEdgeUsable(int edgeId) const;

    // Mutations used by the disruption engine.
    void setNodeActive(int nodeId, bool active);
    void setEdgeActive(int edgeId, bool active);
    void setEdgeCapacity(int edgeId, int newCapacity);  // must be within [0, baseCapacity]
    void setNodeDemand(int nodeId, int newDemand);

    // Structural check; returns human-readable problems (empty = valid).
    std::vector<std::string> validate() const;

private:
    void checkNodeId(int nodeId) const;
    void checkEdgeId(int edgeId) const;

    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::vector<std::vector<int>> outgoing_;
    std::vector<std::vector<int>> incoming_;
};

}  // namespace scx
