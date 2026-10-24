#pragma once
// Data model of the multi-day simulation (design: docs/MULTI_DAY_SIMULATION.md).
//
// The network is the existing single-product SupplyChainGraph with its quantities read PER DAY:
//   node.supply      units a supplier can ship per day (a capacity, it does not accumulate)
//   edge.capacity    units that may depart on a route per day (resets every day)
//   node.inventory   stock on hand at the start of day 0
// plus what only exists in time: transit days per route, a demand sequence, and dated events.

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "graph/SupplyChainGraph.h"
#include "optimization/RecoveryPlan.h"
#include "simulation/Disruption.h"

namespace scx {

// A disruption (or natural recovery) that happens at the start of a given day.
struct ScheduledEvent {
    int day = 0;
    Disruption event;
};

enum class ForecastMode {
    Persistence,   // uncertain forecast: tomorrow looks like today (no knowledge of scheduled events)
    Perfect,       // perfect forecast: the true future network state and demand within the planning window
};
const char* toString(ForecastMode mode);

struct MultiDayConfig {
    int planningWindowDays = 7;          // W: days the dispatch planner looks ahead (clipped at the horizon)
    int activationLeadDays = 1;          // a backup bought on day t supplies from day t + lead
    int repairLeadDays = 1;              // a route reopened on day t carries goods from day t + lead
    double backlogPenaltyFraction = 0.2; // b_j = max(1, round(fraction * p_j)) per unit per day of waiting
    ForecastMode forecast = ForecastMode::Persistence;
    double serviceThreshold = 0.95;      // recovery-time threshold on the daily service level
    int stabilityDays = 3;               // service must stay at or above the threshold this many days
    double saturationThreshold = 0.9;    // a route-day is a bottleneck at this utilisation (bottleneck-aware score)
};

struct Shipment {
    int edgeId = -1;
    int from = -1;
    int to = -1;
    long long quantity = 0;
    int departureDay = 0;
    int arrivalDay = 0;
};

struct MultiDayScenario {
    std::string name;
    std::uint64_t seed = 0;
    SupplyChainGraph network;                  // state at the start of day 0 (before day-0 events)
    int horizon = 0;                           // H days: 0 .. H-1
    std::vector<int> transitDays;              // per edge id, >= 0
    std::vector<std::vector<int>> demand;      // [node id][day]; zero for non-customers
    std::vector<ScheduledEvent> events;        // sorted by day
    int firstDisruptionDay = -1;               // first day with a disruption (event or demand spike); -1 = none
    // Goods already moving at the start of day 0 (warm start). Each departed before day 0 and arrives on
    // departureDay + transitDays[edge] >= 0. Their purchase and transport were paid before the horizon.
    std::vector<Shipment> initialPipeline;
    // Demand without the disruption (the spike removed), used for the normal-service baseline; empty = demand.
    std::vector<std::vector<int>> normalDemand;
};

// Throws std::invalid_argument for inconsistent sizes, negative values, events outside the horizon, or a
// demand-spike event (in the multi-day model spikes are part of the demand sequence, not network events).
void validateMultiDayScenario(const MultiDayScenario& scenario);

// Applies one scheduled event, tolerating the cases a recovery action can create: restoring something that is
// already healthy, or failing something that is already down, does nothing. Returns a description, or "" if
// the event changed nothing.
std::string applyScheduledEvent(SupplyChainGraph& graph, const Disruption& event);

// Backlog cost per unit per day for every node (0 for non-customers).
std::vector<long long> backlogCostPerDay(const SupplyChainGraph& graph, const MultiDayConfig& config);

struct PendingAction {
    RecoveryAction action;
    int decidedDay = 0;
    int effectiveDay = 0;
};

// Everything that changes from day to day. Copyable, so a policy can simulate "what if" on a copy.
struct MultiDayState {
    int day = 0;                                // the day about to be (or being) simulated
    SupplyChainGraph network;
    std::vector<long long> stock;               // per node
    std::vector<long long> backlog;             // per node (customers only)
    std::vector<Shipment> pipeline;             // goods in transit: never part of any stock
    std::vector<PendingAction> pending;         // bought, not yet effective
    std::set<std::pair<int, int>> purchased;    // (action type, target) of every action ever bought
    long long initialStock = 0;
    long long initialInTransit = 0;             // units of the warm-start pipeline
    long long totalDrawn = 0;                   // units produced by suppliers so far
    long long totalServed = 0;                  // units delivered to customers so far
    long long cumulativeCost = 0;
    // Filled by beginDay for the current day:
    std::vector<long long> dueToday;            // backlog + today's demand, per node
    std::vector<long long> demandToday;
    std::vector<std::string> eventsToday;
};

struct DayCosts {
    long long purchase = 0, transport = 0, holding = 0, backlog = 0, actions = 0;
    long long total() const { return purchase + transport + holding + backlog + actions; }
};

struct DayRecord {
    int day = 0;
    DayCosts cost;
    long long cumulativeCost = 0;
    long long demand = 0, due = 0, served = 0;
    long long newBackorders = 0;        // units of TODAY's demand left unserved (older backlog is served first)
    long long backlog = 0;              // units owed at the end of the day
    long long stock = 0, inTransit = 0;
    double serviceLevel = 1.0;          // served / due, 1 when nothing is due
    std::vector<RecoveryAction> actions;   // decided today
    double decisionMs = 0.0, planningMs = 0.0;
    double plannedSaturatedShare = 0.0;
    int activeDisruptions = 0;          // failed nodes + failed or reduced routes, compared with day 0
    std::vector<std::string> events;
    // Snapshots for the dashboard:
    std::vector<long long> stockByNode, dispatchedByEdge, inTransitByEdge, backlogByNode, capacityByEdge;
    std::vector<bool> nodeActive, edgeUsable;
};

struct RecoveryTime {
    bool applicable = false;    // false when the scenario has no disruption at all
    bool recovered = false;     // true when the definition is satisfied inside the horizon
    int days = 0;               // recovery day - first disruption day (only meaningful when recovered)
    int recoveryDay = -1;
};

struct MultiDayRunResult {
    std::string policy;
    bool skipped = false;
    std::string skipReason;
    std::vector<DayRecord> days;
    long long totalCost = 0;            // all daily costs + end-of-horizon settlement
    long long settlement = 0;           // p_j x units still owed after the last day
    long long totalDemand = 0, totalServed = 0, finalBacklog = 0, finalInTransit = 0, finalStock = 0;
    long long actionCost = 0;
    int actionCount = 0;
    double decisionMsTotal = 0.0, runtimeMs = 0.0;
    double fillRate = 1.0;              // served / demand over the horizon
    RecoveryTime recovery;
    std::vector<double> normalService;  // per day: service without disruptions and actions (empty if no disruption)
    bool feasible = true;               // every day validated and goods conserved
};

}  // namespace scx
