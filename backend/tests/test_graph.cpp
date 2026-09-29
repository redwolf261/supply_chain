#include <string>
#include <vector>

#include "graph/SupplyChainGraph.h"
#include "test_framework.h"

using namespace scx;

namespace {
Node makeNode(const std::string& name, NodeType type) {
    Node n;
    n.name = name;
    n.type = type;
    return n;
}
Edge makeEdge(int from, int to, int capacity, int cost) {
    Edge e;
    e.from = from;
    e.to = to;
    e.baseCapacity = capacity;
    e.costPerUnit = cost;
    return e;
}
}  // namespace

void testIdsAreStableAndSequential() {
    SupplyChainGraph g;
    SCX_CHECK_EQ(g.addNode(makeNode("S", NodeType::Supplier)), 0);
    SCX_CHECK_EQ(g.addNode(makeNode("C", NodeType::Customer)), 1);
    SCX_CHECK_EQ(g.addEdge(makeEdge(0, 1, 10, 3)), 0);
    SCX_CHECK_EQ(g.nodeCount(), 2);
    SCX_CHECK_EQ(g.edgeCount(), 1);
    SCX_CHECK_EQ(g.outgoingEdgeIds(0).size(), size_t{1});
    SCX_CHECK_EQ(g.incomingEdgeIds(1).size(), size_t{1});
}

void testRejectsInvalidInput() {
    SupplyChainGraph g;
    g.addNode(makeNode("S", NodeType::Supplier));
    g.addNode(makeNode("C", NodeType::Customer));
    Node bad = makeNode("bad", NodeType::Warehouse);
    bad.inventory = -1;
    SCX_CHECK_THROWS(g.addNode(bad), std::invalid_argument);
    SCX_CHECK_THROWS(g.addEdge(makeEdge(0, 5, 1, 1)), std::invalid_argument);   // missing endpoint
    SCX_CHECK_THROWS(g.addEdge(makeEdge(0, 0, 1, 1)), std::invalid_argument);   // self-loop
    SCX_CHECK_THROWS(g.addEdge(makeEdge(0, 1, -4, 1)), std::invalid_argument);  // negative capacity
    SCX_CHECK_THROWS(g.addEdge(makeEdge(0, 1, 4, -1)), std::invalid_argument);  // negative cost
    SCX_CHECK_THROWS(g.node(9), std::out_of_range);
    SCX_CHECK_EQ(g.edgeCount(), 0);  // nothing half-inserted
}

void testCapacityReductionKeepsBaseCapacity() {
    SupplyChainGraph g;
    g.addNode(makeNode("S", NodeType::Supplier));
    g.addNode(makeNode("C", NodeType::Customer));
    const int e = g.addEdge(makeEdge(0, 1, 10, 1));
    g.setEdgeCapacity(e, 4);
    SCX_CHECK_EQ(g.edge(e).capacity, 4);
    SCX_CHECK_EQ(g.edge(e).baseCapacity, 10);  // original is never overwritten
    SCX_CHECK_THROWS(g.setEdgeCapacity(e, 11), std::invalid_argument);
    SCX_CHECK_THROWS(g.setEdgeCapacity(e, -1), std::invalid_argument);
    g.setEdgeCapacity(e, 0);
    SCX_CHECK(!g.isEdgeUsable(e));  // zero available capacity == unusable
}

void testEdgeUsabilityDependsOnEndpoints() {
    SupplyChainGraph g;
    g.addNode(makeNode("S", NodeType::Supplier));
    g.addNode(makeNode("C", NodeType::Customer));
    const int e = g.addEdge(makeEdge(0, 1, 10, 1));
    SCX_CHECK(g.isEdgeUsable(e));
    g.setNodeActive(0, false);
    SCX_CHECK(!g.isEdgeUsable(e));
    g.setNodeActive(0, true);
    g.setEdgeActive(e, false);
    SCX_CHECK(!g.isEdgeUsable(e));
}

void testValidateFlagsStructuralProblems() {
    SupplyChainGraph g;
    Node customer = makeNode("C", NodeType::Customer);
    customer.demand = 5;  // demand but zero penalty
    g.addNode(customer);
    Node warehouse = makeNode("W", NodeType::Warehouse);
    warehouse.demand = 3;  // non-customer with demand
    g.addNode(warehouse);
    SCX_CHECK_EQ(g.validate().size(), size_t{2});

    SupplyChainGraph clean;
    clean.addNode(makeNode("S", NodeType::Supplier));
    SCX_CHECK(clean.validate().empty());
}

int main() {
    SCX_RUN(testIdsAreStableAndSequential);
    SCX_RUN(testRejectsInvalidInput);
    SCX_RUN(testCapacityReductionKeepsBaseCapacity);
    SCX_RUN(testEdgeUsabilityDependsOnEndpoints);
    SCX_RUN(testValidateFlagsStructuralProblems);
    return scxTestExitCode();
}
