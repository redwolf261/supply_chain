#include <string>
#include <vector>

#include "algorithms/Dijkstra.h"
#include "algorithms/GraphTraversal.h"
#include "test_framework.h"

using namespace scx;

namespace {
// Diamond with a cross edge, plus one isolated node:
//
//        2        1
//   0 ------> 1 ------> 2        0->2 costs 5, 1->3 costs 7, 2->3 costs 2
//   |          \         |
//   | 5         \ 7      | 2      node 4 is isolated
//   v            v       v
//   2 ---------> 3 <-----+
//
// Cheapest 0->3 is 0-1-2-3 = 2+1+2 = 5 (beats 0-2-3 = 7 and 0-1-3 = 9).
struct Diamond {
    SupplyChainGraph graph;
    int e01, e02, e12, e13, e23;
    Diamond() {
        for (int i = 0; i < 5; ++i) {
            Node n;
            n.name = "n" + std::to_string(i);
            n.type = (i == 0) ? NodeType::Supplier : (i == 3 ? NodeType::Customer : NodeType::Warehouse);
            graph.addNode(n);
        }
        auto add = [&](int from, int to, int cost) {
            Edge e;
            e.from = from; e.to = to; e.baseCapacity = 10; e.costPerUnit = cost;
            return graph.addEdge(e);
        };
        e01 = add(0, 1, 2);
        e02 = add(0, 2, 5);
        e12 = add(1, 2, 1);
        e13 = add(1, 3, 7);
        e23 = add(2, 3, 2);
    }
};
}  // namespace

void testBfsOrderAndHopDistances() {
    Diamond d;
    TraversalResult r = breadthFirstSearch(d.graph, 0);
    SCX_CHECK((r.visitOrder == std::vector<int>{0, 1, 2, 3}));
    SCX_CHECK_EQ(r.distanceInHops[3], 2);
    SCX_CHECK(!r.reached[4]);  // isolated node
    SCX_CHECK_EQ(r.distanceInHops[4], -1);
}

void testDfsOrderAndFinishOrder() {
    Diamond d;
    TraversalResult r = depthFirstSearch(d.graph, 0);
    SCX_CHECK((r.visitOrder == std::vector<int>{0, 1, 2, 3}));   // goes deep along 0->1->2->3
    SCX_CHECK((r.finishOrder == std::vector<int>{3, 2, 1, 0}));  // post-order
    SCX_CHECK(!r.reached[4]);
}

void testReachabilityFollowsEdgeDirection() {
    Diamond d;
    // Starting at the customer: nothing leaves node 3, so only node 3 itself is reached.
    SCX_CHECK_EQ(breadthFirstSearch(d.graph, 3).visitOrder.size(), size_t{1});
    SCX_CHECK_EQ(depthFirstSearch(d.graph, 3).visitOrder.size(), size_t{1});
}

void testTraversalSkipsDisabledEdgesAndNodes() {
    Diamond d;
    d.graph.setNodeActive(2, false);  // node 2 down: 0-1-3 is the only way to the customer
    TraversalResult bfs = breadthFirstSearch(d.graph, 0);
    SCX_CHECK(bfs.reached[3] && !bfs.reached[2]);
    SCX_CHECK_EQ(bfs.parentEdge[3], d.e13);

    d.graph.setEdgeActive(d.e13, false);  // now the customer is cut off
    SCX_CHECK(!breadthFirstSearch(d.graph, 0).reached[3]);
    SCX_CHECK(!depthFirstSearch(d.graph, 0).reached[3]);

    d.graph.setNodeActive(0, false);  // inactive source reaches nothing
    SCX_CHECK(breadthFirstSearch(d.graph, 0).visitOrder.empty());
    SCX_CHECK(depthFirstSearch(d.graph, 0).visitOrder.empty());
}

void testNodesLostBetween() {
    Diamond d;
    TraversalResult before = breadthFirstSearch(d.graph, 0);
    d.graph.setEdgeActive(d.e01, false);
    d.graph.setEdgeActive(d.e02, false);
    TraversalResult after = breadthFirstSearch(d.graph, 0);
    SCX_CHECK((nodesLostBetween(before, after) == std::vector<int>{1, 2, 3}));
}

void testInvalidSourceThrows() {
    Diamond d;
    SCX_CHECK_THROWS(breadthFirstSearch(d.graph, 99), std::out_of_range);
    SCX_CHECK_THROWS(depthFirstSearch(d.graph, -1), std::out_of_range);
    SCX_CHECK_THROWS(dijkstra(d.graph, 99), std::out_of_range);
}

void testDijkstraFindsCheapestPath() {
    Diamond d;
    ShortestPathResult r = dijkstra(d.graph, 0);
    SCX_CHECK_EQ(r.distance[3], 5LL);
    SCX_CHECK((r.nodePathTo(3) == std::vector<int>{0, 1, 2, 3}));
    SCX_CHECK((r.edgePathTo(3) == std::vector<int>{d.e01, d.e12, d.e23}));
    SCX_CHECK_EQ(r.distance[0], 0LL);
    SCX_CHECK((r.edgePathTo(0).empty()));
}

void testDijkstraHandlesUnreachableAndDisabledRoutes() {
    Diamond d;
    ShortestPathResult r = dijkstra(d.graph, 0);
    SCX_CHECK(!r.isReachable(4));
    SCX_CHECK(r.nodePathTo(4).empty());

    d.graph.setEdgeActive(d.e12, false);  // cheap shortcut closed -> next best is 0-2-3 = 7
    SCX_CHECK_EQ(dijkstra(d.graph, 0).distance[3], 7LL);
    d.graph.setEdgeCapacity(d.e02, 0);    // zero capacity -> unusable -> 0-1-3 = 9
    SCX_CHECK_EQ(dijkstra(d.graph, 0).distance[3], 9LL);
}

// Regression-style check of the "stale heap entry" logic: a longer path discovered
// first must be replaced once a cheaper one is found.
void testDijkstraRelaxationReplacesWorseDistance() {
    Diamond d;
    ShortestPathResult r = dijkstra(d.graph, 0);
    SCX_CHECK_EQ(r.distance[2], 3LL);  // via 0-1-2 (2+1), not direct 0->2 (5)
    SCX_CHECK_EQ(r.parentNode[2], 1);
}

int main() {
    SCX_RUN(testBfsOrderAndHopDistances);
    SCX_RUN(testDfsOrderAndFinishOrder);
    SCX_RUN(testReachabilityFollowsEdgeDirection);
    SCX_RUN(testTraversalSkipsDisabledEdgesAndNodes);
    SCX_RUN(testNodesLostBetween);
    SCX_RUN(testInvalidSourceThrows);
    SCX_RUN(testDijkstraFindsCheapestPath);
    SCX_RUN(testDijkstraHandlesUnreachableAndDisabledRoutes);
    SCX_RUN(testDijkstraRelaxationReplacesWorseDistance);
    return scxTestExitCode();
}
