#include <random>
#include <vector>

#include "advanced/InventoryDP.h"
#include "test_framework.h"

using namespace scx;

namespace {
// Hand-worked example (see docs/ALGORITHMS.md): 3 days, demand 2, 3, 2; buy at 2 per unit with a setup cost of 5
// per order; holding 1 per unit per night; lost sales cost 10 each; room for 5 units; orders of up to 5 units.
InventoryProblem workedExample() {
    InventoryProblem p;
    p.demand = {2, 3, 2};
    p.unitCost = {2, 2, 2};
    p.setupCost = 5;
    p.holdingCost = 1;
    p.shortagePenalty = 10;
    p.maxInventory = 5;
    p.maxOrder = 5;
    p.initialInventory = 0;
    return p;
}
}  // namespace

void testWorkedExampleMatchesHandCalculation() {
    const InventoryProblem p = workedExample();
    const InventoryPlan plan = solveInventoryDP(p);
    // Day 0: order 2 (5 + 4 = 9). Day 1: order 5 (5 + 10 = 15), serve 3, carry 2 (holding 2). Day 2: serve 2 from stock.
    SCX_CHECK_EQ(plan.totalCost, 26LL);
    SCX_CHECK((plan.order == std::vector<int>{2, 5, 0}));
    SCX_CHECK((plan.endInventory == std::vector<int>{0, 2, 0}));
    SCX_CHECK((plan.lost == std::vector<int>{0, 0, 0}));

    // Spot checks of the DP table: base row is zero; with 2 units on hand on the last day nothing is needed;
    // with 0 on hand the last day orders 2 (5 + 4 = 9); with 1 on hand it orders 1 (5 + 2 = 7).
    SCX_CHECK_EQ(plan.costToGo.size(), size_t{4});
    SCX_CHECK_EQ(plan.costToGo[0].size(), size_t{6});
    for (long long v : plan.costToGo[3]) SCX_CHECK_EQ(v, 0LL);
    SCX_CHECK_EQ(plan.costToGo[2][2], 0LL);
    SCX_CHECK_EQ(plan.costToGo[2][0], 9LL);
    SCX_CHECK_EQ(plan.costToGo[2][1], 7LL);
    SCX_CHECK_EQ(plan.costToGo[0][0], 26LL);
}

void testPlanningAheadBeatsLotForLot() {
    const InventoryProblem p = workedExample();
    const std::vector<int> naive = lotForLotOrders(p);
    SCX_CHECK((naive == std::vector<int>{2, 3, 2}));
    SCX_CHECK_EQ(evaluateInventoryPolicy(p, naive), 29LL);      // three setup costs
    SCX_CHECK(evaluateInventoryPolicy(p, naive) > solveInventoryDP(p).totalCost);
}

void testPolicyEvaluatorIsIndependentAndStrict() {
    const InventoryProblem p = workedExample();
    SCX_CHECK_EQ(evaluateInventoryPolicy(p, {2, 5, 0}), 26LL);
    SCX_CHECK_THROWS(evaluateInventoryPolicy(p, {6, 0, 0}), std::invalid_argument);     // above maxOrder
    SCX_CHECK_THROWS(evaluateInventoryPolicy(p, {5, 5, 0}), std::invalid_argument);     // would overfill the warehouse: 3 + 5 > 5
    SCX_CHECK_THROWS(evaluateInventoryPolicy(p, {1, 1}), std::invalid_argument);        // wrong length
    SCX_CHECK_THROWS(evaluateInventoryPolicy(p, {-1, 0, 0}), std::invalid_argument);
}

// The DP must agree with exhaustive search of every order sequence, and the plan it returns must really cost what it claims.
void testAgainstBruteForceOnRandomProblems() {
    std::mt19937 rng(4242);
    auto pick = [&](int lo, int hi) { return static_cast<int>(lo + rng() % static_cast<unsigned>(hi - lo + 1)); };
    for (int trial = 0; trial < 80; ++trial) {
        InventoryProblem p;
        const int days = pick(1, 5);
        for (int t = 0; t < days; ++t) { p.demand.push_back(pick(0, 5)); p.unitCost.push_back(pick(0, 4)); }
        p.setupCost = pick(0, 8);
        p.holdingCost = pick(0, 3);
        p.shortagePenalty = pick(0, 12);
        p.maxInventory = pick(0, 6);
        p.maxOrder = pick(0, 4);
        p.initialInventory = pick(0, p.maxInventory);

        const InventoryPlan dp = solveInventoryDP(p);
        const InventoryPlan brute = solveInventoryBruteForce(p);
        SCX_CHECK_EQ(dp.totalCost, brute.totalCost);
        SCX_CHECK_EQ(evaluateInventoryPolicy(p, dp.order), dp.totalCost);          // the reconstructed policy achieves the value
        SCX_CHECK(dp.totalCost <= evaluateInventoryPolicy(p, lotForLotOrders(p))); // never worse than the naive policy
    }
}

void testEdgeCases() {
    InventoryProblem none;                                         // no days at all
    SCX_CHECK_EQ(solveInventoryDP(none).totalCost, 0LL);
    SCX_CHECK(solveInventoryDP(none).order.empty());

    InventoryProblem cannotOrder;                                   // maxOrder 0: only stock on hand can serve demand
    cannotOrder.demand = {3, 3};
    cannotOrder.unitCost = {1, 1};
    cannotOrder.shortagePenalty = 7;
    cannotOrder.maxInventory = 4;
    cannotOrder.maxOrder = 0;
    cannotOrder.initialInventory = 4;
    const InventoryPlan plan = solveInventoryDP(cannotOrder);       // serve 3, carry 1, serve 1, lose 2
    SCX_CHECK_EQ(plan.totalCost, 2LL * 7);
    SCX_CHECK((plan.lost == std::vector<int>{0, 2}));

    InventoryProblem tiny;                                          // demand above warehouse capacity must be lost
    tiny.demand = {10};
    tiny.unitCost = {1};
    tiny.shortagePenalty = 5;
    tiny.maxInventory = 3;
    tiny.maxOrder = 10;
    const InventoryPlan capped = solveInventoryDP(tiny);
    SCX_CHECK_EQ(capped.order[0], 3);                               // buy 3 (cost 3), lose 7 (cost 35)
    SCX_CHECK_EQ(capped.totalCost, 3LL + 35LL);
}

void testValidation() {
    InventoryProblem p = workedExample();
    p.unitCost.pop_back();
    SCX_CHECK_THROWS(solveInventoryDP(p), std::invalid_argument);
    p = workedExample(); p.demand[1] = -1;
    SCX_CHECK_THROWS(solveInventoryDP(p), std::invalid_argument);
    p = workedExample(); p.initialInventory = 9;
    SCX_CHECK_THROWS(solveInventoryDP(p), std::invalid_argument);
    p = workedExample(); p.holdingCost = -1;
    SCX_CHECK_THROWS(solveInventoryDP(p), std::invalid_argument);
    p = workedExample(); p.maxOrder = 50; p.demand.assign(8, 1); p.unitCost.assign(8, 1);
    SCX_CHECK_THROWS(solveInventoryBruteForce(p), std::invalid_argument);   // 51^8 sequences: refuse instead of hanging
}

int main() {
    SCX_RUN(testWorkedExampleMatchesHandCalculation);
    SCX_RUN(testPlanningAheadBeatsLotForLot);
    SCX_RUN(testPolicyEvaluatorIsIndependentAndStrict);
    SCX_RUN(testAgainstBruteForceOnRandomProblems);
    SCX_RUN(testEdgeCases);
    SCX_RUN(testValidation);
    return scxTestExitCode();
}
