// Multi-day simulation core: hand-calculated examples, conservation, transit, capacities, events, backorders,
// recovery time, replay and information leakage.
#include <map>
#include <vector>

#include "multiday/DispatchPlanner.h"
#include "multiday/MultiDayPolicies.h"
#include "multiday/MultiDaySimulator.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;

namespace {
// S (10 per day, unit cost 1) --2 days, cap 10, cost 1--> W (stock 5, holding 1) --1 day, cap 10, cost 1--> C
// (demand 6 per day, penalty 50, so backlog cost b = 0.2 * 50 = 10 per unit per day; holding 1 at C too, so that
// delivering early is never free and the optimal plan, hence the hand calculation, is unique).
struct Chain {
    MultiDayScenario scenario;
    int s, w, c, sw, wc;
    Chain(int horizon, int transitSW = 2, int transitWC = 1, int demandPerDay = 6) {
        SupplyChainGraph& g = scenario.network;
        s = addSupplier(g, "S", 10, 1);
        w = addWarehouse(g, "W", 5, 1);
        Node customer;
        customer.name = "C"; customer.type = NodeType::Customer; customer.demand = demandPerDay;
        customer.shortagePenaltyPerUnit = 50; customer.holdingCostPerUnit = 1;
        c = g.addNode(customer);
        sw = addRoute(g, s, w, 10, 1);
        wc = addRoute(g, w, c, 10, 1);
        scenario.name = "chain";
        scenario.horizon = horizon;
        scenario.transitDays = {transitSW, transitWC};
        scenario.demand.assign(3, std::vector<int>(horizon, 0));
        for (int t = 0; t < horizon; ++t) scenario.demand[c][t] = demandPerDay;
    }
};

std::vector<long long> field(const MultiDayRunResult& r, long long DayRecord::*member) {
    std::vector<long long> out;
    for (const DayRecord& d : r.days) out.push_back(d.*member);
    return out;
}
std::vector<long long> dailyCost(const MultiDayRunResult& r) {
    std::vector<long long> out;
    for (const DayRecord& d : r.days) out.push_back(d.cost.total());
    return out;
}
using V = std::vector<long long>;
}  // namespace

void testHandCalculatedChain() {
    Chain chain(5);
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult r = sim.run(none);
    // Day 0: nothing can reach C yet; W's 5 units leave for C (arrive day 1); S ships 10 (arrive W day 2).
    // Day 1: S ships 10 more (arrive W day 3). Day 2: W forwards the 10 (arrive C day 3). Day 3 likewise.
    SCX_CHECK((field(r, &DayRecord::served) == V{0, 5, 0, 10, 10}));
    SCX_CHECK((field(r, &DayRecord::backlog) == V{6, 7, 13, 9, 5}));
    SCX_CHECK((field(r, &DayRecord::due) == V{6, 12, 13, 19, 15}));
    // In transit at the end of each day: day 0: 10 (S->W) + 5 (W->C); day 1: day-0 S->W batch + day-1 batch;
    // day 2: day-1 S->W batch + 10 W->C; day 3: 10 W->C; day 4: nothing.
    SCX_CHECK((field(r, &DayRecord::inTransit) == V{15, 20, 20, 10, 0}));
    SCX_CHECK((field(r, &DayRecord::stock) == V{0, 0, 0, 0, 0}));
    // Daily cost = purchase + transport + holding + backlog (6 x 10 = 60 on day 0, ...).
    SCX_CHECK((dailyCost(r) == V{85, 90, 140, 100, 50}));
    SCX_CHECK_EQ(r.settlement, 250LL);                  // 5 units still owed x penalty 50, charged once
    SCX_CHECK_EQ(r.totalCost, 715LL);
    SCX_CHECK_EQ(r.totalServed, 25LL);
    SCX_CHECK_EQ(r.totalDemand, 30LL);
    SCX_CHECK(r.feasible);
    // A shipment is never served before it arrives: day 0 serves nothing although 15 units are already moving.
    SCX_CHECK_EQ(r.days[0].served, 0LL);
    SCX_CHECK(r.days[0].inTransit > 0);
}

void testPlannerAloneOnDayZero() {
    Chain chain(5);
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    MultiDayState state = sim.initialState();
    sim.beginDay(state);
    const ForecastProvider provider(sim.scenario(), ForecastMode::Persistence, sim.config());
    const DispatchPlan plan = planDispatch(provider.planningInput(state, 7));
    SCX_CHECK_EQ(plan.dispatch[chain.wc], 5LL);         // all of W's stock goes to C now
    SCX_CHECK_EQ(plan.dispatch[chain.sw], 10LL);        // and S ships its full day
    SCX_CHECK_EQ(plan.draw[chain.s], 10LL);
    SCX_CHECK_EQ(plan.plannedUnserved, 5LL);            // what the planner cannot serve by the end of the horizon
    SCX_CHECK_EQ(plan.plannedCost, 715LL);              // the window reaches the horizon end, so it equals the accounting
}

void testZeroDayTransitDeliversTheSameDay() {
    Chain chain(5, 0, 0);
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult r = sim.run(none);
    SCX_CHECK((field(r, &DayRecord::served) == V{6, 6, 6, 6, 6}));       // same-day chain S -> W -> C
    SCX_CHECK((field(r, &DayRecord::backlog) == V{0, 0, 0, 0, 0}));
    SCX_CHECK((field(r, &DayRecord::inTransit) == V{0, 0, 0, 0, 0}));    // nothing is ever on the road
    // Day 0 uses W's 5 units plus 1 from S (purchase 1, transport 1 + 6); later days buy 6 (6 + 12).
    SCX_CHECK((dailyCost(r) == V{8, 18, 18, 18, 18}));
    SCX_CHECK_EQ(r.totalCost, 80LL);
}

void testShipmentsArriveExactlyAfterTheirTransitTime() {
    Chain chain(7, 3, 1, 30);                   // big demand so S ships every day
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult r = sim.run(none);
    // S -> W takes 3 days: after day 0, 1, 2 there are 10, 20, 30 units on that route. On day 3 the day-0 batch arrives
    // and leaves the pipeline, and S does not ship again: goods leaving on day 3 would reach C after the horizon.
    SCX_CHECK_EQ(r.days[0].inTransitByEdge[chain.sw], 10LL);
    SCX_CHECK_EQ(r.days[1].inTransitByEdge[chain.sw], 20LL);
    SCX_CHECK_EQ(r.days[2].inTransitByEdge[chain.sw], 30LL);
    SCX_CHECK_EQ(r.days[3].inTransitByEdge[chain.sw], 20LL);
    // The day-0 batch reaches W on day 3 and is forwarded at once: day 3 dispatches 10 on W -> C.
    SCX_CHECK_EQ(r.days[3].dispatchedByEdge[chain.wc], 10LL);
    // ...which reaches C one day later.
    SCX_CHECK(r.days[4].served >= 10);
    for (const DayRecord& d : r.days) {                                  // capacity resets daily: never above 10 per day
        SCX_CHECK(d.dispatchedByEdge[chain.sw] <= 10);
        SCX_CHECK(d.dispatchedByEdge[chain.wc] <= 10);
    }
    SCX_CHECK_EQ(r.days[0].dispatchedByEdge[chain.sw], 10LL);           // and is fully usable again on the next day
    SCX_CHECK_EQ(r.days[1].dispatchedByEdge[chain.sw], 10LL);
}

void testPersistentSupplierFailureAndGoodsAlreadyMoving() {
    Chain chain(5);
    chain.scenario.events = {{1, {DisruptionType::NodeFailure, 0, 0}}};
    chain.scenario.firstDisruptionDay = 1;
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult r = sim.run(none);
    // The day-0 shipment from S is already on the road and still arrives; S produces nothing from day 1.
    SCX_CHECK((field(r, &DayRecord::served) == V{0, 5, 0, 10, 0}));
    SCX_CHECK((field(r, &DayRecord::backlog) == V{6, 7, 13, 9, 15}));
    SCX_CHECK((dailyCost(r) == V{85, 70, 140, 90, 150}));
    SCX_CHECK_EQ(r.settlement, 750LL);
    SCX_CHECK_EQ(r.totalCost, 1285LL);
    SCX_CHECK_EQ(r.days[1].activeDisruptions, 1);
    SCX_CHECK_EQ(r.days[1].events.size(), size_t{1});
    for (const DayRecord& d : r.days) if (d.day >= 1) SCX_CHECK_EQ(d.cost.purchase, 0LL);   // nothing drawn from a failed supplier
}

void testSupplierRecoveryRestoresProduction() {
    Chain chain(8);
    chain.scenario.events = {{1, {DisruptionType::NodeFailure, 0, 0}}, {3, {DisruptionType::RestoreNode, 0, 0}}};
    chain.scenario.firstDisruptionDay = 1;
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult r = sim.run(none);
    SCX_CHECK_EQ(r.days[1].cost.purchase, 0LL);
    SCX_CHECK_EQ(r.days[2].cost.purchase, 0LL);
    SCX_CHECK(r.days[3].cost.purchase > 0);             // producing again the day it is restored
    SCX_CHECK_EQ(r.days[3].activeDisruptions, 0);
    SCX_CHECK(r.days.back().served > 0);
}

void testRouteFailureAndRepairAction() {
    Chain chain(8);
    // Route W -> C fails on day 1 and is never restored by itself.
    chain.scenario.events = {{1, {DisruptionType::RouteFailure, 1, 0}}};
    chain.scenario.firstDisruptionDay = 1;
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult broken = sim.run(none);
    SCX_CHECK_EQ(broken.totalServed, 5LL);              // only W's day-0 shipment ever arrives

    // Reopen it on day 1 (lead 1: usable from day 2), paying the reopen cost once.
    ScriptedPolicy repair({{1, {{RecoveryActionType::ReopenRoute, chain.wc, 0}}}}, "repair");
    const MultiDayRunResult fixed = sim.run(repair);
    SCX_CHECK(fixed.totalServed > broken.totalServed);
    SCX_CHECK_EQ(fixed.actionCount, 1);
    SCX_CHECK_EQ(fixed.days[1].actions.size(), size_t{1});
    SCX_CHECK_EQ(fixed.days[2].activeDisruptions, 0);
    SCX_CHECK_EQ(fixed.days[1].dispatchedByEdge[chain.wc], 0LL);  // not usable on the day it is bought
    SCX_CHECK(fixed.days[2].dispatchedByEdge[chain.wc] > 0);        // usable from the day after
}

void testBackupActivationLeadTimeAndCostCountedOnce() {
    Chain chain(6);
    SupplyChainGraph& g = chain.scenario.network;
    Node b;
    b.name = "B"; b.type = NodeType::Supplier; b.supply = 10; b.unitPurchaseCost = 2; b.isBackup = true; b.active = false; b.activationCost = 30;
    const int backup = g.addNode(b);
    addRoute(g, backup, chain.c, 10, 1);
    chain.scenario.transitDays.push_back(1);
    chain.scenario.demand.push_back(std::vector<int>(6, 0));
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    ScriptedPolicy buy({{0, {{RecoveryActionType::ActivateSupplier, backup, 30}}}}, "buy");
    const MultiDayRunResult r = sim.run(buy);
    SCX_CHECK_EQ(r.days[0].cost.actions, 30LL);
    SCX_CHECK_EQ(r.actionCost, 30LL);                   // charged once, not every day it is used
    for (size_t d = 1; d < r.days.size(); ++d) SCX_CHECK_EQ(r.days[d].cost.actions, 0LL);
    SCX_CHECK_EQ(r.days[0].dispatchedByEdge[2], 0LL);    // lead 1: the backup cannot ship on day 0
    SCX_CHECK(r.days[1].dispatchedByEdge[2] > 0);
    NoActionPolicy none;
    SCX_CHECK(r.totalServed > sim.run(none).totalServed);

    // Buying it again, or buying something that is not legal, is a program error.
    ScriptedPolicy twice({{0, {{RecoveryActionType::ActivateSupplier, backup, 30}, {RecoveryActionType::ActivateSupplier, backup, 30}}}});
    SCX_CHECK_THROWS(sim.run(twice), std::invalid_argument);
    ScriptedPolicy notBroken({{0, {{RecoveryActionType::ReopenRoute, chain.sw, 0}}}});
    SCX_CHECK_THROWS(sim.run(notBroken), std::invalid_argument);
    ScriptedPolicy wrongCost({{0, {{RecoveryActionType::ActivateSupplier, backup, 1}}}});
    SCX_CHECK_THROWS(sim.run(wrongCost), std::invalid_argument);
}

void testWarehouseDepletionNeverGoesNegative() {
    MultiDayScenario s;
    SupplyChainGraph& g = s.network;
    const int w = addWarehouse(g, "W", 12, 1);
    const int c = addCustomer(g, "C", 5, 50);
    addRoute(g, w, c, 20, 1);
    s.horizon = 6;
    s.transitDays = {1};
    s.demand = {std::vector<int>(6, 0), std::vector<int>(6, 5)};
    MultiDaySimulator sim(s, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult r = sim.run(none);
    SCX_CHECK_EQ(r.totalServed, 12LL);                  // exactly the stock there was, no more
    for (const DayRecord& d : r.days) for (long long v : d.stockByNode) SCX_CHECK(v >= 0);
    SCX_CHECK_EQ(r.days.back().stock, 0LL);
    SCX_CHECK_EQ(r.totalServed + r.finalBacklog, r.totalDemand);   // every unit is either served or still owed
    (void)w; (void)c;
}

void testDemandSpikeAndBackorderAccounting() {
    Chain chain(6, 0, 0);                       // same-day delivery, 10 per day can be served at most
    chain.scenario.demand[chain.c] = {6, 6, 16, 16, 6, 6};
    chain.scenario.firstDisruptionDay = 2;
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult r = sim.run(none);
    // Day 0: 5 stock + 1. Days 2-3: only 10 can be supplied, 6 backordered each day; days 4-5 catch up 4 each.
    SCX_CHECK((field(r, &DayRecord::served) == V{6, 6, 10, 10, 10, 10}));
    SCX_CHECK((field(r, &DayRecord::backlog) == V{0, 0, 6, 12, 8, 4}));
    // Oldest backlog is served first, so a unit of TODAY's demand waits whenever older units are still owed.
    SCX_CHECK((field(r, &DayRecord::newBackorders) == V{0, 0, 6, 12, 6, 4}));
    // Backlog cost: (6 + 12 + 8 + 4) x 10 = 300; settlement 4 x 50 = 200 (charged once at the end).
    long long backlogCost = 0;
    for (const DayRecord& d : r.days) backlogCost += d.cost.backlog;
    SCX_CHECK_EQ(backlogCost, 300LL);
    SCX_CHECK_EQ(r.settlement, 200LL);
}

void testRecoveryTimeDefinition() {
    // Dip on day 2, stable from day 4 for three days: recovery time = 4 - 1 = 3 (counted from the disruption on day 1).
    RecoveryTime r = computeRecoveryTime({1, 1, 0.5, 0.8, 1, 1, 1, 1}, 1, 0.95, 3);
    SCX_CHECK(r.applicable && r.recovered && r.days == 3 && r.recoveryDay == 4);
    // Delayed impact: service is fine on days 1-2, dips on day 3; a stable stretch BEFORE the dip does not count.
    r = computeRecoveryTime({1, 1, 1, 0.4, 0.97, 0.99, 1}, 1, 0.95, 3);
    SCX_CHECK(r.recovered && r.recoveryDay == 4 && r.days == 3);
    // Never dips: recovery time 0.
    r = computeRecoveryTime({1, 1, 1, 0.96, 1}, 1, 0.95, 3);
    SCX_CHECK(r.recovered && r.days == 0);
    // Recovers too late to confirm the stability window: NOT recovered, and no number is claimed.
    r = computeRecoveryTime({1, 0.2, 0.3, 1, 1}, 1, 0.95, 3);
    SCX_CHECK(r.applicable && !r.recovered);
    // No disruption: not applicable.
    r = computeRecoveryTime({1, 1, 1}, -1, 0.95, 3);
    SCX_CHECK(!r.applicable && !r.recovered);
    // A brief recovery that falls back is not a recovery.
    r = computeRecoveryTime({1, 0.1, 1, 1, 0.5, 1, 1, 1}, 1, 0.95, 3);
    SCX_CHECK(r.recovered && r.recoveryDay == 5 && r.days == 4);
    // Relative to normal service: if the undisrupted network itself only reaches 0.9 on days 3-6, matching 0.9 is
    // 'served normally' (required level min(0.95, normal)); against the bare threshold the run never recovers.
    const std::vector<double> service = {1, 1, 0.5, 0.9, 0.9, 0.9, 0.9};
    r = computeRecoveryTime(service, 2, 0.95, 3, {1, 1, 1, 0.9, 0.9, 0.9, 0.9});
    SCX_CHECK(r.recovered && r.recoveryDay == 3 && r.days == 1);
    r = computeRecoveryTime(service, 2, 0.95, 3);
    SCX_CHECK(r.applicable && !r.recovered);
    // Normal service above the threshold never makes the requirement stricter than the threshold.
    r = computeRecoveryTime({1, 0.5, 0.96, 0.96, 0.96}, 1, 0.95, 3, {1, 1, 1, 1, 1});
    SCX_CHECK(r.recovered && r.recoveryDay == 2 && r.days == 1);
    SCX_CHECK_THROWS(computeRecoveryTime({1, 1}, 0, 0.95, 3, {1}), std::invalid_argument);
}

void testDeterministicReplayAndUntouchedScenario() {
    Chain chain(10, 2, 1, 9);
    chain.scenario.events = {{3, {DisruptionType::NodeFailure, 0, 0}}, {6, {DisruptionType::RestoreNode, 0, 0}}};
    chain.scenario.firstDisruptionDay = 3;
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    NoActionPolicy none;
    const MultiDayRunResult a = sim.run(none), b = sim.run(none);
    SCX_CHECK_EQ(a.totalCost, b.totalCost);
    SCX_CHECK((dailyCost(a) == dailyCost(b)));
    SCX_CHECK((field(a, &DayRecord::served) == field(b, &DayRecord::served)));
    // Reset: a fresh initial state is the same every time, and running does not change the scenario.
    const MultiDayState s1 = sim.initialState(), s2 = sim.initialState();
    SCX_CHECK(s1.stock == s2.stock && s1.backlog == s2.backlog && s1.pipeline.empty());
    SCX_CHECK(sim.scenario().network.node(0).active);
}

void testConservationHoldsEveryDay() {
    Chain chain(12, 2, 1, 9);
    chain.scenario.events = {{2, {DisruptionType::RouteFailure, 0, 0}}, {4, {DisruptionType::CapacityReduction, 1, 40}}, {7, {DisruptionType::RestoreRoute, 0, 0}}};
    chain.scenario.firstDisruptionDay = 2;
    MultiDaySimulator sim(chain.scenario, MultiDayConfig{});
    MultiDayState state = sim.initialState();
    const ForecastProvider provider(sim.scenario(), ForecastMode::Persistence, sim.config());
    for (int t = 0; t < sim.horizon(); ++t) {
        sim.beginDay(state);
        const DayRecord d = sim.finishDay(state, {}, provider, true);
        long long pipeline = 0;
        for (const Shipment& sh : state.pipeline) { pipeline += sh.quantity; SCX_CHECK(sh.arrivalDay > t && sh.arrivalDay - sh.departureDay == sim.scenario().transitDays[sh.edgeId]); }
        long long stock = 0;
        for (long long v : state.stock) { stock += v; SCX_CHECK(v >= 0); }
        SCX_CHECK_EQ(state.initialStock + state.initialInTransit + state.totalDrawn, stock + pipeline + state.totalServed);   // independent recount
        if (t >= 4 && t < 7) SCX_CHECK(d.dispatchedByEdge[1] <= 4);      // capacity cut to 40% of 10
        if (t >= 2 && t < 7) SCX_CHECK_EQ(d.dispatchedByEdge[0], 0LL);    // failed route carries nothing
    }
}

// A policy with the persistence forecast must not anticipate a scheduled failure; with the perfect forecast it may.
void testForecastModesAndNoLeakage() {
    MultiDayScenario s;
    SupplyChainGraph& g = s.network;
    // Holding is free at W but costs 1 at the customer, so delivering early only pays if you know the route will close.
    const int w = addWarehouse(g, "W", 40, 0);
    Node customer;
    customer.name = "C"; customer.type = NodeType::Customer; customer.demand = 6; customer.shortagePenaltyPerUnit = 50; customer.holdingCostPerUnit = 1;
    const int c = g.addNode(customer);
    const int route = addRoute(g, w, c, 10, 1);
    addWarehouse(g, "unused", 0, 0);
    s.horizon = 8;
    s.transitDays = {1};
    s.demand = {std::vector<int>(8, 0), std::vector<int>(8, 6), std::vector<int>(8, 0)};
    s.events = {{2, {DisruptionType::RouteFailure, route, 0}}, {5, {DisruptionType::RestoreRoute, route, 0}}};
    s.firstDisruptionDay = 2;

    MultiDayState state;
    {
        MultiDaySimulator sim(s, MultiDayConfig{});
        state = sim.initialState();
        sim.beginDay(state);
        const ForecastProvider persistence(sim.scenario(), ForecastMode::Persistence, sim.config());
        const ForecastProvider perfect(sim.scenario(), ForecastMode::Perfect, sim.config());
        const PlanningInput blind = persistence.planningInput(state, 7);
        const PlanningInput seeing = perfect.planningInput(state, 7);
        SCX_CHECK_EQ(blind.days[2].capacity[route], 10LL);    // persistence: the failure on day 2 is unknown
        SCX_CHECK_EQ(seeing.days[2].capacity[route], 0LL);    // perfect: it is known
        SCX_CHECK_EQ(seeing.days[5].capacity[route], 10LL);   // and so is the restoration
        SCX_CHECK(persistence.forecastScenario(state, 5).events.empty());
        SCX_CHECK_EQ(perfect.forecastScenario(state, 5).events.size(), size_t{1});   // only events after today, inside the window
    }
    MultiDayConfig blindConfig, seeingConfig;
    seeingConfig.forecast = ForecastMode::Perfect;
    NoActionPolicy none;
    const MultiDayRunResult blindRun = MultiDaySimulator(s, blindConfig).run(none);
    const MultiDayRunResult seeingRun = MultiDaySimulator(s, seeingConfig).run(none);
    // Knowing the closure, the planner sends extra goods to C before day 2 (customers may hold stock).
    SCX_CHECK(seeingRun.days[0].dispatchedByEdge[route] + seeingRun.days[1].dispatchedByEdge[route]
              > blindRun.days[0].dispatchedByEdge[route] + blindRun.days[1].dispatchedByEdge[route]);
    SCX_CHECK(seeingRun.totalCost < blindRun.totalCost);
    (void)w; (void)c;
}

void testInvalidScenariosAreRejected() {
    Chain bad(5);
    bad.scenario.transitDays = {1};
    SCX_CHECK_THROWS(MultiDaySimulator(bad.scenario, MultiDayConfig{}), std::invalid_argument);
    Chain spike(5);
    spike.scenario.events = {{1, {DisruptionType::DemandSpike, 2, 50}}};
    SCX_CHECK_THROWS(MultiDaySimulator(spike.scenario, MultiDayConfig{}), std::invalid_argument);
    Chain late(5);
    late.scenario.events = {{5, {DisruptionType::NodeFailure, 0, 0}}};
    SCX_CHECK_THROWS(MultiDaySimulator(late.scenario, MultiDayConfig{}), std::invalid_argument);
    Chain unsorted(5);
    unsorted.scenario.events = {{3, {DisruptionType::NodeFailure, 0, 0}}, {1, {DisruptionType::RestoreNode, 0, 0}}};
    SCX_CHECK_THROWS(MultiDaySimulator(unsorted.scenario, MultiDayConfig{}), std::invalid_argument);
    Chain warehouseDemand(5);
    warehouseDemand.scenario.demand[1][0] = 3;
    SCX_CHECK_THROWS(MultiDaySimulator(warehouseDemand.scenario, MultiDayConfig{}), std::invalid_argument);
    Chain ok(5);
    MultiDayConfig badWindow;
    badWindow.planningWindowDays = 0;
    SCX_CHECK_THROWS(MultiDaySimulator(ok.scenario, badWindow), std::invalid_argument);
}

// S -> W takes 4 days and W -> C 3, so the supply lead is 7 and, with a 1-day lead, the window must be >= 9.
void testPlanningWindowCoversTheSupplyLead() {
    Chain chain(12, 4, 3, 6);
    SCX_CHECK_EQ(supplyLeadDays(chain.scenario), 7);
    MultiDayConfig config;
    config.planningWindowDays = 3;
    SCX_CHECK_EQ(minimumPlanningWindow(chain.scenario, config), 9);
    SCX_CHECK_EQ(MultiDaySimulator(chain.scenario, config).config().planningWindowDays, 9);   // raised
    config.planningWindowDays = 11;
    SCX_CHECK_EQ(MultiDaySimulator(chain.scenario, config).config().planningWindowDays, 11);  // kept
    // With the raised window the planner buys on day 0 for delivery on day 7; with a 3-day window it never would.
    NoActionPolicy none;
    const MultiDayRunResult r = MultiDaySimulator(chain.scenario, config).run(none);
    SCX_CHECK_EQ(r.days[0].inTransit, 15LL);                                  // 10 from S (route capacity) + W's 5 in stock
    SCX_CHECK(r.days[7].served > 0);
    Chain isolated(5);
    isolated.scenario.network.setEdgeActive(isolated.wc, true);
    SCX_CHECK_EQ(supplyLeadDays(isolated.scenario), 3);                       // ignores up/down state by design
}

int main() {
    SCX_RUN(testHandCalculatedChain);
    SCX_RUN(testPlanningWindowCoversTheSupplyLead);
    SCX_RUN(testPlannerAloneOnDayZero);
    SCX_RUN(testZeroDayTransitDeliversTheSameDay);
    SCX_RUN(testShipmentsArriveExactlyAfterTheirTransitTime);
    SCX_RUN(testPersistentSupplierFailureAndGoodsAlreadyMoving);
    SCX_RUN(testSupplierRecoveryRestoresProduction);
    SCX_RUN(testRouteFailureAndRepairAction);
    SCX_RUN(testBackupActivationLeadTimeAndCostCountedOnce);
    SCX_RUN(testWarehouseDepletionNeverGoesNegative);
    SCX_RUN(testDemandSpikeAndBackorderAccounting);
    SCX_RUN(testRecoveryTimeDefinition);
    SCX_RUN(testDeterministicReplayAndUntouchedScenario);
    SCX_RUN(testConservationHoldsEveryDay);
    SCX_RUN(testForecastModesAndNoLeakage);
    SCX_RUN(testInvalidScenariosAreRejected);
    return scxTestExitCode();
}
