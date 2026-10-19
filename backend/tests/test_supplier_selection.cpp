#include <algorithm>
#include <random>
#include <vector>

#include "advanced/SupplierSelection.h"
#include "optimization/RecoveryPlan.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;

namespace {
BackupSupplier supplier(const std::string& name, int capacity, int activation, int unit = 0) { return {name, capacity, activation, unit}; }

// Deficit 10, no unit costs. A and B are cheap but small, C covers everything alone but costs more than A + B... no:
// A + B = 6 + 6 = 12 < C = 15, so the optimum is {A, B}, while greedy settles for {C}.
SelectionProblem greedyTrap() {
    SelectionProblem p;
    p.suppliers = {supplier("A", 6, 6), supplier("B", 6, 6), supplier("C", 10, 15)};
    p.deficit = 10;
    p.penaltyPerUnit = 100;
    return p;
}

bool subsetSumExists(const std::vector<int>& values, int target) {
    for (unsigned mask = 0; mask < (1u << values.size()); ++mask) {
        int sum = 0;
        for (size_t i = 0; i < values.size(); ++i) if (mask & (1u << i)) sum += values[i];
        if (sum == target) return true;
    }
    return false;
}
}  // namespace

void testCostDefinitionByHand() {
    const SelectionProblem p = greedyTrap();
    int unserved = -1;
    SCX_CHECK_EQ(selectionCost(p, {0, 1}, &unserved), 12LL);     // 6 + 6, deficit fully covered
    SCX_CHECK_EQ(unserved, 0);
    SCX_CHECK_EQ(selectionCost(p, {2}), 15LL);
    SCX_CHECK_EQ(selectionCost(p, {}, &unserved), 1000LL);       // nothing activated: 10 units * penalty 100
    SCX_CHECK_EQ(unserved, 10);
    SCX_CHECK_EQ(selectionCost(p, {0}, &unserved), 6LL + 400LL); // covers 6, 4 unserved
    SCX_CHECK_EQ(unserved, 4);
    SCX_CHECK_THROWS(selectionCost(p, {0, 0}), std::invalid_argument);
    SCX_CHECK_THROWS(selectionCost(p, {7}), std::invalid_argument);
}

void testBacktrackingFindsOptimumWhereGreedyDoesNot() {
    const SelectionProblem p = greedyTrap();
    const SelectionResult exact = solveSelectionBacktracking(p);
    const SelectionResult greedy = solveSelectionGreedy(p);
    SCX_CHECK((exact.chosen == std::vector<int>{0, 1}));
    SCX_CHECK_EQ(exact.totalCost, 12LL);
    SCX_CHECK((greedy.chosen == std::vector<int>{2}));           // adds C first (15 beats 406), then nothing improves
    SCX_CHECK_EQ(greedy.totalCost, 15LL);
    SCX_CHECK(greedy.totalCost > exact.totalCost);               // greedy is a heuristic, not an optimiser
    SCX_CHECK_EQ(exact.unserved, 0);
}

void testUnitCostsAndPenaltyInteract() {
    SelectionProblem p;
    p.suppliers = {supplier("cheapUnits", 5, 10, 1), supplier("dearUnits", 5, 1, 50)};
    p.deficit = 8;
    p.penaltyPerUnit = 20;
    // dearUnits sells at 50 per unit, more than the penalty of 20: never worth buying from, so never worth activating.
    const SelectionResult r = solveSelectionBacktracking(p);
    SCX_CHECK((r.chosen == std::vector<int>{0}));
    SCX_CHECK_EQ(r.totalCost, 10LL + 5LL * 1 + 3LL * 20);        // activate, buy 5 units at 1, leave 3 unserved at 20
    SCX_CHECK_EQ(r.unserved, 3);

    p.penaltyPerUnit = 2;                                        // now even the cheap units (1) barely pay: 5 * 1 vs 5 * 2
    const SelectionResult low = solveSelectionBacktracking(p);   // activation 10 is not repaid by saving 5 * (2 - 1)
    SCX_CHECK(low.chosen.empty());
    SCX_CHECK_EQ(low.totalCost, 8LL * 2);
}

void testPruningKeepsTheOptimumButVisitsFewerNodes() {
    std::mt19937 rng(99);
    auto pick = [&](int lo, int hi) { return static_cast<int>(lo + rng() % static_cast<unsigned>(hi - lo + 1)); };
    long long prunedTotal = 0, fullTotal = 0;
    for (int trial = 0; trial < 40; ++trial) {
        SelectionProblem p;
        const int n = pick(1, 11);
        for (int i = 0; i < n; ++i) p.suppliers.push_back(supplier("s" + std::to_string(i), pick(1, 12), pick(0, 20), pick(0, 6)));
        p.deficit = pick(0, 40);
        p.penaltyPerUnit = pick(0, 15);

        // Independent oracle: evaluate every subset with the reference cost function.
        long long best = -1;
        for (unsigned mask = 0; mask < (1u << n); ++mask) {
            std::vector<int> subset;
            for (int i = 0; i < n; ++i) if (mask & (1u << i)) subset.push_back(i);
            const long long cost = selectionCost(p, subset);
            if (best < 0 || cost < best) best = cost;
        }
        const SelectionResult pruned = solveSelectionBacktracking(p, true);
        const SelectionResult full = solveSelectionBacktracking(p, false);
        SCX_CHECK_EQ(pruned.totalCost, best);
        SCX_CHECK_EQ(full.totalCost, best);
        SCX_CHECK_EQ(selectionCost(p, pruned.chosen), pruned.totalCost);        // reported subset really costs that much
        SCX_CHECK(pruned.nodesVisited <= full.nodesVisited);
        SCX_CHECK_EQ(full.nodesVisited, full.fullTreeNodes);                     // without pruning every node is visited
        SCX_CHECK(solveSelectionGreedy(p).totalCost >= best);                    // greedy can never beat the optimum
        prunedTotal += pruned.nodesVisited;
        fullTotal += full.nodesVisited;
    }
    SCX_CHECK(prunedTotal < fullTotal);                                          // pruning saved real work overall
}

void testValidation() {
    SelectionProblem p = greedyTrap();
    p.suppliers[0].capacity = -1;
    SCX_CHECK_THROWS(solveSelectionBacktracking(p), std::invalid_argument);
    p = greedyTrap(); p.deficit = -3;
    SCX_CHECK_THROWS(solveSelectionGreedy(p), std::invalid_argument);
    p = greedyTrap();
    for (int i = 0; i < 30; ++i) p.suppliers.push_back(supplier("x", 1, 1));
    SCX_CHECK_THROWS(solveSelectionBacktracking(p), std::invalid_argument);     // refuse exponential blow-up
    SelectionProblem none;                                                       // no suppliers: everything unserved
    none.deficit = 5; none.penaltyPerUnit = 3;
    const SelectionResult r = solveSelectionBacktracking(none);
    SCX_CHECK_EQ(r.totalCost, 15LL);
    SCX_CHECK_EQ(r.unserved, 5);
}

// NP-completeness link (proof in docs/NP_COMPLETENESS.md): with activation cost = capacity, deficit D and
// budget D, "cover D within budget D" holds exactly when some subset of the capacities sums to D (SUBSET-SUM).
void testSubsetSumReductionAgreesWithBruteForce() {
    std::mt19937 rng(2025);
    auto pick = [&](int lo, int hi) { return static_cast<int>(lo + rng() % static_cast<unsigned>(hi - lo + 1)); };
    int yes = 0, no = 0;
    for (int trial = 0; trial < 150; ++trial) {
        std::vector<int> values;
        const int n = pick(1, 10);
        for (int i = 0; i < n; ++i) values.push_back(pick(1, 15));
        const int target = pick(1, 60);
        const bool expected = subsetSumExists(values, target);
        SCX_CHECK_EQ(canCoverWithinBudget(values, values, target, target), expected);
        (expected ? yes : no)++;
    }
    SCX_CHECK(yes >= 20 && no >= 20);                              // both answers are exercised, not a one-sided test
}

// The same reduction pushed through the REAL recovery model: customer with demand D and penalty D + 1, one backup
// supplier per item (supply = value, activation cost = value, free units, free route). The optimum total cost of
// the exhaustive recovery solver is <= D exactly when a subset sums to D.
void testSubsetSumReductionThroughTheRecoveryEngine() {
    std::mt19937 rng(7);
    auto pick = [&](int lo, int hi) { return static_cast<int>(lo + rng() % static_cast<unsigned>(hi - lo + 1)); };
    int yes = 0, no = 0;
    for (int trial = 0; trial < 60; ++trial) {
        std::vector<int> values;
        const int n = pick(1, 8);
        for (int i = 0; i < n; ++i) values.push_back(pick(1, 12));
        const int target = pick(1, 40);

        SupplyChainGraph g;
        const int customer = addCustomer(g, "C", target, target + 1);
        for (int i = 0; i < n; ++i) {
            Node backup;
            backup.name = "B" + std::to_string(i);
            backup.type = NodeType::Supplier;
            backup.supply = values[i];
            backup.activationCost = values[i];
            backup.isBackup = true;
            backup.active = false;
            const int id = g.addNode(backup);
            addRoute(g, id, customer, values[i], 0);
        }
        const RecoveryPlan best = solveRecoveryByExhaustiveSearch(g);
        const bool decided = best.totalCost() <= target;
        const bool expected = subsetSumExists(values, target);
        SCX_CHECK_EQ(decided, expected);
        if (decided) SCX_CHECK_EQ(best.unmetDemand, 0LL);          // a "yes" is a plan that serves everything
        (expected ? yes : no)++;
    }
    SCX_CHECK(yes >= 8 && no >= 8);
}

int main() {
    SCX_RUN(testCostDefinitionByHand);
    SCX_RUN(testBacktrackingFindsOptimumWhereGreedyDoesNot);
    SCX_RUN(testUnitCostsAndPenaltyInteract);
    SCX_RUN(testPruningKeepsTheOptimumButVisitsFewerNodes);
    SCX_RUN(testValidation);
    SCX_RUN(testSubsetSumReductionAgreesWithBruteForce);
    SCX_RUN(testSubsetSumReductionThroughTheRecoveryEngine);
    return scxTestExitCode();
}
