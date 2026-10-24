#include "multiday/MultiDaySimulator.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <queue>

namespace scx {

namespace {
double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void applyCompletedAction(SupplyChainGraph& g, const RecoveryAction& a) {
    if (a.type == RecoveryActionType::ActivateSupplier) g.setNodeActive(a.targetId, true);
    else g.setEdgeActive(a.targetId, true);
}

int leadFor(const MultiDayConfig& config, const RecoveryAction& a) {
    return a.type == RecoveryActionType::ActivateSupplier ? config.activationLeadDays : config.repairLeadDays;
}

void validateConfig(const MultiDayConfig& c) {
    if (c.planningWindowDays < 1 || c.planningWindowDays > 60) throw std::invalid_argument("planning window must be between 1 and 60 days");
    if (c.activationLeadDays < 0 || c.repairLeadDays < 0 || c.activationLeadDays > 30 || c.repairLeadDays > 30) throw std::invalid_argument("lead days must be between 0 and 30");
    if (!(c.backlogPenaltyFraction >= 0.0 && c.backlogPenaltyFraction <= 10.0)) throw std::invalid_argument("backlog penalty fraction must be between 0 and 10");
    if (!(c.serviceThreshold > 0.0 && c.serviceThreshold <= 1.0)) throw std::invalid_argument("service threshold must be in (0, 1]");
    if (c.stabilityDays < 1) throw std::invalid_argument("stability days must be at least 1");
    if (!(c.saturationThreshold > 0.0 && c.saturationThreshold <= 1.0)) throw std::invalid_argument("saturation threshold must be in (0, 1]");
}
}  // namespace

int PolicyContext::leadDays(const RecoveryAction& action) const { return leadFor(config, action); }

// ------------------------------------------------------------------------------------------------ forecast

ForecastProvider::ForecastProvider(const MultiDayScenario& scenario, ForecastMode mode, const MultiDayConfig& config)
    : scenario_(scenario), mode_(mode), config_(config), backlogCost_(backlogCostPerDay(scenario.network, config)) {}

PlanningInput ForecastProvider::planningInput(const MultiDayState& state, int windowDays, const std::vector<RecoveryAction>& boughtToday) const {
    std::vector<PendingAction> hypothetical;
    for (const RecoveryAction& a : boughtToday) hypothetical.push_back({a, state.day, state.day + leadFor(config_, a)});
    return planningInputWith(state, windowDays, hypothetical);
}

PlanningInput ForecastProvider::planningInputWith(const MultiDayState& state, int windowDays, const std::vector<PendingAction>& hypothetical) const {
    const int t = state.day;
    const int window = std::max(1, std::min(windowDays, scenario_.horizon - t));
    const SupplyChainGraph& base = state.network;
    const int n = base.nodeCount(), m = base.edgeCount();

    // Effects known to the policy: its pending actions and the hypothetical ones.
    std::vector<PendingAction> known = state.pending;
    known.insert(known.end(), hypothetical.begin(), hypothetical.end());

    PlanningInput in;
    in.graph = &scenario_.network;
    in.transitDays = &scenario_.transitDays;
    in.stock = state.stock;
    in.backlogCost = backlogCost_;
    in.saturationThreshold = config_.saturationThreshold;
    in.arrivals.assign(window, std::vector<long long>(n, 0));
    for (const Shipment& s : state.pipeline) {
        const int k = s.arrivalDay - t;
        if (k >= 1 && k < window) in.arrivals[k][s.to] += s.quantity;
    }

    SupplyChainGraph g = base;                  // evolves through the window as the forecast believes
    for (int k = 0; k < window; ++k) {
        const int day = t + k;
        if (k > 0 && mode_ == ForecastMode::Perfect) {
            for (const ScheduledEvent& e : scenario_.events) if (e.day == day) applyScheduledEvent(g, e.event);
        }
        for (const PendingAction& p : known) {
            if (p.effectiveDay == day || (k == 0 && p.effectiveDay < day)) {
                const bool already = p.action.type == RecoveryActionType::ActivateSupplier ? g.node(p.action.targetId).active : g.edge(p.action.targetId).active;
                if (!already) applyCompletedAction(g, p.action);
            }
        }
        PlanningDay pd;
        pd.supply.assign(n, 0);
        pd.capacity.assign(m, 0);
        pd.due.assign(n, 0);
        for (const Node& node : g.nodes()) {
            if (node.active) pd.supply[node.id] = node.supply;
            if (node.type == NodeType::Customer) {
                if (k == 0) pd.due[node.id] = state.dueToday[node.id];
                else pd.due[node.id] = mode_ == ForecastMode::Perfect ? scenario_.demand[node.id][day] : state.demandToday[node.id];
            }
        }
        for (const Edge& e : g.edges()) if (g.isEdgeUsable(e.id)) pd.capacity[e.id] = e.capacity;
        in.days.push_back(std::move(pd));
    }
    return in;
}

MultiDayScenario ForecastProvider::forecastScenario(const MultiDayState& state, int days) const {
    const int t = state.day;
    MultiDayScenario f;
    f.name = scenario_.name + "/forecast";
    f.seed = scenario_.seed;
    f.network = scenario_.network;
    f.horizon = std::max(t + 1, std::min(scenario_.horizon, t + std::max(1, days)));
    f.transitDays = scenario_.transitDays;
    f.demand.assign(scenario_.network.nodeCount(), std::vector<int>(f.horizon, 0));
    for (const Node& node : scenario_.network.nodes()) {
        if (node.type != NodeType::Customer) continue;
        for (int day = t; day < f.horizon; ++day) {
            f.demand[node.id][day] = (mode_ == ForecastMode::Perfect || day == t) ? scenario_.demand[node.id][day] : static_cast<int>(state.demandToday[node.id]);
        }
    }
    if (mode_ == ForecastMode::Perfect) {
        for (const ScheduledEvent& e : scenario_.events) if (e.day > t && e.day < f.horizon) f.events.push_back(e);
    }
    return f;
}

// ------------------------------------------------------------------------------------------------ simulator

MultiDaySimulator::MultiDaySimulator(MultiDayScenario scenario, MultiDayConfig config)
    : scenario_(std::move(scenario)), config_(config) {
    validateMultiDayScenario(scenario_);
    validateConfig(config_);
    config_.planningWindowDays = std::max(config_.planningWindowDays, minimumPlanningWindow(scenario_, config_));
    backlogCost_ = backlogCostPerDay(scenario_.network, config_);
}

int supplyLeadDays(const MultiDayScenario& s) {
    const SupplyChainGraph& g = s.network;
    int lead = -1;
    for (const Node& source : g.nodes()) {
        if (source.type != NodeType::Supplier) continue;
        // Dijkstra on transit days over every route (O((V + E) log V) per supplier).
        std::vector<long long> dist(g.nodeCount(), std::numeric_limits<long long>::max());
        using Item = std::pair<long long, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
        dist[source.id] = 0;
        heap.emplace(0, source.id);
        while (!heap.empty()) {
            const auto [d, u] = heap.top();
            heap.pop();
            if (d > dist[u]) continue;
            if (g.node(u).type == NodeType::Customer) { lead = std::max(lead, static_cast<int>(d)); break; }
            for (int edgeId : g.outgoingEdgeIds(u)) {
                const Edge& e = g.edge(edgeId);
                if (d + s.transitDays[edgeId] < dist[e.to]) { dist[e.to] = d + s.transitDays[edgeId]; heap.emplace(dist[e.to], e.to); }
            }
        }
    }
    return lead;
}

int minimumPlanningWindow(const MultiDayScenario& s, const MultiDayConfig& c) {
    const int lead = supplyLeadDays(s);
    return lead < 0 ? 1 : std::max(c.activationLeadDays, c.repairLeadDays) + lead + 1;
}

MultiDayState MultiDaySimulator::initialState() const {
    MultiDayState s;
    s.day = 0;
    s.network = scenario_.network;
    const int n = s.network.nodeCount();
    s.stock.assign(n, 0);
    s.backlog.assign(n, 0);
    s.dueToday.assign(n, 0);
    s.demandToday.assign(n, 0);
    for (const Node& node : s.network.nodes()) s.stock[node.id] = node.inventory;
    for (long long v : s.stock) s.initialStock += v;
    s.pipeline = scenario_.initialPipeline;
    for (const Shipment& sh : s.pipeline) s.initialInTransit += sh.quantity;
    return s;
}

void MultiDaySimulator::beginDay(MultiDayState& s) const {
    if (s.day < 0 || s.day >= scenario_.horizon) throw std::logic_error("day outside the horizon");
    const int t = s.day;
    s.eventsToday.clear();
    // 1. Arrivals: each shipment arrives exactly once, on its arrival day, and leaves the pipeline.
    std::vector<Shipment> stillMoving;
    for (const Shipment& sh : s.pipeline) {
        if (sh.arrivalDay == t) s.stock[sh.to] += sh.quantity;
        else if (sh.arrivalDay > t) stillMoving.push_back(sh);
        else throw std::logic_error("a shipment missed its arrival day");
    }
    s.pipeline = std::move(stillMoving);
    // 2. Scheduled events.
    for (const ScheduledEvent& e : scenario_.events) {
        if (e.day != t) continue;
        const std::string text = applyScheduledEvent(s.network, e.event);
        if (!text.empty()) s.eventsToday.push_back(text);
    }
    // 3. Recovery actions that complete today (after the scheduled events of the same day).
    std::vector<PendingAction> waiting;
    for (const PendingAction& p : s.pending) {
        if (p.effectiveDay == t) {
            applyCompletedAction(s.network, p.action);
            s.eventsToday.push_back(p.action.type == RecoveryActionType::ActivateSupplier
                ? "backup " + s.network.node(p.action.targetId).name + " comes online"
                : "route " + s.network.node(s.network.edge(p.action.targetId).from).name + " -> " + s.network.node(s.network.edge(p.action.targetId).to).name + " reopened");
        } else {
            waiting.push_back(p);
        }
    }
    s.pending = std::move(waiting);
    // 4./5. Capacities are read from the network when planning; observe today's demand.
    for (const Node& node : s.network.nodes()) {
        s.demandToday[node.id] = scenario_.demand[node.id][t];
        s.dueToday[node.id] = s.backlog[node.id] + s.demandToday[node.id];
    }
}

std::vector<RecoveryAction> MultiDaySimulator::candidateActions(const MultiDayState& s) const {
    auto pending = [&](RecoveryActionType type, int target) {
        return std::any_of(s.pending.begin(), s.pending.end(), [&](const PendingAction& p) { return p.action.type == type && p.action.targetId == target; });
    };
    std::vector<RecoveryAction> out;
    for (const Node& node : s.network.nodes()) {
        if (node.isBackup && !node.active && !pending(RecoveryActionType::ActivateSupplier, node.id)) out.push_back({RecoveryActionType::ActivateSupplier, node.id, node.activationCost});
    }
    for (const Edge& e : s.network.edges()) {
        if (!e.active && !pending(RecoveryActionType::ReopenRoute, e.id)) out.push_back({RecoveryActionType::ReopenRoute, e.id, e.reopenCost});
    }
    return out;
}

DayRecord MultiDaySimulator::finishDay(MultiDayState& s, const std::vector<RecoveryAction>& actions, const ForecastProvider& provider, bool snapshots) const {
    const int t = s.day;
    const SupplyChainGraph& g0 = scenario_.network;
    const int n = s.network.nodeCount(), m = s.network.edgeCount();
    DayRecord rec;
    rec.day = t;
    rec.events = s.eventsToday;

    // 6. Pay for and schedule the recovery actions (each must be legal now and bought once).
    const std::vector<RecoveryAction> legal = candidateActions(s);
    for (size_t i = 0; i < actions.size(); ++i) {
        const RecoveryAction& a = actions[i];
        const bool isLegal = std::any_of(legal.begin(), legal.end(), [&](const RecoveryAction& l) { return l.type == a.type && l.targetId == a.targetId && l.cost == a.cost; });
        const bool repeated = std::any_of(actions.begin(), actions.begin() + static_cast<long>(i), [&](const RecoveryAction& b) { return b.type == a.type && b.targetId == a.targetId; });
        if (!isLegal || repeated) throw std::invalid_argument("policy proposed an illegal or repeated recovery action");
        rec.cost.actions += a.cost;
        rec.actions.push_back(a);
        s.purchased.insert({static_cast<int>(a.type), a.targetId});
        const int lead = leadFor(config_, a);
        if (lead == 0) applyCompletedAction(s.network, a);
        else s.pending.push_back({a, t, t + lead});
    }

    // 7. Plan today's shipments with the common planner and the policy's information.
    const auto planStart = std::chrono::steady_clock::now();
    const DispatchPlan plan = planDispatch(provider.planningInput(s, config_.planningWindowDays));
    rec.planningMs = millisecondsSince(planStart);
    rec.plannedSaturatedShare = plan.saturatedShare;

    // 8. Validate against the TRUE network state of today, independently of the planner.
    std::vector<long long> inflowSameDay(n, 0), outflow(n, 0);
    for (const Edge& e : s.network.edges()) {
        const long long q = plan.dispatch[e.id];
        if (q < 0) throw std::logic_error("negative dispatch");
        if (q == 0) continue;
        if (!s.network.isEdgeUsable(e.id) || q > e.capacity) throw std::logic_error("dispatch exceeds today's route capacity");
        outflow[e.from] += q;
        if (scenario_.transitDays[e.id] == 0) inflowSameDay[e.to] += q;
    }
    for (const Node& node : s.network.nodes()) {
        const long long supplyToday = node.active ? node.supply : 0;
        if (plan.draw[node.id] < 0 || plan.draw[node.id] > supplyToday) throw std::logic_error("supply drawn exceeds today's supply");
        if (s.stock[node.id] + plan.draw[node.id] + inflowSameDay[node.id] - outflow[node.id] < 0) throw std::logic_error("dispatch would make inventory negative");
    }

    // 9. Dispatch: draw supply, move goods; zero-day routes deliver now, the rest enter the pipeline.
    std::vector<long long> inTransitByEdge(m, 0);
    for (const Node& node : s.network.nodes()) {
        s.stock[node.id] += plan.draw[node.id] + inflowSameDay[node.id] - outflow[node.id];
        s.totalDrawn += plan.draw[node.id];
        rec.cost.purchase += plan.draw[node.id] * node.unitPurchaseCost;
    }
    for (const Edge& e : s.network.edges()) {
        const long long q = plan.dispatch[e.id];
        if (q == 0) continue;
        rec.cost.transport += q * e.costPerUnit;
        const int arrival = t + scenario_.transitDays[e.id];
        if (arrival > t) s.pipeline.push_back({e.id, e.from, e.to, q, t, arrival});
    }

    // 10./11. Fulfil what is owed from goods on hand, oldest backlog first; the rest is backordered.
    for (const Node& node : s.network.nodes()) {
        if (node.type != NodeType::Customer) continue;
        const int j = node.id;
        const long long served = std::min(s.stock[j], s.dueToday[j]);
        s.stock[j] -= served;
        s.totalServed += served;
        const long long oldBacklogServed = std::min(served, s.backlog[j]);
        rec.newBackorders += s.demandToday[j] - (served - oldBacklogServed);
        s.backlog[j] = s.dueToday[j] - served;
        rec.demand += s.demandToday[j];
        rec.due += s.dueToday[j];
        rec.served += served;
        rec.backlog += s.backlog[j];
    }

    // 12. Daily cost: holding on end-of-day stock everywhere, backlog cost per unit still owed.
    for (const Node& node : s.network.nodes()) {
        rec.cost.holding += s.stock[node.id] * node.holdingCostPerUnit;
        rec.cost.backlog += s.backlog[node.id] * backlogCost_[node.id];
        rec.stock += s.stock[node.id];
    }
    for (const Shipment& sh : s.pipeline) { rec.inTransit += sh.quantity; inTransitByEdge[sh.edgeId] += sh.quantity; }
    s.cumulativeCost += rec.cost.total();
    rec.cumulativeCost = s.cumulativeCost;
    rec.serviceLevel = rec.due > 0 ? static_cast<double>(rec.served) / static_cast<double>(rec.due) : 1.0;

    // Conservation of goods: nothing created, nothing lost.
    if (s.initialStock + s.initialInTransit + s.totalDrawn != rec.stock + rec.inTransit + s.totalServed) throw std::logic_error("conservation of goods violated");
    for (long long v : s.stock) if (v < 0) throw std::logic_error("negative inventory");

    for (const Node& node : s.network.nodes()) if (g0.node(node.id).active && !node.active) ++rec.activeDisruptions;
    for (const Edge& e : s.network.edges()) {
        const Edge& e0 = g0.edge(e.id);
        if ((e0.active && !e.active) || (e.active && e.capacity < e0.capacity)) ++rec.activeDisruptions;
    }
    if (snapshots) {
        rec.stockByNode = s.stock;
        rec.backlogByNode = s.backlog;
        rec.dispatchedByEdge = plan.dispatch;
        rec.inTransitByEdge = inTransitByEdge;
        for (const Node& node : s.network.nodes()) rec.nodeActive.push_back(node.active);
        for (const Edge& e : s.network.edges()) {
            rec.edgeUsable.push_back(s.network.isEdgeUsable(e.id));
            rec.capacityByEdge.push_back(s.network.isEdgeUsable(e.id) ? e.capacity : 0);   // available today
        }
    }
    // 13. Advance.
    s.day += 1;
    return rec;
}

long long MultiDaySimulator::settlement(const MultiDayState& s) const {
    long long total = 0;
    for (const Node& node : s.network.nodes()) total += s.backlog[node.id] * node.shortagePenaltyPerUnit;
    return total;
}

MultiDayRunResult MultiDaySimulator::run(MultiDayPolicy& policy, bool snapshots) const {
    const auto started = std::chrono::steady_clock::now();
    MultiDayRunResult result;
    result.policy = policy.name();
    const ForecastProvider provider(scenario_, config_.forecast, config_);
    MultiDayState state = initialState();
    try {
        for (int t = 0; t < scenario_.horizon; ++t) {
            beginDay(state);
            const PolicyContext context{state, provider, config_, candidateActions(state), scenario_.horizon};
            const auto decideStart = std::chrono::steady_clock::now();
            const std::vector<RecoveryAction> actions = policy.decide(context);
            const double decisionMs = millisecondsSince(decideStart);
            DayRecord rec = finishDay(state, actions, provider, snapshots);
            rec.decisionMs = decisionMs;
            result.decisionMsTotal += decisionMs;
            result.actionCount += static_cast<int>(rec.actions.size());
            result.actionCost += rec.cost.actions;
            result.totalDemand += rec.demand;
            result.days.push_back(std::move(rec));
        }
    } catch (const PolicyNotApplicable& reason) {
        MultiDayRunResult skipped;
        skipped.policy = result.policy;
        skipped.skipped = true;
        skipped.skipReason = reason.what();
        return skipped;
    }
    result.settlement = settlement(state);
    result.totalCost = state.cumulativeCost + result.settlement;
    result.totalServed = state.totalServed;
    if (!result.days.empty()) {
        result.finalBacklog = result.days.back().backlog;
        result.finalInTransit = result.days.back().inTransit;
        result.finalStock = result.days.back().stock;
    }
    result.fillRate = result.totalDemand > 0 ? static_cast<double>(result.totalServed) / static_cast<double>(result.totalDemand) : 1.0;
    std::vector<double> sl;
    for (const DayRecord& d : result.days) sl.push_back(d.serviceLevel);
    if (scenario_.firstDisruptionDay >= 0 && scenario_.firstDisruptionDay < scenario_.horizon) result.normalService = normalService();
    result.recovery = computeRecoveryTime(sl, scenario_.firstDisruptionDay, config_.serviceThreshold, config_.stabilityDays, result.normalService);
    result.runtimeMs = millisecondsSince(started);
    return result;
}

MultiDayScenario MultiDaySimulator::baselineScenario() const {
    MultiDayScenario b = scenario_;
    b.name += "/baseline";
    b.events.clear();
    if (!b.normalDemand.empty()) b.demand = b.normalDemand;
    b.firstDisruptionDay = -1;
    return b;
}

std::vector<double> MultiDaySimulator::normalService() const {
    const MultiDaySimulator baseline(baselineScenario(), config_);
    const ForecastProvider provider(baseline.scenario(), config_.forecast, config_);
    MultiDayState state = baseline.initialState();
    std::vector<double> sl;
    for (int t = 0; t < baseline.horizon(); ++t) {
        baseline.beginDay(state);
        sl.push_back(baseline.finishDay(state, {}, provider, false).serviceLevel);
    }
    return sl;
}

RecoveryTime computeRecoveryTime(const std::vector<double>& sl, int t0, double threshold, int stabilityDays, const std::vector<double>& normal) {
    if (!normal.empty() && normal.size() != sl.size()) throw std::invalid_argument("normal service needs one value per day");
    auto required = [&](int d) { return normal.empty() ? threshold : std::min(threshold, normal[d]); };
    RecoveryTime r;
    const int horizon = static_cast<int>(sl.size());
    if (t0 < 0 || t0 >= horizon) return r;                  // no disruption: not applicable
    r.applicable = true;
    // The impact may be delayed (stock cushions a failure for a few days), so recovery is measured after the FIRST
    // day on or after t0 whose service is below the threshold. If there is no such day, service never suffered.
    int dip = -1;
    for (int d = t0; d < horizon && dip < 0; ++d) if (sl[d] < required(d)) dip = d;
    if (dip < 0) {
        r.recovered = true;
        r.recoveryDay = t0;
        r.days = 0;
        return r;
    }
    for (int start = dip + 1; start + stabilityDays - 1 < horizon; ++start) {
        bool stable = true;
        for (int d = start; d < start + stabilityDays; ++d) stable = stable && sl[d] >= required(d);
        if (stable) {
            r.recovered = true;
            r.recoveryDay = start;
            r.days = start - t0;
            return r;
        }
    }
    return r;                                                // not recovered inside the horizon
}

}  // namespace scx
