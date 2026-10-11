#include <set>
#include <string>
#include <vector>

#include "algorithms/DispatchBaseline.h"
#include "algorithms/GraphTraversal.h"
#include "simulation/NetworkGenerator.h"
#include "simulation/ScenarioIO.h"
#include "simulation/Simulation.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;
using nlohmann::json;

namespace {
std::string fingerprint(const SupplyChainGraph& g) { return networkToJson(g).dump(); }

// S1 (cheap, 100 units) and S2 (dearer, 100 units) both serve W, which serves C.
struct TinyChain {
    SupplyChainGraph graph;
    int s1, s2, w, c, e1, e2, e3;
    TinyChain() {
        s1 = addSupplier(graph, "S1", 100, 1);
        s2 = addSupplier(graph, "S2", 100, 4);
        w = addWarehouse(graph, "W");
        c = addCustomer(graph, "C", 80, 50);
        e1 = addRoute(graph, s1, w, 100, 1);
        e2 = addRoute(graph, s2, w, 100, 1);
        e3 = addRoute(graph, w, c, 100, 1);
    }
};
}  // namespace

void testEachDisruptionType() {
    TinyChain t;
    applyDisruption(t.graph, {DisruptionType::NodeFailure, t.s1, 0});
    SCX_CHECK(!t.graph.node(t.s1).active);
    applyDisruption(t.graph, {DisruptionType::RestoreNode, t.s1, 0});
    SCX_CHECK(t.graph.node(t.s1).active);

    applyDisruption(t.graph, {DisruptionType::CapacityReduction, t.e3, 30});
    SCX_CHECK_EQ(t.graph.edge(t.e3).capacity, 30);
    SCX_CHECK_EQ(t.graph.edge(t.e3).baseCapacity, 100);
    applyDisruption(t.graph, {DisruptionType::CapacityReduction, t.e3, 30});     // idempotent: measured from base
    SCX_CHECK_EQ(t.graph.edge(t.e3).capacity, 30);

    applyDisruption(t.graph, {DisruptionType::RouteFailure, t.e1, 0});
    SCX_CHECK(!t.graph.isEdgeUsable(t.e1));
    applyDisruption(t.graph, {DisruptionType::RestoreRoute, t.e3, 0});           // restores full base capacity
    SCX_CHECK_EQ(t.graph.edge(t.e3).capacity, 100);

    applyDisruption(t.graph, {DisruptionType::DemandSpike, t.c, 50});
    SCX_CHECK_EQ(t.graph.node(t.c).demand, 120);                                  // 80 * 1.5
}

void testInvalidDisruptionsAreRejected() {
    TinyChain t;
    SCX_CHECK_THROWS(applyDisruption(t.graph, {DisruptionType::RestoreNode, t.s1, 0}), std::invalid_argument);   // not failed
    SCX_CHECK_THROWS(applyDisruption(t.graph, {DisruptionType::RestoreRoute, t.e1, 0}), std::invalid_argument);  // healthy
    SCX_CHECK_THROWS(applyDisruption(t.graph, {DisruptionType::DemandSpike, t.s1, 10}), std::invalid_argument);  // not a customer
    SCX_CHECK_THROWS(applyDisruption(t.graph, {DisruptionType::CapacityReduction, t.e1, 101}), std::invalid_argument);
    SCX_CHECK_THROWS(applyDisruption(t.graph, {DisruptionType::NodeFailure, 99, 0}), std::out_of_range);
    applyDisruption(t.graph, {DisruptionType::NodeFailure, t.s1, 0});
    SCX_CHECK_THROWS(applyDisruption(t.graph, {DisruptionType::NodeFailure, t.s1, 0}), std::invalid_argument);   // twice

    SupplyChainGraph withBackup;
    Node backup; backup.name = "B"; backup.type = NodeType::Supplier; backup.isBackup = true; backup.active = false;
    const int b = withBackup.addNode(backup);
    SCX_CHECK_THROWS(applyDisruption(withBackup, {DisruptionType::RestoreNode, b, 0}), std::invalid_argument);   // must pay to activate
}

void testFailedEventLeavesSimulationUnchanged() {
    Simulation sim{TinyChain().graph};
    const std::string before = fingerprint(sim.current());
    SCX_CHECK_THROWS(sim.applyDisruption({DisruptionType::RestoreNode, 0, 0}), std::invalid_argument);
    SCX_CHECK_EQ(fingerprint(sim.current()), before);
    SCX_CHECK(sim.appliedEvents().empty());
}

void testResetRestoresPristineState() {
    TinyChain t;
    Simulation sim(t.graph);
    const std::string pristine = fingerprint(sim.initial());
    sim.applyDisruption({DisruptionType::NodeFailure, t.s1, 0});
    sim.applyDisruption({DisruptionType::CapacityReduction, t.e3, 10});
    sim.applyDisruption({DisruptionType::DemandSpike, t.c, 100});
    SCX_CHECK(fingerprint(sim.current()) != pristine);
    SCX_CHECK_EQ(fingerprint(sim.initial()), pristine);        // initial untouched by events
    sim.resetToInitial();
    SCX_CHECK_EQ(fingerprint(sim.current()), pristine);
    SCX_CHECK(sim.appliedEvents().empty());
    SCX_CHECK(!sim.recoveryExecuted());
}

void testCheckpointLetsUsRetryOnTheSameDisruptedState() {
    TinyChain t;
    Node b; b.name = "B"; b.type = NodeType::Supplier; b.supply = 100; b.unitPurchaseCost = 2; b.isBackup = true; b.active = false; b.activationCost = 20;
    const int backup = t.graph.addNode(b);
    addRoute(t.graph, backup, t.w, 100, 1);
    Simulation sim(t.graph);
    sim.applyDisruption({DisruptionType::NodeFailure, t.s1, 0});
    sim.applyDisruption({DisruptionType::NodeFailure, t.s2, 0});
    const std::string disrupted = fingerprint(sim.current());

    RecoveryPlan greedy = sim.planRecovery("greedy");
    SCX_CHECK_EQ(fingerprint(sim.current()), disrupted);          // planning never changes the state
    SCX_CHECK_EQ(greedy.actions.size(), size_t{1});
    sim.executeRecovery(greedy);
    SCX_CHECK(sim.recoveryExecuted());
    SCX_CHECK(sim.current().node(backup).active);
    SCX_CHECK(fingerprint(sim.current()) != disrupted);

    sim.resetToDisruptedState();                                   // same disrupted state again...
    SCX_CHECK_EQ(fingerprint(sim.current()), disrupted);
    SCX_CHECK_EQ(sim.appliedEvents().size(), size_t{2});
    RecoveryPlan flowOnly = sim.planRecovery("min_cost_flow");     // ...to try a different strategy
    SCX_CHECK(flowOnly.actions.empty());
    SCX_CHECK(flowOnly.totalCost() > greedy.totalCost());          // doing nothing is worse here: 80 units short
    SCX_CHECK_THROWS(sim.resetToDisruptedState(), std::logic_error);
}

void testInvalidPlanIsRejectedAndStateKept() {
    TinyChain t;
    Simulation sim(t.graph);
    sim.applyDisruption({DisruptionType::NodeFailure, t.s1, 0});
    RecoveryPlan plan = sim.planRecovery("min_cost_flow");
    plan.flow.edgeFlow[t.e3] += 500;                               // exceeds capacity and breaks conservation
    const std::string before = fingerprint(sim.current());
    SCX_CHECK_THROWS(sim.executeRecovery(plan), std::invalid_argument);
    SCX_CHECK_EQ(fingerprint(sim.current()), before);
    SCX_CHECK(!sim.recoveryExecuted());
}

void testEventScriptStepping() {
    TinyChain t;
    Simulation sim(t.graph);
    sim.loadScript({{DisruptionType::NodeFailure, t.s1, 0}, {DisruptionType::RouteFailure, t.e2, 0}, {DisruptionType::DemandSpike, t.c, 25}});
    SCX_CHECK_EQ(sim.pendingEventCount(), size_t{3});
    auto first = sim.stepForward();
    SCX_CHECK(first.has_value() && first->find("S1") != std::string::npos);
    SCX_CHECK(!sim.current().node(t.s1).active);
    sim.stepForward();
    sim.stepForward();
    SCX_CHECK_EQ(sim.current().node(t.c).demand, 100);
    SCX_CHECK(!sim.hasPendingEvents());
    SCX_CHECK(!sim.stepForward().has_value());
    sim.resetToInitial();                                          // reset rewinds the script as well
    SCX_CHECK_EQ(sim.pendingEventCount(), size_t{3});
    SCX_CHECK(sim.current().node(t.s1).active);
}

// Dijkstra dispatch commits customer X to the shared cheap supplier and can never take it back.
void testDispatchBaselineIsFeasibleButNotOptimal() {
    SupplyChainGraph g;
    const int s1 = addSupplier(g, "S1", 1, 0);
    const int s2 = addSupplier(g, "S2", 1, 0);
    const int x = addCustomer(g, "X", 1, 100);
    const int y = addCustomer(g, "Y", 1, 90);
    addRoute(g, s1, x, 1, 1);
    addRoute(g, s1, y, 1, 1);
    addRoute(g, s2, x, 1, 5);
    RecoveryPlan dispatch = runStrategy("dijkstra_dispatch", g);
    RecoveryPlan optimal = runStrategy("min_cost_flow", g);
    SCX_CHECK(validateRecoveryPlan(g, dispatch).empty());
    SCX_CHECK_EQ(dispatch.totalCost(), 1LL + 90LL);       // X takes S1 (cost 1); Y is left short (penalty 90)
    SCX_CHECK_EQ(optimal.totalCost(), 6LL);               // S2->X (5) and S1->Y (1)
    (void)y;
}

void testStrategyRegistry() {
    TinyChain t;
    for (const std::string& name : strategyNames()) {
        RecoveryPlan plan = runStrategy(name, t.graph);
        SCX_CHECK(validateRecoveryPlan(t.graph, plan).empty());
        SCX_CHECK_EQ(plan.unmetDemand, 0LL);
    }
    SCX_CHECK_THROWS(runStrategy("quantum_annealing", t.graph), std::invalid_argument);
}

void testMetricsReadDisruptionsFromState() {
    TinyChain t;
    Simulation sim(t.graph);
    sim.applyDisruption({DisruptionType::NodeFailure, t.s1, 0});
    sim.applyDisruption({DisruptionType::RouteFailure, t.e2, 0});
    sim.applyDisruption({DisruptionType::CapacityReduction, t.e3, 50});
    sim.applyDisruption({DisruptionType::DemandSpike, t.c, 25});
    ScenarioMetrics m = sim.metricsFor(sim.planRecovery("min_cost_flow"));
    SCX_CHECK_EQ(m.failedNodes, 1);
    SCX_CHECK_EQ(m.failedRoutes, 1);
    SCX_CHECK_EQ(m.reducedRoutes, 1);
    SCX_CHECK_EQ(m.demandSpikes, 1);
    SCX_CHECK_EQ(m.activeDisruptions, 4);
    SCX_CHECK_EQ(m.activeSuppliers, 1);
    SCX_CHECK_EQ(m.totalSuppliers, 2);
    SCX_CHECK_EQ(m.totalDemand, 100LL);
    SCX_CHECK_EQ(m.totalCost, m.flowCost.total() + m.actionCost);
    SCX_CHECK(m.feasible);
}

void testRecoveryEventRestoresNormalOperations() {
    Scenario s = buildStandardScenario("recovery_after_failure");
    ScenarioRunResult run = runScenario(s.graph, s.events, "min_cost_flow");
    SCX_CHECK_EQ(run.recovered.totalCost, run.normalOperations.totalCost);
    SCX_CHECK_EQ(run.recovered.activeDisruptions, 0);
    SCX_CHECK_EQ(run.recovered.unmetDemand, run.normalOperations.unmetDemand);
}

// Every standard scenario x every strategy: validity and the orderings the maths guarantees.
void testStandardScenarioMatrix() {
    for (const std::string& name : standardScenarioNames()) {
        Scenario s = buildStandardScenario(name);
        const std::string untouched = fingerprint(s.graph);
        long long optimalFlowOnly = -1;
        for (const std::string& strategy : strategyNames()) {
            if (strategy == "exact" && name == "stress_test") continue;      // 2^k subsets: small networks only
            ScenarioRunResult run = runScenario(s.graph, s.events, strategy);
            SCX_CHECK(run.plan.actions.size() <= 20);
            SCX_CHECK(run.recovered.feasible);
            SCX_CHECK_EQ(run.recovered.totalCost, run.plan.totalCost());
            SCX_CHECK_EQ(run.recovered.deliveredDemand + run.recovered.unmetDemand, run.recovered.totalDemand);
            SCX_CHECK(run.recovered.totalCost >= 0);
            if (strategy == "min_cost_flow") optimalFlowOnly = run.recovered.totalCost;
            if (strategy == "dijkstra_dispatch") SCX_CHECK(run.recovered.totalCost >= run.disruptedNoRecovery.totalCost);
            if (strategy == "greedy" || strategy == "bottleneck_aware" || strategy == "bottleneck_aware_lambda0" || strategy == "exact") {
                SCX_CHECK(run.recovered.totalCost <= run.disruptedNoRecovery.totalCost);   // never worse than doing nothing
            }
        }
        SCX_CHECK(optimalFlowOnly >= 0);
        SCX_CHECK_EQ(fingerprint(s.graph), untouched);                         // runScenario never mutates the caller's network
    }
}

void testScenariosAreReproducible() {
    auto render = [](const std::string& name, std::uint64_t seed) {
        Scenario s = buildStandardScenario(name, seed);
        json out = scenarioRunToJson(runScenario(s.graph, s.events, "greedy"));
        out["plan"].erase("runtimeMs");                                          // timing is the only non-deterministic field
        return out.dump();
    };
    SCX_CHECK_EQ(render("multiple_disruptions", 5), render("multiple_disruptions", 5));
    SCX_CHECK_EQ(render("stress_test", 9), render("stress_test", 9));
    SCX_CHECK(render("multiple_disruptions", 5) != render("multiple_disruptions", 6));
    SCX_CHECK_EQ(scenarioToJson(buildStandardScenario("stress_test", 3)).dump(), scenarioToJson(buildStandardScenario("stress_test", 3)).dump());
}

void testGeneratedNetworkStructure() {
    GeneratorParams p;
    Scenario s = generateNetwork(p);
    const SupplyChainGraph& g = s.graph;
    SCX_CHECK_EQ(g.nodeCount(), 4 + 2 + 3 + 3 + 6);
    SCX_CHECK(g.validate().empty());
    int backups = 0;
    for (const Node& n : g.nodes()) {
        if (n.isBackup) { ++backups; SCX_CHECK(!n.active); SCX_CHECK(n.activationCost >= 100); }
        if (n.type == NodeType::Supplier) SCX_CHECK(!g.outgoingEdgeIds(n.id).empty());
        else SCX_CHECK(!g.incomingEdgeIds(n.id).empty());                      // nobody is born cut off from upstream
        if (n.type != NodeType::Customer) SCX_CHECK(!g.outgoingEdgeIds(n.id).empty());
    }
    SCX_CHECK_EQ(backups, 2);

    p.edges = 40;
    SCX_CHECK_EQ(generateNetwork(p).graph.edgeCount(), 40);                    // an explicit edge target is honoured exactly
    p.edges = 3;
    SCX_CHECK_THROWS(generateNetwork(p), std::invalid_argument);              // cannot connect the layers
    p.edges = 100000;
    SCX_CHECK_THROWS(generateNetwork(p), std::invalid_argument);              // more than the layered maximum
    GeneratorParams bad;
    bad.customers = 0;
    SCX_CHECK_THROWS(generateNetwork(bad), std::invalid_argument);
    bad = GeneratorParams{}; bad.capacityFactor = 0;
    SCX_CHECK_THROWS(generateNetwork(bad), std::invalid_argument);

    SCX_CHECK_EQ(scenarioToJson(generateNetwork(GeneratorParams{})).dump(), scenarioToJson(generateNetwork(GeneratorParams{})).dump());
    GeneratorParams other; other.seed = 2;
    SCX_CHECK(scenarioToJson(generateNetwork(other)).dump() != scenarioToJson(generateNetwork(GeneratorParams{})).dump());
}

void testGeneratedDisruptionsAreValidAndScaleWithSeverity() {
    Scenario s = generateNetwork(stressTestParams());
    SCX_CHECK(generateDisruptions(s.graph, 0.0, 1).empty());
    size_t previous = 0;
    for (double severity : {0.2, 0.5, 1.0}) {
        std::vector<Disruption> events = generateDisruptions(s.graph, severity, 7);
        SupplyChainGraph working = s.graph;
        for (const Disruption& e : events) applyDisruption(working, e);        // would throw if any were invalid in sequence
        SCX_CHECK(events.size() >= previous);
        previous = events.size();
    }
    SCX_CHECK_THROWS(generateDisruptions(s.graph, 1.5, 1), std::invalid_argument);
    SCX_CHECK_THROWS(buildStandardScenario("asteroid_strike"), std::invalid_argument);
}

// Regression: an early generator left 15 units of demand unmet in NORMAL operations (a warehouse was fed
// only by an offline backup), which made every disruption scenario meaningless. A healthy generated network
// must always serve all demand, and disruptions must be able to create shortage that recovery can fix.
void testHealthyGeneratedNetworksServeAllDemandAndDisruptionsBite() {
    int seedsWithShortage = 0, seedsWhereGreedyHelps = 0;
    for (std::uint64_t seed = 1; seed <= 15; ++seed) {
        Scenario s = buildStandardScenario("supplier_failure", seed);
        ScenarioRunResult run = runScenario(s.graph, s.events, "greedy");
        SCX_CHECK_EQ(run.normalOperations.unmetDemand, 0LL);
        SCX_CHECK_EQ(run.normalOperations.activeDisruptions, 0);
        if (run.disruptedNoRecovery.unmetDemand > 0) ++seedsWithShortage;
        if (run.recovered.totalCost < run.disruptedNoRecovery.totalCost) ++seedsWhereGreedyHelps;
    }
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        Scenario s = buildStandardScenario("stress_test", seed);
        SCX_CHECK_EQ(runScenario(s.graph, {}, "min_cost_flow").normalOperations.unmetDemand, 0LL);
    }
    SCX_CHECK(seedsWithShortage >= 5);          // losing the biggest supplier usually causes a real shortage
    SCX_CHECK(seedsWhereGreedyHelps >= 3);      // and paid recovery actions do pay off in some of them
}

int main() {
    SCX_RUN(testEachDisruptionType);
    SCX_RUN(testInvalidDisruptionsAreRejected);
    SCX_RUN(testFailedEventLeavesSimulationUnchanged);
    SCX_RUN(testResetRestoresPristineState);
    SCX_RUN(testCheckpointLetsUsRetryOnTheSameDisruptedState);
    SCX_RUN(testInvalidPlanIsRejectedAndStateKept);
    SCX_RUN(testEventScriptStepping);
    SCX_RUN(testDispatchBaselineIsFeasibleButNotOptimal);
    SCX_RUN(testStrategyRegistry);
    SCX_RUN(testMetricsReadDisruptionsFromState);
    SCX_RUN(testRecoveryEventRestoresNormalOperations);
    SCX_RUN(testStandardScenarioMatrix);
    SCX_RUN(testScenariosAreReproducible);
    SCX_RUN(testGeneratedNetworkStructure);
    SCX_RUN(testGeneratedDisruptionsAreValidAndScaleWithSeverity);
    SCX_RUN(testHealthyGeneratedNetworksServeAllDemandAndDisruptionsBite);
    return scxTestExitCode();
}
