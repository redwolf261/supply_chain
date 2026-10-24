#pragma once
// The common dispatch planner of the multi-day simulation: a time-expanded minimum-cost flow over the next W days
// (docs/MULTI_DAY_SIMULATION.md, section 6). Every policy uses it for shipments; policies only differ in which
// recovery actions they buy.
//
// DATA STRUCTURE. The planning network has one copy (i, k) of every node i for every day k = 0..W-1 of the window
// (k = 0 is today), plus a source and a sink. Node (i, k) has index k * n + i.
//   source -> (i, 0)        stock on hand                 cost -M  (must be accounted for)
//   source -> (j, k)        pipeline arrivals on day k    cost -M
//   source -> (i, k)        expected supply on day k      cost a_i
//   (i, k) -> (j, k + L_e)  route e = (i, j), departs day k, capacity of that day, cost c_e (only if it arrives inside the window)
//   (i, k) -> (i, k + 1)    hold overnight                cost h_i
//   (i, W-1) -> sink        stock left at the end         cost h_i
//   (j, k) -> sink          serve what is owed on day k   cost -R_jk,  R_jk = b_j (W-1-k+1) + p_j
//   (j, k+1) -> (j, k)      serve day-k demand one day late, cost b_j
// Only arcs leaving the source or entering the sink are negative, so there is no negative cycle and the existing
// solver (Bellman-Ford start, Dijkstra on reduced costs) applies. It stops at the first non-negative path, so goods
// only move when that pays.
//
// SIZE. About nW + 2 nodes and (m + 3n) W arcs for n nodes and m routes. One plan = one min-cost flow on it.

#include <vector>

#include "graph/SupplyChainGraph.h"

namespace scx {

// What the planner may assume about one day of the window.
struct PlanningDay {
    std::vector<long long> supply;     // per node: units available that day (0 if inactive)
    std::vector<long long> capacity;   // per edge: units that may depart that day (0 if unusable)
    std::vector<long long> due;        // per node: units owed that day (day 0: backlog + demand; later: expected demand)
};

struct PlanningInput {
    const SupplyChainGraph* graph = nullptr;   // costs, node types (statuses come from `days`)
    const std::vector<int>* transitDays = nullptr;
    std::vector<PlanningDay> days;             // days[0] = today; size W >= 1
    std::vector<long long> stock;              // per node, now
    std::vector<std::vector<long long>> arrivals;   // [k][node]: pipeline goods arriving on day k (k >= 1)
    std::vector<long long> backlogCost;        // b_j per node
    double saturationThreshold = 0.9;
};

struct DispatchPlan {
    std::vector<long long> dispatch;           // per edge: units departing TODAY
    std::vector<long long> draw;               // per node: units produced TODAY
    long long plannedCost = 0;                 // purchase + transport + holding + backlog + unserved valuation over the window
    long long plannedUnserved = 0;             // units still owed at the end of the window
    long long plannedUnservedPenalty = 0;      // sum of p_j over those units
    double saturatedShare = 0.0;               // share of planned shipments on route-days at >= threshold utilisation
    long long plannedShipped = 0;              // units moved on routes during the window
};

// Throws std::invalid_argument for an inconsistent input.
DispatchPlan planDispatch(const PlanningInput& input);

}  // namespace scx
