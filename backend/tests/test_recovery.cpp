#include <iostream>
#include <random>
#include <vector>

#include "algorithms/RecoveryHeuristics.h"
#include "optimization/RecoveryPlan.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;

namespace {
int addBackup(SupplyChainGraph& g, const std::string& name, int supply, int purchaseCost, int activationCost) {
    Node n;
    n.name = name; n.type = NodeType::Supplier; n.supply = supply; n.unitPurchaseCost = purchaseCost;
    n.isBackup = true; n.active = false; n.activationCost = activationCost;
    return g.addNode(n);
}

// Customer (demand 10, penalty 10) with no working supply plus the given backups wired directly to it.
struct BackupCase {
    SupplyChainGraph graph;
    int customer;
};
BackupCase customerOnly() {
    BackupCase c;
    c.customer = addCustomer(c.graph, "C", 10, 10);
    return c;
}

bool sameActions(const RecoveryPlan& a, const RecoveryPlan& b) {
    if (a.actions.size() != b.actions.size()) return false;
    for (size_t i = 0; i < a.actions.size(); ++i) {
        if (a.actions[i].type != b.actions[i].type || a.actions[i].targetId != b.actions[i].targetId) return false;
    }
    return true;
}
}  // namespace

void testActivatesBackupWhenItPaysOff() {
    BackupCase c = customerOnly();
    const int b = addBackup(c.graph, "B", 10, 1, 30);
    addRoute(c.graph, b, c.customer, 10, 1);
    // Doing nothing leaves all 10 units short: 10 * penalty 10 = 100.
    SCX_CHECK_EQ(buildRecoveryPlan(c.graph, "none", {}).totalCost(), 100LL);

    for (const RecoveryPlan& plan : {greedyRecovery(c.graph), bottleneckAwareRecovery(c.graph),
                                     solveRecoveryByExhaustiveSearch(c.graph)}) {
        SCX_CHECK_EQ(plan.actions.size(), size_t{1});
        SCX_CHECK_EQ(plan.totalCost(), 50LL);  // activation 30 + purchase 10 + transport 10
        SCX_CHECK_EQ(plan.unmetDemand, 0LL);
        SCX_CHECK(validateRecoveryPlan(c.graph, plan).empty());
    }
}

void testSkipsActionThatDoesNotPayOff() {
    BackupCase c = customerOnly();
    const int b = addBackup(c.graph, "B", 10, 1, 500);  // saving is 80 < 500
    addRoute(c.graph, b, c.customer, 10, 1);
    RecoveryPlan plan = greedyRecovery(c.graph);
    SCX_CHECK(plan.actions.empty());
    SCX_CHECK_EQ(plan.totalCost(), 100LL);
    SCX_CHECK_EQ(plan.unmetDemand, 10LL);  // unmet demand is reported, not hidden
}

void testReopensFailedRoute() {
    SupplyChainGraph g;
    const int s = addSupplier(g, "S", 10, 0);
    const int c = addCustomer(g, "C", 10, 10);
    Edge e;
    e.from = s; e.to = c; e.baseCapacity = 10; e.costPerUnit = 1; e.reopenCost = 15; e.active = false;
    g.addEdge(e);
    RecoveryPlan plan = greedyRecovery(g);
    SCX_CHECK_EQ(plan.actions.size(), size_t{1});
    SCX_CHECK(plan.actions[0].type == RecoveryActionType::ReopenRoute);
    SCX_CHECK_EQ(plan.totalCost(), 25LL);  // reopen 15 + transport 10
    SCX_CHECK(validateRecoveryPlan(g, plan).empty());
}

// Greedy ranks B1 (6 units for 6) above B2 (10 units for 30) and then still needs B2.
// The true optimum is B2 alone. This documents that greedy is a heuristic, not an optimiser.
void testGreedyIsNotAlwaysOptimal() {
    BackupCase c = customerOnly();
    const int b1 = addBackup(c.graph, "B1", 6, 1, 6);
    const int b2 = addBackup(c.graph, "B2", 10, 1, 30);
    addRoute(c.graph, b1, c.customer, 10, 1);
    addRoute(c.graph, b2, c.customer, 10, 1);

    RecoveryPlan greedy = greedyRecovery(c.graph);
    RecoveryPlan exact = solveRecoveryByExhaustiveSearch(c.graph);
    SCX_CHECK_EQ(greedy.actions.size(), size_t{2});
    SCX_CHECK_EQ(greedy.totalCost(), 56LL);
    SCX_CHECK_EQ(exact.totalCost(), 50LL);
    SCX_CHECK_EQ(exact.actions.size(), size_t{1});
    SCX_CHECK_EQ(exact.actions[0].targetId, b2);
    SCX_CHECK_EQ(exact.candidateEvaluations, 4LL);  // 2^2 subsets
    SCX_CHECK(validateRecoveryPlan(c.graph, greedy).empty());
}

// X is cheaper to activate but its route is exactly saturated; Y costs slightly more but has slack.
// lambda = 0 and plain greedy pick X; a positive lambda penalises X's exposure and picks Y.
void testBottleneckTermChangesTheChoice() {
    BackupCase c = customerOnly();
    const int x = addBackup(c.graph, "X", 10, 1, 20);
    const int y = addBackup(c.graph, "Y", 10, 1, 22);
    addRoute(c.graph, x, c.customer, 10, 1);   // utilisation 100%
    addRoute(c.graph, y, c.customer, 20, 1);   // utilisation 50%

    BottleneckConfig off; off.lambda = 0.0;
    BottleneckConfig on;  on.lambda = 1.0;
    RecoveryPlan greedy = greedyRecovery(c.graph);
    RecoveryPlan ablated = bottleneckAwareRecovery(c.graph, off);
    RecoveryPlan aware = bottleneckAwareRecovery(c.graph, on);

    SCX_CHECK_EQ(greedy.actions[0].targetId, x);
    SCX_CHECK_EQ(ablated.actions[0].targetId, x);
    SCX_CHECK_EQ(aware.actions[0].targetId, y);
    SCX_CHECK_EQ(ablated.totalCost(), 40LL);
    SCX_CHECK_EQ(aware.totalCost(), 42LL);  // honest result: the risk-averse choice costs 2 more here
    SCX_CHECK_EQ(ablated.algorithm, std::string("bottleneck_aware_lambda0"));
    SCX_CHECK(aware.steps[0].bottleneckRisk < 0.01);   // Y's flow rides on an unsaturated route
    SCX_CHECK(validateRecoveryPlan(c.graph, aware).empty());
}

void testSaturatedFlowShare() {
    BackupCase c = customerOnly();
    const int b = addBackup(c.graph, "B", 10, 1, 1);
    const int e = addRoute(c.graph, b, c.customer, 10, 1);
    c.graph.setNodeActive(b, true);
    FlowSolution plan = FlowSolution::empty(c.graph);
    SCX_CHECK_EQ(saturatedFlowShare(c.graph, plan, 0.9), 0.0);   // nothing shipped
    plan.edgeFlow[e] = 9;
    SCX_CHECK_EQ(saturatedFlowShare(c.graph, plan, 0.9), 1.0);   // 9 >= 0.9 * 10
    plan.edgeFlow[e] = 8;
    SCX_CHECK_EQ(saturatedFlowShare(c.graph, plan, 0.9), 0.0);
}

void testIllegalActionsAndTamperedPlans() {
    BackupCase c = customerOnly();
    const int b = addBackup(c.graph, "B", 10, 1, 30);
    addRoute(c.graph, b, c.customer, 10, 1);
    SupplyChainGraph working = c.graph;
    working.setNodeActive(b, true);

    RecoveryAction activate{RecoveryActionType::ActivateSupplier, b, 30};
    SCX_CHECK_THROWS(applyAction(working, activate), std::invalid_argument);             // already active
    SCX_CHECK_THROWS(applyAction(c.graph, {RecoveryActionType::ActivateSupplier, b, 1}), std::invalid_argument);  // wrong cost
    SCX_CHECK_THROWS(applyAction(c.graph, {RecoveryActionType::ReopenRoute, 0, 0}), std::invalid_argument);       // edge is active

    RecoveryPlan good = greedyRecovery(c.graph);
    RecoveryPlan duplicated = good;
    duplicated.actions.push_back(duplicated.actions[0]);
    SCX_CHECK(!validateRecoveryPlan(c.graph, duplicated).empty());

    RecoveryPlan understated = good;
    understated.actionCost = 0;
    SCX_CHECK(!validateRecoveryPlan(c.graph, understated).empty());

    RecoveryPlan fakeFulfilment = good;
    fakeFulfilment.flow.unmetDemand[c.customer] = 0;
    fakeFulfilment.flow.edgeFlow[0] = 0;  // claims demand met with no shipments
    SCX_CHECK(!validateRecoveryPlan(c.graph, fakeFulfilment).empty());
}

void testNoCandidatesOnHealthyNetwork() {
    SupplyChainGraph g;
    const int s = addSupplier(g, "S", 10, 1);
    const int c = addCustomer(g, "C", 5, 10);
    addRoute(g, s, c, 10, 1);
    SCX_CHECK(enumerateCandidateActions(g).empty());
    RecoveryPlan plan = greedyRecovery(g);
    SCX_CHECK(plan.actions.empty());
    SCX_CHECK_EQ(plan.totalCost(), 5LL * 1 + 5LL * 1);
}

void testConfigurationAndSearchLimits() {
    BackupCase c = customerOnly();
    BottleneckConfig bad; bad.lambda = -1;
    SCX_CHECK_THROWS(bottleneckAwareRecovery(c.graph, bad), std::invalid_argument);
    for (int i = 0; i < 4; ++i) addBackup(c.graph, "B" + std::to_string(i), 1, 1, 1);
    SCX_CHECK_THROWS(solveRecoveryByExhaustiveSearch(c.graph, 3), std::invalid_argument);
}

// 40 seeded random disrupted networks: invariants every correct implementation must satisfy,
// plus a printed (not asserted) summary of how often each heuristic matched the optimum.
void testRandomNetworkInvariants() {
    std::mt19937 rng(777);
    auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    int greedyOptimal = 0, awareOptimal = 0, ablatedOptimal = 0, trials = 0;

    for (int trial = 0; trial < 40; ++trial) {
        SupplyChainGraph g;
        addSupplier(g, "S0", pick(0, 4), pick(0, 3));
        addSupplier(g, "S1", pick(0, 4), pick(0, 3));
        addBackup(g, "B0", pick(2, 5), pick(0, 4), pick(1, 20));
        addBackup(g, "B1", pick(2, 5), pick(0, 4), pick(1, 20));
        addWarehouse(g, "W", pick(0, 2), pick(0, 2));
        addCustomer(g, "C0", pick(1, 5), pick(3, 12));
        addCustomer(g, "C1", pick(1, 5), pick(3, 12));
        const int n = g.nodeCount();
        for (int k = 0; k < 8; ++k) {
            Edge e;
            e.from = pick(0, n - 1);
            e.to = pick(0, n - 1);
            if (e.from == e.to) e.to = (e.to + 1) % n;
            e.baseCapacity = pick(1, 4); e.costPerUnit = pick(0, 3); e.reopenCost = pick(1, 10);
            e.active = pick(0, 9) >= 3;
            g.addEdge(e);
        }

        const RecoveryPlan none = buildRecoveryPlan(g, "none", {});
        const RecoveryPlan exact = solveRecoveryByExhaustiveSearch(g);
        BottleneckConfig off; off.lambda = 0.0;
        const RecoveryPlan greedy = greedyRecovery(g);
        const RecoveryPlan aware = bottleneckAwareRecovery(g);
        const RecoveryPlan ablated = bottleneckAwareRecovery(g, off);

        for (const RecoveryPlan* plan : {&none, &exact, &greedy, &aware, &ablated}) {
            SCX_CHECK(validateRecoveryPlan(g, *plan).empty());
            SCX_CHECK(exact.totalCost() <= plan->totalCost());   // nothing beats the exact optimum
        }
        for (const RecoveryPlan* plan : {&greedy, &aware, &ablated}) {
            SCX_CHECK(plan->totalCost() <= none.totalCost());    // never worse than doing nothing
        }
        greedyOptimal += greedy.totalCost() == exact.totalCost();
        awareOptimal += aware.totalCost() == exact.totalCost();
        ablatedOptimal += ablated.totalCost() == exact.totalCost();
        ++trials;

        SCX_CHECK(sameActions(greedy, greedyRecovery(g)));       // deterministic
    }
    std::cout << "  [info] matched exact optimum in " << greedyOptimal << " (greedy), " << awareOptimal
              << " (bottleneck-aware, lambda=1), " << ablatedOptimal << " (lambda=0) of " << trials << " random cases\n";
}

int main() {
    SCX_RUN(testActivatesBackupWhenItPaysOff);
    SCX_RUN(testSkipsActionThatDoesNotPayOff);
    SCX_RUN(testReopensFailedRoute);
    SCX_RUN(testGreedyIsNotAlwaysOptimal);
    SCX_RUN(testBottleneckTermChangesTheChoice);
    SCX_RUN(testSaturatedFlowShare);
    SCX_RUN(testIllegalActionsAndTamperedPlans);
    SCX_RUN(testNoCandidatesOnHealthyNetwork);
    SCX_RUN(testConfigurationAndSearchLimits);
    SCX_RUN(testRandomNetworkInvariants);
    return scxTestExitCode();
}
