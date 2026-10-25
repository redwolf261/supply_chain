#include "multiday/MultiDayScenarioBuilder.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

#include "algorithms/SupplyChainFlow.h"

namespace scx {

namespace {
// Same portable mapping as the network generator: a seed means the same scenario with every standard library.
class Random {
public:
    explicit Random(std::uint64_t seed) : engine_(static_cast<std::mt19937::result_type>((seed * 2654435761ULL) ^ 0x5bd1e995ULL)) {}
    int between(int low, int high) { return low + static_cast<int>(engine_() % static_cast<unsigned>(high - low + 1)); }
    double symmetric() { return static_cast<double>(engine_() % 2000001u) / 1000000.0 - 1.0; }   // [-1, 1]
private:
    std::mt19937 engine_;
};

void check(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }

// The graph has no setters for costs or stock, so changed nodes are applied by rebuilding it with the same ids.
template <typename Change>
SupplyChainGraph rebuild(const SupplyChainGraph& g, Change change) {
    SupplyChainGraph out;
    for (Node n : g.nodes()) { change(n); out.addNode(n); }
    for (const Edge& e : g.edges()) {
        const int id = out.addEdge(e);
        out.setEdgeActive(id, e.active);
        out.setEdgeCapacity(id, e.capacity);
    }
    return out;
}
}  // namespace

MultiDayScenario buildMultiDayScenario(const MultiDayScenarioParams& p) {
    check(p.horizon >= 1 && p.horizon <= 365, "horizon must be between 1 and 365 days");
    check(p.transitMin >= 0 && p.transitMax >= p.transitMin && p.transitMax <= 30, "transit days must satisfy 0 <= min <= max <= 30");
    check(p.demandAmplitude >= 0.0 && p.demandAmplitude <= 1.0, "demand amplitude must be in [0, 1]");
    check(p.spikePercent >= 0 && p.spikePercent <= 1000 && p.spikeStart >= 0 && p.spikeDuration >= 0, "invalid demand spike");
    check(p.supplierFailures >= 0 && p.supplierFailureDay >= 0 && p.supplierFailureDuration >= 0, "invalid supplier failure");
    check(p.routeFailures >= 0 && p.routeFailureDay >= 0 && p.routeFailureDuration >= 0, "invalid route failure");
    check(p.routeCapacityLossPercent >= 1 && p.routeCapacityLossPercent <= 100, "route capacity loss must be between 1 and 100 percent");
    check(p.initialStockDays >= 0.0 && p.initialStockDays <= 30.0, "initial stock must be between 0 and 30 days of demand");
    check(p.penaltyScale > 0.0 && p.penaltyScale <= 100.0, "penalty scale must be in (0, 100]");

    GeneratorParams gp = p.network;
    gp.seed = p.seed;
    const Scenario base = generateNetwork(gp);
    Random random(p.seed);

    long long dailyDemand = 0;
    int warehouses = 0;
    for (const Node& n : base.graph.nodes()) {
        if (n.type == NodeType::Customer) dailyDemand += n.demand;
        if (n.type == NodeType::Warehouse) ++warehouses;
    }
    const int stockPerWarehouse = warehouses > 0 ? static_cast<int>(std::lround(p.initialStockDays * static_cast<double>(dailyDemand) / warehouses)) : 0;

    MultiDayScenario s;
    s.name = "multiday_seed" + std::to_string(p.seed);
    s.seed = p.seed;
    s.horizon = p.horizon;
    s.network = rebuild(base.graph, [&](Node& n) {
        if (n.type == NodeType::Warehouse) n.inventory = stockPerWarehouse;
        else if (n.type != NodeType::Customer) n.inventory = 0;
        if (n.type == NodeType::Customer) n.shortagePenaltyPerUnit = std::max(1, static_cast<int>(std::lround(n.shortagePenaltyPerUnit * p.penaltyScale)));
    });
    const SupplyChainGraph& g = s.network;

    for (int e = 0; e < g.edgeCount(); ++e) s.transitDays.push_back(random.between(p.transitMin, p.transitMax));

    if (p.warmStart) {
        // Steady state of the healthy network: the one-day optimal flow from supplier production alone (stock is a
        // buffer, not a daily source). A route with transit tau holds the batches that left on days -tau .. -1.
        const SupplyChainGraph noStock = rebuild(g, [](Node& n) { n.inventory = 0; });
        const MinCostFlowResult steady = solveMinCostFlow(noStock);
        for (const Edge& e : g.edges()) {
            const long long q = steady.plan.edgeFlow[e.id];
            for (int departure = -s.transitDays[e.id]; departure < 0 && q > 0; ++departure) {
                s.initialPipeline.push_back({e.id, e.from, e.to, q, departure, departure + s.transitDays[e.id]});
            }
        }
    }

    s.demand.assign(g.nodeCount(), std::vector<int>(p.horizon, 0));
    if (p.spikePercent > 0) s.normalDemand = s.demand;
    for (const Node& n : g.nodes()) {
        if (n.type != NodeType::Customer) continue;
        for (int t = 0; t < p.horizon; ++t) {
            double value = n.demand * (1.0 + p.demandAmplitude * random.symmetric());
            if (!s.normalDemand.empty()) s.normalDemand[n.id][t] = std::max(0, static_cast<int>(std::lround(value)));
            if (p.spikePercent > 0 && t >= p.spikeStart && t < p.spikeStart + p.spikeDuration) value *= 1.0 + p.spikePercent / 100.0;
            s.demand[n.id][t] = std::max(0, static_cast<int>(std::lround(value)));
        }
    }

    std::vector<ScheduledEvent> events;
    auto schedule = [&](int day, Disruption failure, Disruption restore, int duration) {
        if (day >= p.horizon) return;
        events.push_back({day, failure});
        if (duration > 0 && day + duration < p.horizon) events.push_back({day + duration, restore});
    };
    std::vector<int> suppliers;
    for (const Node& n : g.nodes()) if (n.type == NodeType::Supplier && !n.isBackup) suppliers.push_back(n.id);
    std::stable_sort(suppliers.begin(), suppliers.end(), [&](int a, int b) { return g.node(a).supply > g.node(b).supply; });
    for (int i = 0; i < std::min<int>(p.supplierFailures, static_cast<int>(suppliers.size())); ++i) {
        schedule(p.supplierFailureDay, {DisruptionType::NodeFailure, suppliers[i], 0}, {DisruptionType::RestoreNode, suppliers[i], 0}, p.supplierFailureDuration);
    }
    if (p.routeFailures > 0) {
        const MinCostFlowResult oneDay = solveMinCostFlow(g);
        std::vector<int> busy;
        for (const Edge& e : g.edges()) if (oneDay.plan.edgeFlow[e.id] > 0) busy.push_back(e.id);
        std::stable_sort(busy.begin(), busy.end(), [&](int a, int b) { return oneDay.plan.edgeFlow[a] > oneDay.plan.edgeFlow[b]; });
        for (int i = 0; i < std::min<int>(p.routeFailures, static_cast<int>(busy.size())); ++i) {
            const Disruption failure = p.routeCapacityLossPercent >= 100
                ? Disruption{DisruptionType::RouteFailure, busy[i], 0}
                : Disruption{DisruptionType::CapacityReduction, busy[i], 100 - p.routeCapacityLossPercent};
            schedule(p.routeFailureDay, failure, {DisruptionType::RestoreRoute, busy[i], 0}, p.routeFailureDuration);
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const ScheduledEvent& a, const ScheduledEvent& b) { return a.day < b.day; });
    s.events = events;

    s.firstDisruptionDay = -1;
    if (!events.empty()) s.firstDisruptionDay = events.front().day;
    if (p.spikePercent > 0 && p.spikeDuration > 0 && p.spikeStart < p.horizon) {
        s.firstDisruptionDay = s.firstDisruptionDay < 0 ? p.spikeStart : std::min(s.firstDisruptionDay, p.spikeStart);
    }
    validateMultiDayScenario(s);
    return s;
}

}  // namespace scx
