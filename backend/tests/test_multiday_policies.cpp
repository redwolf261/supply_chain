// Multi-day recovery policies: common interface, feasibility, the clairvoyant lower bound, look-ahead consequences,
// no information leakage at policy level, applicability, determinism.
#include <map>
#include <vector>

#include "multiday/MultiDayPolicies.h"
#include "multiday/MultiDayScenarioBuilder.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;

namespace {
MultiDayScenarioParams smallParams(std::uint64_t seed) {
    MultiDayScenarioParams p;
    p.network.suppliers = 3; p.network.backupSuppliers = 2; p.network.warehouses = 2;
    p.network.distributionCenters = 2; p.network.customers = 4;
    p.horizon = 10;
    p.supplierFailures = 1; p.supplierFailureDay = 2; p.supplierFailureDuration = 5;
    p.routeFailures = 1; p.routeFailureDay = 3; p.routeFailureDuration = 4;
    p.seed = seed;
    return p;
}

// S (regular, fails on `failDay` for good) and backup B both feed customer C directly, transit 1 day.
struct Rescue {
    MultiDayScenario scenario;
    int s, b, c;
    Rescue(int horizon, int failDay, int activationCost) {
        SupplyChainGraph& g = scenario.network;
        s = addSupplier(g, "S", 10, 1);
        Node backup;
        backup.name = "B"; backup.type = NodeType::Supplier; backup.supply = 10; backup.unitPurchaseCost = 1;
        backup.isBackup = true; backup.active = false; backup.activationCost = activationCost;
        b = g.addNode(backup);
        Node customer;
        // One day of opening stock at C: day-0 demand is served on time, so S (10 per day, one day of transit) keeps pace
        // exactly and nothing is ever short until S fails. Without it the customer would start one day behind for good,
        // and buying the backup early would be rational with no knowledge of the future at all.
        customer.name = "C"; customer.type = NodeType::Customer; customer.demand = 10; customer.shortagePenaltyPerUnit = 50;
        customer.holdingCostPerUnit = 1; customer.inventory = 10;
        c = g.addNode(customer);
        addRoute(g, s, c, 10, 1);
        addRoute(g, b, c, 10, 1);
        scenario.horizon = horizon;
        scenario.transitDays = {1, 1};
        scenario.demand.assign(3, std::vector<int>(horizon, 0));
        for (int t = 0; t < horizon; ++t) scenario.demand[c][t] = 10;
        scenario.events = {{failDay, {DisruptionType::NodeFailure, s, 0}}};
        scenario.firstDisruptionDay = failDay;
    }
};

int firstPurchaseDay(const MultiDayRunResult& r) {
    for (const DayRecord& d : r.days) if (!d.actions.empty()) return d.day;
    return -1;
}
}  // namespace

void testEveryPolicyProducesValidRunsOnTheSameInputs() {
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        const MultiDaySimulator sim(buildMultiDayScenario(smallParams(seed)), MultiDayConfig{});
        long long demand = -1;
        for (const std::string& name : multiDayPolicyNames()) {
            auto policy = makeMultiDayPolicy(name, 1.0, 4, 6);
            const MultiDayRunResult r = sim.run(*policy);
            if (r.skipped) { SCX_CHECK(name == "rolling_exact" && !r.skipReason.empty()); continue; }
            SCX_CHECK(r.feasible);
            SCX_CHECK_EQ(static_cast<int>(r.days.size()), sim.horizon());
            SCX_CHECK_EQ(r.policy, name);
            if (demand < 0) demand = r.totalDemand;
            SCX_CHECK_EQ(r.totalDemand, demand);                                     // identical demand sequence for everyone
            SCX_CHECK_EQ(r.totalServed + r.finalBacklog, r.totalDemand);             // every unit served or still owed
            long long sum = 0;
            for (const DayRecord& d : r.days) sum += d.cost.total();
            SCX_CHECK_EQ(r.totalCost, sum + r.settlement);                           // total = days + settlement, nothing else
            SCX_CHECK(r.fillRate >= 0.0 && r.fillRate <= 1.0);
        }
    }
}

// The clairvoyant bound must not exceed ANY policy's realised cost: if it did, the bound or the accounting would be wrong.
void testClairvoyantBoundIsALowerBound() {
    int computed = 0, attainable = 0;
    for (bool warm : {false, true})
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        for (ForecastMode mode : {ForecastMode::Persistence, ForecastMode::Perfect}) {
            MultiDayConfig config;
            config.forecast = mode;
            MultiDayScenarioParams params = smallParams(seed);
            params.warmStart = warm;
            const MultiDaySimulator sim(buildMultiDayScenario(params), config);
            const ClairvoyantBound bound = computeClairvoyantBound(sim, 10);
            if (!bound.computed) continue;
            ++computed;
            for (const std::string& name : multiDayPolicyNames()) {
                auto policy = makeMultiDayPolicy(name, 1.0, 4, 6);
                const MultiDayRunResult r = sim.run(*policy);
                if (r.skipped) continue;
                SCX_CHECK(bound.cost <= r.totalCost);
            }
            // Attainability. Replaying the bound's actions with perfect information and a full planning window reaches
            // the bound exactly WHEN every action is bought on day 0. An action bought later (a route repaired on the day
            // it fails) cannot be anticipated by the replay's planner on the days before (an online policy does not know
            // its own future purchases), while the bound's plan does anticipate it; then the replay can only be >= bound.
            bool allOnDayZero = true;
            for (const PendingAction& a : bound.actions) allOnDayZero = allOnDayZero && a.decidedDay == 0;
            std::map<int, std::vector<RecoveryAction>> script;
            for (const PendingAction& a : bound.actions) {
                const int lead = a.action.type == RecoveryActionType::ActivateSupplier ? config.activationLeadDays : config.repairLeadDays;
                script[a.effectiveDay - lead].push_back(a.action);
            }
            MultiDayConfig full = config;
            full.forecast = ForecastMode::Perfect;
            full.planningWindowDays = sim.horizon();
            const MultiDaySimulator fullSim(sim.scenario(), full);
            ScriptedPolicy replay(script, "replay");
            const long long replayed = fullSim.run(replay).totalCost;
            if (allOnDayZero) { SCX_CHECK_EQ(replayed, bound.cost); ++attainable; }
            else SCX_CHECK(replayed >= bound.cost);
        }
    }
    SCX_CHECK(computed >= 8);
    SCX_CHECK(attainable >= 1);                 // the equality case is actually exercised
}

void testClairvoyantByHand() {
    // No disruption at all: the bound is simply the optimal plan, and the no-action policy with perfect, full-window
    // planning attains it.
    Rescue rescue(6, 0, 1000);
    rescue.scenario.events.clear();
    rescue.scenario.firstDisruptionDay = -1;
    MultiDayConfig full;
    full.forecast = ForecastMode::Perfect;
    full.planningWindowDays = 6;
    const MultiDaySimulator sim(rescue.scenario, full);
    const ClairvoyantBound bound = computeClairvoyantBound(sim);
    NoActionPolicy none;
    SCX_CHECK(bound.computed && bound.actions.empty());
    SCX_CHECK_EQ(bound.cost, sim.run(none).totalCost);
    // Day 0 is served from C's opening stock; days 1..5 by S shipping on days 0..4: 50 units x (purchase 1 + transport 1).
    SCX_CHECK_EQ(bound.cost, 100LL);
}

// Consequences over time: buying the backup only pays off over several days, which the immediate-benefit greedy
// cannot see and the look-ahead can.
void testLookaheadSeesWhatImmediateBenefitMisses() {
    Rescue rescue(14, 0, 1000);               // S fails on day 0 for good; activating B costs 1000
    const MultiDaySimulator sim(rescue.scenario, MultiDayConfig{});
    auto greedy = makeMultiDayPolicy("greedy");
    LookaheadPolicy lookahead(10);
    const MultiDayRunResult g = sim.run(*greedy);
    const MultiDayRunResult l = sim.run(lookahead);
    // Greedy's window ends on the backup's first delivery day: 10 units x (b + p) = 600 of benefit, less than 1000.
    SCX_CHECK_EQ(g.actionCount, 0);
    SCX_CHECK_EQ(l.actionCount, 1);
    SCX_CHECK_EQ(firstPurchaseDay(l), 0);
    SCX_CHECK(l.totalCost < g.totalCost);
    SCX_CHECK(l.totalServed > g.totalServed);
    // With a cheap backup, greedy buys too.
    Rescue cheap(14, 0, 300);
    const MultiDaySimulator cheapSim(cheap.scenario, MultiDayConfig{});
    SCX_CHECK_EQ(cheapSim.run(*greedy).actionCount, 1);
}

// No leakage at policy level: with the persistence forecast a policy cannot react before a failure happens.
void testPoliciesCannotSeeTheFutureWithoutAForecast() {
    Rescue rescue(14, 4, 300);
    MultiDayConfig blind, seeing;
    blind.activationLeadDays = seeing.activationLeadDays = 3;
    seeing.forecast = ForecastMode::Perfect;
    LookaheadPolicy lookahead(8);
    const MultiDayRunResult b = MultiDaySimulator(rescue.scenario, blind).run(lookahead);
    const MultiDayRunResult s = MultiDaySimulator(rescue.scenario, seeing).run(lookahead);
    SCX_CHECK(firstPurchaseDay(b) >= 4);                     // only after the failure is observed
    SCX_CHECK(firstPurchaseDay(s) >= 0 && firstPurchaseDay(s) < 4);   // knowing it, buys early enough to cover the lead time
    SCX_CHECK(s.totalCost < b.totalCost);
}

void testNoPolicyBuysWhenNothingIsWrong() {
    MultiDayScenarioParams p = smallParams(3);
    p.supplierFailures = 0;
    p.routeFailures = 0;
    p.initialStockDays = 2.0;
    const MultiDaySimulator sim(buildMultiDayScenario(p), MultiDayConfig{});
    for (const std::string& name : {"greedy", "bottleneck_aware", "bottleneck_aware_lambda0"}) {
        auto policy = makeMultiDayPolicy(name);
        SCX_CHECK_EQ(sim.run(*policy).actionCount, 0);
    }
}

void testDaysUntilFirstDelivery() {
    // B --1 day--> W --2 days--> C with activation lead 1: B's goods can first reach C on day 0 + 1 + 1 + 2 = day 4.
    MultiDayScenario s;
    SupplyChainGraph& g = s.network;
    Node backup;
    backup.name = "B"; backup.type = NodeType::Supplier; backup.supply = 5; backup.isBackup = true; backup.active = false; backup.activationCost = 10;
    const int b = g.addNode(backup);
    const int w = addWarehouse(g, "W");
    const int c = addCustomer(g, "C", 5, 50);
    addRoute(g, b, w, 10, 1);
    addRoute(g, w, c, 10, 1);
    s.horizon = 8;
    s.transitDays = {1, 2};
    s.demand = {std::vector<int>(8, 0), std::vector<int>(8, 0), std::vector<int>(8, 5)};
    const MultiDaySimulator sim(s, MultiDayConfig{});
    MultiDayState state = sim.initialState();
    sim.beginDay(state);
    const ForecastProvider provider(sim.scenario(), ForecastMode::Persistence, sim.config());
    const PolicyContext context{state, provider, sim.config(), sim.candidateActions(state), sim.horizon()};
    SCX_CHECK_EQ(context.candidates.size(), size_t{1});
    SCX_CHECK_EQ(daysUntilFirstDelivery(context, context.candidates[0]), 1 + 1 + 2);
    (void)c;
}

void testRollingExactAppliesOnlyToSmallCandidateSets() {
    const MultiDaySimulator sim(buildMultiDayScenario(smallParams(2)), MultiDayConfig{});
    RollingExactPolicy tooStrict(0);
    const MultiDayRunResult r = sim.run(tooStrict);
    SCX_CHECK(r.skipped);                          // never reported as a result when it could not run
    SCX_CHECK(!r.skipReason.empty());
    SCX_CHECK(r.days.empty());
    RollingExactPolicy normal(8);
    SCX_CHECK(!sim.run(normal).skipped);
}

void testRegistryAndDeterminism() {
    SCX_CHECK_THROWS(makeMultiDayPolicy("teleport"), std::invalid_argument);
    SCX_CHECK_EQ(makeMultiDayPolicy("bottleneck_aware_lambda0")->name(), std::string("bottleneck_aware_lambda0"));
    SCX_CHECK_THROWS(LookaheadPolicy(0), std::invalid_argument);
    SCX_CHECK_THROWS(ImmediateBenefitPolicy(ImmediateBenefitPolicy::Score::BottleneckAware, -1.0, "x"), std::invalid_argument);
    const MultiDaySimulator sim(buildMultiDayScenario(smallParams(5)), MultiDayConfig{});
    for (const std::string& name : {"greedy", "bottleneck_aware", "lookahead"}) {
        auto a = makeMultiDayPolicy(name), b = makeMultiDayPolicy(name);
        const MultiDayRunResult ra = sim.run(*a), rb = sim.run(*b);
        SCX_CHECK_EQ(ra.totalCost, rb.totalCost);
        SCX_CHECK_EQ(ra.actionCount, rb.actionCount);
    }
    // Same seed, same scenario; different seed, different scenario.
    const MultiDayScenario x = buildMultiDayScenario(smallParams(7)), y = buildMultiDayScenario(smallParams(7)), z = buildMultiDayScenario(smallParams(8));
    SCX_CHECK(x.demand == y.demand && x.transitDays == y.transitDays && x.events.size() == y.events.size());
    SCX_CHECK(x.demand != z.demand);
}

void testScenarioBuilder() {
    MultiDayScenarioParams p = smallParams(4);
    p.spikePercent = 100; p.spikeStart = 5; p.spikeDuration = 2;
    p.demandAmplitude = 0.0;
    p.transitMin = p.transitMax = 2;
    const MultiDayScenario s = buildMultiDayScenario(p);
    for (int d : s.transitDays) SCX_CHECK_EQ(d, 2);
    for (const Node& n : s.network.nodes()) {
        if (n.type != NodeType::Customer) continue;
        SCX_CHECK_EQ(s.demand[n.id][5], 2 * s.demand[n.id][4]);            // +100% during the spike
        SCX_CHECK_EQ(s.demand[n.id][7], s.demand[n.id][4]);                // back to normal after it
    }
    SCX_CHECK_EQ(s.firstDisruptionDay, 2);
    int failures = 0, restores = 0;
    for (const ScheduledEvent& e : s.events) {
        if (e.event.type == DisruptionType::NodeFailure || e.event.type == DisruptionType::RouteFailure) ++failures;
        if (e.event.type == DisruptionType::RestoreNode || e.event.type == DisruptionType::RestoreRoute) ++restores;
    }
    SCX_CHECK_EQ(failures, 2);
    SCX_CHECK_EQ(restores, 2);                                              // both recover inside the 10-day horizon
    MultiDayScenarioParams bad = smallParams(1);
    bad.transitMin = 3; bad.transitMax = 1;
    SCX_CHECK_THROWS(buildMultiDayScenario(bad), std::invalid_argument);
    bad = smallParams(1); bad.routeCapacityLossPercent = 0;
    SCX_CHECK_THROWS(buildMultiDayScenario(bad), std::invalid_argument);
}

// Warm start: each route carries its steady-state flow once per transit day, so a healthy network serves every day,
// while the same network started with empty routes is short on day 0 (nothing has reached the customers yet).
void testWarmStart() {
    MultiDayScenarioParams p = smallParams(3);
    p.supplierFailures = 0; p.routeFailures = 0; p.demandAmplitude = 0.0;
    p.transitMin = p.transitMax = 2;
    const MultiDayScenario warm = buildMultiDayScenario(p);
    SCX_CHECK(!warm.initialPipeline.empty());
    std::map<int, std::vector<Shipment>> byEdge;
    for (const Shipment& sh : warm.initialPipeline) {
        SCX_CHECK(sh.departureDay < 0);
        SCX_CHECK_EQ(sh.arrivalDay, sh.departureDay + 2);
        byEdge[sh.edgeId].push_back(sh);
    }
    for (const auto& [edge, list] : byEdge) {
        SCX_CHECK_EQ(static_cast<int>(list.size()), 2);                      // departed on day -2 and day -1
        SCX_CHECK_EQ(list[0].quantity, list[1].quantity);
    }
    NoActionPolicy none;
    const MultiDayRunResult healthy = MultiDaySimulator(warm, MultiDayConfig{}).run(none);
    for (const DayRecord& d : healthy.days) SCX_CHECK_EQ(d.served, d.due);
    SCX_CHECK_EQ(healthy.finalBacklog, 0LL);
    SCX_CHECK(!healthy.recovery.applicable);
    p.warmStart = false;
    const MultiDayRunResult cold = MultiDaySimulator(buildMultiDayScenario(p), MultiDayConfig{}).run(none);
    SCX_CHECK(cold.days[0].served < cold.days[0].due);
    SCX_CHECK(cold.totalCost > healthy.totalCost);

    // Normal service: the baseline drops the events and the spike but keeps the demand noise.
    MultiDayScenarioParams q = smallParams(3);
    q.spikePercent = 100; q.spikeStart = 4; q.spikeDuration = 2;
    const MultiDaySimulator disrupted(buildMultiDayScenario(q), MultiDayConfig{});
    const MultiDayScenario base = disrupted.baselineScenario();
    SCX_CHECK(base.events.empty() && base.firstDisruptionDay == -1 && !disrupted.scenario().events.empty());
    q.spikePercent = 0; q.supplierFailures = 0; q.routeFailures = 0;
    const MultiDayScenario calm = buildMultiDayScenario(q);
    SCX_CHECK((base.demand == calm.demand));                                  // same noise, no spike
    SCX_CHECK((base.demand != disrupted.scenario().demand));
    const MultiDayRunResult withNormal = disrupted.run(none);
    SCX_CHECK((withNormal.normalService == disrupted.normalService()));
    SCX_CHECK_EQ(static_cast<int>(withNormal.normalService.size()), disrupted.horizon());
    std::vector<double> calmService;
    for (const DayRecord& d : MultiDaySimulator(calm, MultiDayConfig{}).run(none).days) calmService.push_back(d.serviceLevel);
    SCX_CHECK((withNormal.normalService == calmService));                    // = the undisrupted run, day by day
    SCX_CHECK(healthy.normalService.empty());                                 // no disruption: no baseline needed

    // Long transit (3 days on every route, 6+ days from supplier to customer) must not drain the pipeline: the
    // planning window is raised to cover the supply lead, so the healthy network still serves every day.
    MultiDayScenarioParams slow = p;
    slow.warmStart = true; slow.transitMin = slow.transitMax = 3;
    const MultiDayRunResult slowRun = MultiDaySimulator(buildMultiDayScenario(slow), MultiDayConfig{}).run(none);
    for (const DayRecord& d : slowRun.days) SCX_CHECK_EQ(d.served, d.due);

    MultiDayScenario bad = warm;
    bad.initialPipeline[0].arrivalDay += 1;                                   // inconsistent with the transit time
    SCX_CHECK_THROWS(MultiDaySimulator(bad, MultiDayConfig{}), std::invalid_argument);
    bad = warm;
    bad.initialPipeline[0].departureDay = 0; bad.initialPipeline[0].arrivalDay = 2;   // departs inside the horizon
    SCX_CHECK_THROWS(MultiDaySimulator(bad, MultiDayConfig{}), std::invalid_argument);
    bad = warm;
    bad.initialPipeline[0].quantity = 0;
    SCX_CHECK_THROWS(MultiDaySimulator(bad, MultiDayConfig{}), std::invalid_argument);
}

int main() {
    SCX_RUN(testEveryPolicyProducesValidRunsOnTheSameInputs);
    SCX_RUN(testWarmStart);
    SCX_RUN(testClairvoyantBoundIsALowerBound);
    SCX_RUN(testClairvoyantByHand);
    SCX_RUN(testLookaheadSeesWhatImmediateBenefitMisses);
    SCX_RUN(testPoliciesCannotSeeTheFutureWithoutAForecast);
    SCX_RUN(testNoPolicyBuysWhenNothingIsWrong);
    SCX_RUN(testDaysUntilFirstDelivery);
    SCX_RUN(testRollingExactAppliesOnlyToSmallCandidateSets);
    SCX_RUN(testRegistryAndDeterminism);
    SCX_RUN(testScenarioBuilder);
    return scxTestExitCode();
}
