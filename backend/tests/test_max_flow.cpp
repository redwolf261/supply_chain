#include <algorithm>
#include <vector>

#include "algorithms/FlowNetwork.h"
#include "algorithms/SupplyChainFlow.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;

// The classic textbook network (CLRS, Fig. 26.1): maximum flow is 23.
void testEdmondsKarpTextbookNetwork() {
    FlowNetwork net(6);  // s=0, v1..v4 = 1..4, t=5
    net.addArc(0, 1, 16); net.addArc(0, 2, 13); net.addArc(1, 2, 10); net.addArc(2, 1, 4);
    net.addArc(1, 3, 12); net.addArc(3, 2, 9);  net.addArc(2, 4, 14); net.addArc(4, 3, 7);
    const int a35 = net.addArc(3, 5, 20);
    const int a45 = net.addArc(4, 5, 4);
    SCX_CHECK_EQ(net.edmondsKarp(0, 5), 23LL);
    SCX_CHECK_EQ(net.flowOnArc(a35) + net.flowOnArc(a45), 23LL);  // everything arrives at the sink
}

void testZeroCapacityAndDisconnected() {
    FlowNetwork zero(2);
    zero.addArc(0, 1, 0);
    SCX_CHECK_EQ(zero.edmondsKarp(0, 1), 0LL);

    FlowNetwork disconnected(4);
    disconnected.addArc(0, 1, 5);
    disconnected.addArc(2, 3, 5);
    SCX_CHECK_EQ(disconnected.edmondsKarp(0, 3), 0LL);
}

// BFS first picks s-a-b-t (3 hops, uses a->b). The true maximum (2) needs the second path
// s-x-b-(undo a->b)-a-y-t, which only exists through the REVERSE residual arc b->a.
void testReverseResidualArcIsUsed() {
    FlowNetwork net(6);  // s=0 a=1 b=2 t=3 x=4 y=5
    net.addArc(0, 1, 1);               // s->a
    const int ab = net.addArc(1, 2, 1);  // a->b
    net.addArc(2, 3, 1);               // b->t
    net.addArc(0, 4, 1);               // s->x
    net.addArc(4, 2, 1);               // x->b
    net.addArc(1, 5, 1);               // a->y
    net.addArc(5, 3, 1);               // y->t
    SCX_CHECK_EQ(net.edmondsKarp(0, 3), 2LL);
    SCX_CHECK_EQ(net.flowOnArc(ab), 0LL);  // the middle edge's flow was cancelled
}

void testRejectsBadArcs() {
    FlowNetwork net(2);
    SCX_CHECK_THROWS(net.addArc(0, 5, 1), std::invalid_argument);
    SCX_CHECK_THROWS(net.addArc(0, 0, 1), std::invalid_argument);
    SCX_CHECK_THROWS(net.addArc(0, 1, -1), std::invalid_argument);
    SCX_CHECK_THROWS(net.edmondsKarp(0, 0), std::invalid_argument);
}

void testSupplyChainMaxFlowAndMinCut() {
    SupplyChainGraph g;
    const int s = addSupplier(g, "S", 10);
    const int w1 = addWarehouse(g, "W1");
    const int w2 = addWarehouse(g, "W2");
    const int c = addCustomer(g, "C", 20, 9);
    const int e1 = addRoute(g, s, w1, 4, 1);
    const int e2 = addRoute(g, s, w2, 3, 1);
    addRoute(g, w1, c, 5, 1);
    addRoute(g, w2, c, 5, 1);

    MaxFlowResult r = computeMaxFlow(g);
    SCX_CHECK_EQ(r.maxDeliverable, 7LL);  // limited by the two out-routes of S (4 + 3), not by supply 10
    SCX_CHECK_EQ(r.minCutCapacity, 7LL);  // max-flow min-cut theorem
    SCX_CHECK((r.minCutEdgeIds == std::vector<int>{e1, e2}));
    SCX_CHECK(r.supplyLimitedNodeIds.empty());  // S is on the source side: supply is NOT the bottleneck
    SCX_CHECK_EQ(r.totalDemand, 20LL);
    (void)c;
}

void testMaxFlowLimitedBySupplyOrDemand() {
    SupplyChainGraph g;
    const int s = addSupplier(g, "S", 3);
    const int c = addCustomer(g, "C", 8, 9);
    addRoute(g, s, c, 100, 1);
    MaxFlowResult supplyBound = computeMaxFlow(g);
    SCX_CHECK_EQ(supplyBound.maxDeliverable, 3LL);
    SCX_CHECK((supplyBound.supplyLimitedNodeIds == std::vector<int>{s}));
    SCX_CHECK_EQ(supplyBound.minCutCapacity, 3LL);

    g.setNodeDemand(c, 2);
    MaxFlowResult demandBound = computeMaxFlow(g);
    SCX_CHECK_EQ(demandBound.maxDeliverable, 2LL);
    SCX_CHECK((demandBound.demandLimitedNodeIds == std::vector<int>{c}));
    SCX_CHECK_EQ(demandBound.minCutCapacity, 2LL);
}

void testMaxFlowRespectsDisruptions() {
    SupplyChainGraph g;
    const int s = addSupplier(g, "S", 10);
    const int w = addWarehouse(g, "W");
    const int c = addCustomer(g, "C", 10, 9);
    const int e1 = addRoute(g, s, w, 6, 1);
    addRoute(g, w, c, 6, 1);
    SCX_CHECK_EQ(computeMaxFlow(g).maxDeliverable, 6LL);
    g.setEdgeCapacity(e1, 2);  // capacity reduction
    SCX_CHECK_EQ(computeMaxFlow(g).maxDeliverable, 2LL);
    g.setEdgeActive(e1, false);  // route failure
    SCX_CHECK_EQ(computeMaxFlow(g).maxDeliverable, 0LL);
    g.setEdgeActive(e1, true);
    g.setEdgeCapacity(e1, 6);
    g.setNodeActive(s, false);  // supplier failure
    SCX_CHECK_EQ(computeMaxFlow(g).maxDeliverable, 0LL);
    (void)c;
}

int main() {
    SCX_RUN(testEdmondsKarpTextbookNetwork);
    SCX_RUN(testZeroCapacityAndDisconnected);
    SCX_RUN(testReverseResidualArcIsUsed);
    SCX_RUN(testRejectsBadArcs);
    SCX_RUN(testSupplyChainMaxFlowAndMinCut);
    SCX_RUN(testMaxFlowLimitedBySupplyOrDemand);
    SCX_RUN(testMaxFlowRespectsDisruptions);
    return scxTestExitCode();
}
