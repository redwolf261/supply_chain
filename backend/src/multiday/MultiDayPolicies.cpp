#include "multiday/MultiDayPolicies.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace scx {

namespace {
constexpr double kEpsilon = 1e-9;

DispatchPlan planWith(const PolicyContext& c, int window, const std::vector<RecoveryAction>& actions) {
    return planDispatch(c.forecast.planningInput(c.state, window, actions));
}
}  // namespace

std::vector<RecoveryAction> ScriptedPolicy::decide(const PolicyContext& context) {
    const auto it = script_.find(context.state.day);
    return it == script_.end() ? std::vector<RecoveryAction>{} : it->second;
}

int daysUntilFirstDelivery(const PolicyContext& context, const RecoveryAction& action) {
    const SupplyChainGraph& g = context.state.network;
    const std::vector<int>& transit = context.forecast.transitDays();
    // Multi-source Dijkstra on transit days. Start: the activated supplier, or the head of the reopened route.
    const long long kFar = std::numeric_limits<long long>::max() / 4;
    std::vector<long long> dist(g.nodeCount(), kFar);
    using Item = std::pair<long long, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    if (action.type == RecoveryActionType::ActivateSupplier) {
        dist[action.targetId] = 0;
        heap.emplace(0, action.targetId);
    } else {
        const Edge& e = g.edge(action.targetId);
        if (!g.node(e.from).active || !g.node(e.to).active) return -1;
        dist[e.to] = transit[e.id];
        heap.emplace(dist[e.to], e.to);
    }
    // The action's own target counts as restored for this search (a backup is still inactive when we ask).
    auto activeOrTarget = [&](int node) {
        return g.node(node).active || (action.type == RecoveryActionType::ActivateSupplier && node == action.targetId);
    };
    auto traversable = [&](int edgeId) {
        const Edge& e = g.edge(edgeId);
        return e.active && e.capacity > 0 && activeOrTarget(e.from) && activeOrTarget(e.to);
    };
    while (!heap.empty()) {
        const auto [d, u] = heap.top();
        heap.pop();
        if (d > dist[u]) continue;
        if (g.node(u).type == NodeType::Customer) return context.leadDays(action) + static_cast<int>(d);
        for (int edgeId : g.outgoingEdgeIds(u)) {
            if (!traversable(edgeId)) continue;
            const Edge& e = g.edge(edgeId);
            if (d + transit[edgeId] < dist[e.to]) { dist[e.to] = d + transit[edgeId]; heap.emplace(dist[e.to], e.to); }
        }
    }
    return -1;
}

// ------------------------------------------------------------------------------------------------ greedy

ImmediateBenefitPolicy::ImmediateBenefitPolicy(Score score, double lambda, std::string label)
    : score_(score), lambda_(lambda), label_(std::move(label)) {
    if (lambda_ < 0) throw std::invalid_argument("lambda must be non-negative");
}

std::vector<RecoveryAction> ImmediateBenefitPolicy::decide(const PolicyContext& c) {
    std::vector<RecoveryAction> chosen;
    std::vector<RecoveryAction> remaining = c.candidates;
    while (!remaining.empty()) {
        int bestIndex = -1;
        double bestScore = 0.0;
        long long bestGain = 0;
        std::map<int, DispatchPlan> baseline;                // planned without the candidate, per window length
        for (size_t i = 0; i < remaining.size(); ++i) {
            const RecoveryAction& a = remaining[i];
            const int delay = daysUntilFirstDelivery(c, a);
            if (delay < 0) continue;                         // cannot reach any customer: no immediate benefit possible
            const int window = delay + 1;                    // up to and including the first day it can help
            if (!baseline.count(window)) baseline.emplace(window, planWith(c, window, chosen));
            const DispatchPlan& base = baseline.at(window);
            std::vector<RecoveryAction> trial = chosen;
            trial.push_back(a);
            const DispatchPlan with = planWith(c, window, trial);
            const long long reduction = base.plannedUnserved - with.plannedUnserved;
            const long long gain = base.plannedCost - with.plannedCost;
            if (reduction <= 0 || gain <= a.cost) continue;   // must reduce shortage AND pay for itself
            double s = 0.0;
            if (score_ == Score::UnitsPerCost) {
                s = static_cast<double>(reduction) / (static_cast<double>(a.cost) + kEpsilon);
            } else {
                const double avoided = static_cast<double>(base.plannedUnservedPenalty - with.plannedUnservedPenalty);
                s = avoided / (static_cast<double>(a.cost) + lambda_ * with.saturatedShare * avoided + kEpsilon);
            }
            if (bestIndex < 0 || s > bestScore || (s == bestScore && gain > bestGain)) { bestIndex = static_cast<int>(i); bestScore = s; bestGain = gain; }
        }
        if (bestIndex < 0) break;
        chosen.push_back(remaining[bestIndex]);
        remaining.erase(remaining.begin() + bestIndex);
    }
    return chosen;
}

// ------------------------------------------------------------------------------------------------ look-ahead

LookaheadPolicy::LookaheadPolicy(int lookaheadDays, double lambda, std::string label)
    : lookaheadDays_(lookaheadDays), lambda_(lambda), label_(label.empty() ? "lookahead" : std::move(label)) {
    if (lookaheadDays_ < 1 || lookaheadDays_ > 60) throw std::invalid_argument("look-ahead must be between 1 and 60 days");
    if (lambda_ < 0) throw std::invalid_argument("lambda must be non-negative");
}

long long LookaheadPolicy::evaluate(const PolicyContext& c, const std::vector<RecoveryAction>& actions) const {
    // The forecast world: same network data, demand and events exactly as the forecast believes them.
    MultiDayConfig config = c.config;
    config.forecast = ForecastMode::Perfect;          // perfect knowledge OF THE FORECAST WORLD, which is all we have
    const MultiDaySimulator world(c.forecast.forecastScenario(c.state, lookaheadDays_), config);
    const ForecastProvider provider(world.scenario(), ForecastMode::Perfect, config);
    MultiDayState s = c.state;                         // copy: the real state is never touched
    long long cost = 0, shortage = 0;
    double risk = 0.0;
    DayRecord first = world.finishDay(s, actions, provider, false);
    cost += first.cost.total();
    risk = first.plannedSaturatedShare;
    while (s.day < world.horizon()) {
        world.beginDay(s);
        cost += world.finishDay(s, {}, provider, false).cost.total();
    }
    shortage = world.settlement(s);
    const double riskTerm = lambda_ * risk * static_cast<double>(shortage);
    return cost + shortage + static_cast<long long>(riskTerm);
}

std::vector<RecoveryAction> LookaheadPolicy::decide(const PolicyContext& c) {
    std::vector<RecoveryAction> chosen;
    std::vector<RecoveryAction> remaining = c.candidates;
    long long current = evaluate(c, chosen);
    while (!remaining.empty()) {
        int bestIndex = -1;
        long long bestValue = current;
        for (size_t i = 0; i < remaining.size(); ++i) {
            std::vector<RecoveryAction> trial = chosen;
            trial.push_back(remaining[i]);
            const long long value = evaluate(c, trial);
            if (value < bestValue) { bestValue = value; bestIndex = static_cast<int>(i); }
        }
        if (bestIndex < 0) break;
        chosen.push_back(remaining[bestIndex]);
        remaining.erase(remaining.begin() + bestIndex);
        current = bestValue;
    }
    return chosen;
}

// ------------------------------------------------------------------------------------------------ rolling exact

std::vector<RecoveryAction> RollingExactPolicy::decide(const PolicyContext& c) {
    const int k = static_cast<int>(c.candidates.size());
    if (k > maxCandidates_) {
        throw PolicyNotApplicable(std::to_string(k) + " candidate actions on day " + std::to_string(c.state.day) +
                                  " exceed the exhaustive-search limit of " + std::to_string(maxCandidates_));
    }
    const int window = c.config.planningWindowDays;
    long long bestValue = planWith(c, window, {}).plannedCost;
    std::vector<RecoveryAction> best;
    for (unsigned mask = 1; mask < (1u << k); ++mask) {
        std::vector<RecoveryAction> subset;
        long long actionCost = 0;
        for (int i = 0; i < k; ++i) if (mask & (1u << i)) { subset.push_back(c.candidates[i]); actionCost += c.candidates[i].cost; }
        const long long value = planWith(c, window, subset).plannedCost + actionCost;
        if (value < bestValue) { bestValue = value; best = subset; }
    }
    return best;
}

// ------------------------------------------------------------------------------------------------ registry

const std::vector<std::string>& multiDayPolicyNames() {
    static const std::vector<std::string> names = {"none", "greedy", "bottleneck_aware", "bottleneck_aware_lambda0", "lookahead", "rolling_exact"};
    return names;
}

std::unique_ptr<MultiDayPolicy> makeMultiDayPolicy(const std::string& name, double lambda, int lookaheadDays, int maxExactCandidates) {
    if (name == "none") return std::make_unique<NoActionPolicy>();
    if (name == "greedy") return std::make_unique<ImmediateBenefitPolicy>(ImmediateBenefitPolicy::Score::UnitsPerCost, 0.0, "greedy");
    if (name == "bottleneck_aware") return std::make_unique<ImmediateBenefitPolicy>(ImmediateBenefitPolicy::Score::BottleneckAware, lambda, "bottleneck_aware");
    if (name == "bottleneck_aware_lambda0") return std::make_unique<ImmediateBenefitPolicy>(ImmediateBenefitPolicy::Score::BottleneckAware, 0.0, "bottleneck_aware_lambda0");
    if (name == "lookahead") return std::make_unique<LookaheadPolicy>(lookaheadDays, 0.0, "lookahead");
    if (name == "rolling_exact") return std::make_unique<RollingExactPolicy>(maxExactCandidates);
    throw std::invalid_argument("unknown multi-day policy '" + name + "'");
}

// ------------------------------------------------------------------------------------------------ clairvoyant

ClairvoyantBound computeClairvoyantBound(const MultiDaySimulator& sim, int maxCandidates) {
    const auto started = std::chrono::steady_clock::now();
    const MultiDayScenario& sc = sim.scenario();
    const MultiDayConfig& config = sim.config();
    ClairvoyantBound bound;

    MultiDayState state = sim.initialState();
    sim.beginDay(state);                                  // day-0 events applied, as every policy sees them
    // Every action that ever becomes possible, at the earliest day it can take effect.
    std::vector<PendingAction> candidates;
    for (const Node& n : state.network.nodes()) {
        if (n.isBackup && !n.active && config.activationLeadDays < sc.horizon) {
            candidates.push_back({{RecoveryActionType::ActivateSupplier, n.id, n.activationCost}, 0, config.activationLeadDays});
        }
    }
    for (const Edge& e : state.network.edges()) {
        if (!e.active && config.repairLeadDays < sc.horizon) candidates.push_back({{RecoveryActionType::ReopenRoute, e.id, e.reopenCost}, 0, config.repairLeadDays});
    }
    for (const ScheduledEvent& ev : sc.events) {
        if (ev.event.type != DisruptionType::RouteFailure || ev.day == 0) continue;
        const int effective = ev.day + config.repairLeadDays;
        const bool duplicate = std::any_of(candidates.begin(), candidates.end(), [&](const PendingAction& p) {
            return p.action.type == RecoveryActionType::ReopenRoute && p.action.targetId == ev.event.targetId; });
        if (effective < sc.horizon && !duplicate) {
            candidates.push_back({{RecoveryActionType::ReopenRoute, ev.event.targetId, sc.network.edge(ev.event.targetId).reopenCost}, ev.day, effective});
        }
    }
    bound.candidates = static_cast<int>(candidates.size());
    if (bound.candidates > maxCandidates) {
        bound.reason = std::to_string(bound.candidates) + " possible actions exceed the limit of " + std::to_string(maxCandidates);
        return bound;
    }
    const ForecastProvider perfect(sc, ForecastMode::Perfect, config);
    const int k = bound.candidates;
    bool first = true;
    for (unsigned mask = 0; mask < (1u << k); ++mask) {
        std::vector<PendingAction> subset;
        long long actionCost = 0;
        for (int i = 0; i < k; ++i) if (mask & (1u << i)) { subset.push_back(candidates[i]); actionCost += candidates[i].action.cost; }
        const long long value = planDispatch(perfect.planningInputWith(state, sc.horizon, subset)).plannedCost + actionCost;
        if (first || value < bound.cost) { bound.cost = value; bound.actions = subset; first = false; }
    }
    bound.computed = true;
    bound.runtimeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return bound;
}

}  // namespace scx
