#include "advanced/InventoryDP.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace scx {

namespace {
constexpr long long kUnreachable = std::numeric_limits<long long>::max() / 4;

struct DayOutcome {
    long long cost;     // cost of this day only (order + holding + shortage)
    int endStock;
    int lost;
};

// One day's transition: the single place where order/holding/shortage costs are defined, shared by the DP,
// the brute force and the policy evaluator so they cannot disagree about the model.
DayOutcome simulateDay(const InventoryProblem& p, int day, int stock, int orderQuantity) {
    const int available = stock + orderQuantity;
    const int sold = std::min(available, p.demand[day]);
    const int lost = p.demand[day] - sold;
    const int end = available - sold;
    long long cost = 0;
    if (orderQuantity > 0) cost += static_cast<long long>(p.setupCost) + static_cast<long long>(p.unitCost[day]) * orderQuantity;
    cost += static_cast<long long>(p.holdingCost) * end;
    cost += static_cast<long long>(p.shortagePenalty) * lost;
    return {cost, end, lost};
}
}  // namespace

void validateInventoryProblem(const InventoryProblem& p) {
    if (p.demand.size() != p.unitCost.size()) throw std::invalid_argument("demand and unitCost must have one entry per day");
    for (int d : p.demand) if (d < 0) throw std::invalid_argument("demand must be non-negative");
    for (int c : p.unitCost) if (c < 0) throw std::invalid_argument("unit costs must be non-negative");
    if (p.setupCost < 0 || p.holdingCost < 0 || p.shortagePenalty < 0) throw std::invalid_argument("costs must be non-negative");
    if (p.maxInventory < 0 || p.maxOrder < 0) throw std::invalid_argument("capacities must be non-negative");
    if (p.initialInventory < 0 || p.initialInventory > p.maxInventory) throw std::invalid_argument("initial inventory must be within [0, maxInventory]");
}

InventoryPlan solveInventoryDP(const InventoryProblem& p) {
    validateInventoryProblem(p);
    const int days = static_cast<int>(p.demand.size());
    const int capacity = p.maxInventory;

    InventoryPlan plan;
    plan.costToGo.assign(days + 1, std::vector<long long>(capacity + 1, 0));       // row `days` is the base case: all zero
    std::vector<std::vector<int>> bestOrder(days, std::vector<int>(capacity + 1, 0));

    // Fill the table from the last day backwards: costToGo[t] only needs costToGo[t + 1].
    for (int t = days - 1; t >= 0; --t) {
        for (int stock = 0; stock <= capacity; ++stock) {
            long long best = kUnreachable;
            int bestQ = 0;
            const int largestOrder = std::min(p.maxOrder, capacity - stock);          // stock after ordering must fit
            for (int q = 0; q <= largestOrder; ++q) {
                const DayOutcome day = simulateDay(p, t, stock, q);
                const long long total = day.cost + plan.costToGo[t + 1][day.endStock];
                if (total < best) { best = total; bestQ = q; }                        // ties keep the smaller order
            }
            plan.costToGo[t][stock] = best;
            bestOrder[t][stock] = bestQ;
        }
    }

    // Walk forward from the initial stock following the stored decisions.
    plan.totalCost = days == 0 ? 0 : plan.costToGo[0][p.initialInventory];
    int stock = p.initialInventory;
    for (int t = 0; t < days; ++t) {
        const int q = bestOrder[t][stock];
        const DayOutcome day = simulateDay(p, t, stock, q);
        plan.order.push_back(q);
        plan.endInventory.push_back(day.endStock);
        plan.lost.push_back(day.lost);
        stock = day.endStock;
    }
    return plan;
}

long long evaluateInventoryPolicy(const InventoryProblem& p, const std::vector<int>& orders) {
    validateInventoryProblem(p);
    if (orders.size() != p.demand.size()) throw std::invalid_argument("one order quantity per day is required");
    long long total = 0;
    int stock = p.initialInventory;
    for (size_t t = 0; t < orders.size(); ++t) {
        if (orders[t] < 0 || orders[t] > p.maxOrder) throw std::invalid_argument("order exceeds the maximum order size");
        if (stock + orders[t] > p.maxInventory) throw std::invalid_argument("order would exceed warehouse capacity");
        const DayOutcome day = simulateDay(p, static_cast<int>(t), stock, orders[t]);
        total += day.cost;
        stock = day.endStock;
    }
    return total;
}

InventoryPlan solveInventoryBruteForce(const InventoryProblem& p) {
    validateInventoryProblem(p);
    const int days = static_cast<int>(p.demand.size());
    double sequences = 1;
    for (int t = 0; t < days; ++t) sequences *= (p.maxOrder + 1);
    if (sequences > 2.1e6) throw std::invalid_argument("too many order sequences for brute force");

    InventoryPlan best;
    best.totalCost = kUnreachable;
    std::vector<int> orders(days, 0);
    // Depth-first enumeration of every order sequence, pruning only the physically impossible ones.
    auto recurse = [&](auto&& self, int t, int stock, long long costSoFar) -> void {
        if (t == days) {
            if (costSoFar < best.totalCost) { best.totalCost = costSoFar; best.order = orders; }
            return;
        }
        for (int q = 0; q <= p.maxOrder && stock + q <= p.maxInventory; ++q) {
            const DayOutcome day = simulateDay(p, t, stock, q);
            orders[t] = q;
            self(self, t + 1, day.endStock, costSoFar + day.cost);
        }
    };
    recurse(recurse, 0, p.initialInventory, 0);
    if (days == 0) { best.totalCost = 0; best.order.clear(); }

    int stock = p.initialInventory;
    for (int t = 0; t < days; ++t) {
        const DayOutcome day = simulateDay(p, t, stock, best.order[t]);
        best.endInventory.push_back(day.endStock);
        best.lost.push_back(day.lost);
        stock = day.endStock;
    }
    return best;
}

std::vector<int> lotForLotOrders(const InventoryProblem& p) {
    validateInventoryProblem(p);
    std::vector<int> orders;
    int stock = p.initialInventory;
    for (size_t t = 0; t < p.demand.size(); ++t) {
        const int needed = std::max(0, p.demand[t] - stock);
        const int q = std::min({needed, p.maxOrder, p.maxInventory - stock});
        orders.push_back(q);
        stock = stock + q - std::min(stock + q, p.demand[t]);
    }
    return orders;
}

}  // namespace scx
