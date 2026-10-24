#include "multiday/DispatchPlanner.h"

#include <stdexcept>

#include "algorithms/FlowNetwork.h"

namespace scx {

namespace {
// Large enough that a path through a forced arc is always negative, small enough that M x units cannot overflow.
constexpr long long kForce = 1000000000LL;
constexpr long long kUnlimited = 1000000000000LL;

struct ServeArc { int arc; int node; int day; long long due; long long reward; };
struct RouteArc { int arc; int edge; int day; long long capacity; };
}  // namespace

DispatchPlan planDispatch(const PlanningInput& in) {
    if (!in.graph || !in.transitDays) throw std::invalid_argument("planner needs a graph and transit days");
    const SupplyChainGraph& g = *in.graph;
    const int n = g.nodeCount();
    const int window = static_cast<int>(in.days.size());
    if (window < 1) throw std::invalid_argument("planning window must be at least one day");
    if (static_cast<int>(in.stock.size()) != n || static_cast<int>(in.backlogCost.size()) != n) throw std::invalid_argument("planner input sizes do not match the graph");

    const int source = window * n, sink = window * n + 1;
    auto at = [n](int node, int day) { return day * n + node; };
    FlowNetwork net(window * n + 2);

    long long forcedUnits = 0;
    std::vector<int> stockArc(n, -1), supplyArc(n, -1);
    for (int i = 0; i < n; ++i) {
        if (in.stock[i] < 0) throw std::invalid_argument("negative stock given to the planner");
        if (in.stock[i] > 0) { stockArc[i] = net.addArc(source, at(i, 0), in.stock[i], -kForce); forcedUnits += in.stock[i]; }
    }
    for (int k = 1; k < window && k < static_cast<int>(in.arrivals.size()); ++k) {
        for (int i = 0; i < n; ++i) {
            const long long q = in.arrivals[k][i];
            if (q > 0) { net.addArc(source, at(i, k), q, -kForce); forcedUnits += q; }
        }
    }

    std::vector<RouteArc> routes;
    std::vector<ServeArc> serves;
    long long rewardConstant = 0;
    for (int k = 0; k < window; ++k) {
        const PlanningDay& day = in.days[k];
        if (static_cast<int>(day.supply.size()) != n || static_cast<int>(day.due.size()) != n || static_cast<int>(day.capacity.size()) != g.edgeCount()) {
            throw std::invalid_argument("planning day sizes do not match the graph");
        }
        for (const Node& node : g.nodes()) {
            const int i = node.id;
            if (day.supply[i] > 0) {
                const int arc = net.addArc(source, at(i, k), day.supply[i], node.unitPurchaseCost);
                if (k == 0) supplyArc[i] = arc;
            }
            // Holding: overnight within the window, or at the end of the last day.
            net.addArc(at(i, k), k + 1 < window ? at(i, k + 1) : sink, kUnlimited, node.holdingCostPerUnit);
            if (node.type == NodeType::Customer) {
                if (day.due[i] > 0) {
                    const long long reward = in.backlogCost[i] * (window - k) + node.shortagePenaltyPerUnit;
                    serves.push_back({net.addArc(at(i, k), sink, day.due[i], -reward), i, k, day.due[i], reward});
                    rewardConstant += reward * day.due[i];
                }
                if (k + 1 < window) net.addArc(at(i, k + 1), at(i, k), kUnlimited, in.backlogCost[i]);   // serve late
            }
        }
        for (const Edge& e : g.edges()) {
            const long long cap = day.capacity[e.id];
            const int arrival = k + (*in.transitDays)[e.id];
            if (cap <= 0 || arrival >= window) continue;
            routes.push_back({net.addArc(at(e.from, k), at(e.to, arrival), cap, e.costPerUnit), e.id, k, cap});
        }
    }

    const FlowNetwork::MinCostResult solved = net.minCostFlow(source, sink, FlowNetwork::kInfinity, /*stopAtNonNegativePath=*/true);

    DispatchPlan plan;
    plan.dispatch.assign(g.edgeCount(), 0);
    plan.draw.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        if (stockArc[i] >= 0 && net.flowOnArc(stockArc[i]) != in.stock[i]) throw std::logic_error("planner left stock unaccounted for");
        if (supplyArc[i] >= 0) plan.draw[i] = net.flowOnArc(supplyArc[i]);
    }
    long long saturated = 0;
    for (const RouteArc& r : routes) {
        const long long flow = net.flowOnArc(r.arc);
        if (r.day == 0) plan.dispatch[r.edge] += flow;
        plan.plannedShipped += flow;
        if (flow > 0 && static_cast<double>(flow) >= in.saturationThreshold * static_cast<double>(r.capacity)) saturated += flow;
    }
    plan.saturatedShare = plan.plannedShipped > 0 ? static_cast<double>(saturated) / static_cast<double>(plan.plannedShipped) : 0.0;
    for (const ServeArc& s : serves) {
        const long long unserved = s.due - net.flowOnArc(s.arc);
        plan.plannedUnserved += unserved;
        plan.plannedUnservedPenalty += unserved * g.node(s.node).shortagePenaltyPerUnit;
    }
    // Undo the constants: forced arcs (-M each) and rewards (-R per served unit, i.e. +R per unserved unit).
    plan.plannedCost = solved.cost + kForce * forcedUnits + rewardConstant;
    return plan;
}

}  // namespace scx
