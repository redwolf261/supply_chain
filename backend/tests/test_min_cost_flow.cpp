#include <algorithm>
#include <random>
#include <vector>

#include "algorithms/FlowNetwork.h"
#include "algorithms/SupplyChainFlow.h"
#include "optimization/FlowSolution.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;

namespace {
// Two suppliers feeding one customer directly (hand-checkable).
//   S1: supply 10, buy 1, route cap 8 cost 2  -> 3 per unit, max 8 units
//   S2: supply 10, buy 3, route cap 10 cost 1 -> 4 per unit, max 10 units
SupplyChainGraph twoSupplierNetwork(int demand, int penalty) {
    SupplyChainGraph g;
    const int s1 = addSupplier(g, "S1", 10, 1);
    const int s2 = addSupplier(g, "S2", 10, 3);
    const int c = addCustomer(g, "C", demand, penalty);
    addRoute(g, s1, c, 8, 2);
    addRoute(g, s2, c, 10, 1);
    return g;
}
}  // namespace

void testHandCheckedCheapestAllocation() {
    SupplyChainGraph g = twoSupplierNetwork(15, 100);
    MinCostFlowResult r = solveMinCostFlow(g);
    // 8 units at 3 + 7 units at 4 = 24 + 28 = 52, nothing unmet.
    SCX_CHECK_EQ(r.cost.total(), 52LL);
    SCX_CHECK_EQ(r.cost.purchase, 8LL * 1 + 7LL * 3);
    SCX_CHECK_EQ(r.cost.transport, 8LL * 2 + 7LL * 1);
    SCX_CHECK_EQ(r.plan.unmetDemand[2], 0LL);
    SCX_CHECK(validateFlowSolution(g, r.plan).empty());
}

void testShortageIsPenalisedNotDelivered() {
    SupplyChainGraph g = twoSupplierNetwork(20, 100);  // only 18 units of route capacity exist
    MinCostFlowResult r = solveMinCostFlow(g);
    // 8*3 + 10*4 = 64, plus 2 unmet * 100 = 200.
    SCX_CHECK_EQ(r.cost.total(), 264LL);
    SCX_CHECK_EQ(r.plan.unmetDemand[2], 2LL);
    SCX_CHECK_EQ(r.cost.shortagePenalty, 200LL);
    PlanMetrics m = computeMetrics(g, r.plan);
    SCX_CHECK_EQ(m.deliveredDemand, 18LL);  // the 2 short units are NOT counted as delivered
    SCX_CHECK(validateFlowSolution(g, r.plan).empty());
}

// If the penalty is cheaper than the cheapest delivery, the optimum is to ship nothing.
void testLowPenaltyMeansNoShipment() {
    SupplyChainGraph g = twoSupplierNetwork(15, 2);  // cheapest unit costs 3 > penalty 2
    MinCostFlowResult r = solveMinCostFlow(g);
    SCX_CHECK_EQ(r.cost.total(), 30LL);
    SCX_CHECK_EQ(r.plan.unmetDemand[2], 15LL);
    SCX_CHECK_EQ(r.cost.transport + r.cost.purchase, 0LL);
}

void testInventoryIsFreeAndHoldingChargedOnLeftovers() {
    SupplyChainGraph g;
    const int w = addWarehouse(g, "W", 5, 2);
    const int c = addCustomer(g, "C", 3, 50);
    addRoute(g, w, c, 10, 1);
    MinCostFlowResult r = solveMinCostFlow(g);
    SCX_CHECK_EQ(r.plan.inventoryDraw[w], 3LL);
    // transport 3*1 + holding (5-3)*2 = 7; nothing purchased.
    SCX_CHECK_EQ(r.cost.total(), 7LL);
    SCX_CHECK_EQ(r.cost.purchase, 0LL);
}

void testDisruptedSupplierIsExcluded() {
    SupplyChainGraph g = twoSupplierNetwork(15, 100);
    g.setNodeActive(0, false);  // S1 fails: only S2 (cap 10) remains
    MinCostFlowResult r = solveMinCostFlow(g);
    SCX_CHECK_EQ(r.plan.supplyDraw[0], 0LL);
    SCX_CHECK_EQ(r.plan.unmetDemand[2], 5LL);
    SCX_CHECK_EQ(r.cost.total(), 10LL * 4 + 5LL * 100);
    SCX_CHECK(validateFlowSolution(g, r.plan).empty());
}

// Min-cost flow must cancel earlier flow via a NEGATIVE-cost reverse arc.
void testNegativeReverseCostsHandled() {
    FlowNetwork net(4);  // s=0 a=1 b=2 t=3
    net.addArc(0, 1, 1, 1);
    net.addArc(1, 3, 1, 5);
    net.addArc(0, 2, 1, 5);
    net.addArc(2, 3, 1, 1);
    const int ab = net.addArc(1, 2, 1, 1);
    FlowNetwork::MinCostResult r = net.minCostFlow(0, 3, 2, false);
    SCX_CHECK_EQ(r.flow, 2LL);
    SCX_CHECK_EQ(r.cost, 12LL);  // 3 (s-a-b-t) then 9 (s-b-a-t using the reverse arc, -1 + 5 + 5)
    SCX_CHECK_EQ(net.flowOnArc(ab), 0LL);
}

// Compare the solver with exhaustive enumeration of every integer plan on small random networks.
void testAgainstBruteForceOnRandomNetworks() {
    std::mt19937 rng(20240601);  // fixed seed => reproducible
    auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    for (int trial = 0; trial < 25; ++trial) {
        SupplyChainGraph g;
        const int s0 = addSupplier(g, "S0", pick(0, 3), pick(0, 4));
        const int s1 = addSupplier(g, "S1", pick(0, 3), pick(0, 4));
        const int w = addWarehouse(g, "W", pick(0, 3), pick(0, 2));
        const int c0 = addCustomer(g, "C0", pick(1, 3), pick(1, 8));
        const int c1 = addCustomer(g, "C1", pick(1, 3), pick(1, 8));
        for (int k = 0; k < 7; ++k) {
            int from = pick(0, 4), to = pick(0, 4);
            if (from == to) to = (to + 1) % 5;
            addRoute(g, from, to, pick(1, 2), pick(0, 4));
        }

        // Exhaustive search over all edge flows and all source draws.
        long long best = -1;
        std::vector<long long> flow(g.edgeCount(), 0);
        FlowSolution candidate = FlowSolution::empty(g);
        const long long combos = [&] { long long x = 1; for (const Edge& e : g.edges()) x *= (e.capacity + 1); return x; }();
        for (long long code = 0; code < combos; ++code) {
            long long rest = code;
            std::vector<long long> inflow(5, 0), outflow(5, 0);
            for (const Edge& e : g.edges()) {
                candidate.edgeFlow[e.id] = rest % (e.capacity + 1);
                rest /= (e.capacity + 1);
                outflow[e.from] += candidate.edgeFlow[e.id];
                inflow[e.to] += candidate.edgeFlow[e.id];
            }
            for (int y0 = 0; y0 <= g.node(s0).supply; ++y0)
            for (int y1 = 0; y1 <= g.node(s1).supply; ++y1)
            for (int z = 0; z <= g.node(w).inventory; ++z) {
                candidate.supplyDraw[s0] = y0; candidate.supplyDraw[s1] = y1; candidate.inventoryDraw[w] = z;
                const long long draws[5] = {y0, y1, z, 0, 0};
                bool feasible = true;
                for (int i = 0; i < 5 && feasible; ++i) {
                    const long long net = draws[i] + inflow[i] - outflow[i];
                    if (i < 3) feasible = (net == 0);
                    else {
                        feasible = net >= 0 && net <= g.node(i).demand;
                        candidate.unmetDemand[i] = g.node(i).demand - net;
                    }
                }
                if (!feasible) continue;
                const long long total = computeCost(g, candidate).total();
                if (best < 0 || total < best) best = total;
            }
        }
        MinCostFlowResult r = solveMinCostFlow(g);
        SCX_CHECK(validateFlowSolution(g, r.plan).empty());
        SCX_CHECK_EQ(r.cost.total(), best);
        (void)c0; (void)c1;
    }
}

void testValidatorCatchesBrokenPlans() {
    SupplyChainGraph g = twoSupplierNetwork(15, 100);
    FlowSolution plan = FlowSolution::empty(g);
    SCX_CHECK(validateFlowSolution(g, plan).empty());  // shipping nothing is valid

    FlowSolution overCapacity = plan;
    overCapacity.edgeFlow[0] = 9; overCapacity.supplyDraw[0] = 9; overCapacity.unmetDemand[2] = 6;
    SCX_CHECK(!validateFlowSolution(g, overCapacity).empty());  // route cap is 8

    FlowSolution inventedGoods = plan;
    inventedGoods.edgeFlow[0] = 4; inventedGoods.unmetDemand[2] = 11;  // goods with no source
    SCX_CHECK(!validateFlowSolution(g, inventedGoods).empty());

    FlowSolution shortageAsDelivery = plan;
    shortageAsDelivery.unmetDemand[2] = 0;  // claims delivery without any flow
    SCX_CHECK(!validateFlowSolution(g, shortageAsDelivery).empty());
}

int main() {
    SCX_RUN(testHandCheckedCheapestAllocation);
    SCX_RUN(testShortageIsPenalisedNotDelivered);
    SCX_RUN(testLowPenaltyMeansNoShipment);
    SCX_RUN(testInventoryIsFreeAndHoldingChargedOnLeftovers);
    SCX_RUN(testDisruptedSupplierIsExcluded);
    SCX_RUN(testNegativeReverseCostsHandled);
    SCX_RUN(testAgainstBruteForceOnRandomNetworks);
    SCX_RUN(testValidatorCatchesBrokenPlans);
    return scxTestExitCode();
}
