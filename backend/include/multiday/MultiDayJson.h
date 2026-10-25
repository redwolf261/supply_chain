#pragma once
// JSON conversion for the multi-day simulator (used by POST /api/multiday/run).

#include <string>
#include <vector>

#include "multiday/MultiDayPolicies.h"
#include "multiday/MultiDayScenarioBuilder.h"
#include "nlohmann/json.hpp"

namespace scx {

struct MultiDayRequest {
    MultiDayScenarioParams scenario;
    MultiDayConfig simulation;
    std::vector<std::string> policies;     // names from multiDayPolicyNames()
    double lambda = 1.0;
    int lookaheadDays = 5;
    int maxExactCandidates = 8;
    bool clairvoyant = true;
    int clairvoyantMaxCandidates = 10;
};

// Accepted fields (all optional):
//   seed, horizon, transitMin, transitMax, demandAmplitude, initialStockDays, penaltyScale, warmStart,
//   network: {suppliers, backupSuppliers, warehouses, distributionCenters, customers, capacityFactor},
//   spike: {percent, start, duration},
//   supplierFailures: {count, day, duration},  routeFailures: {count, day, duration, capacityLossPercent},
//   forecast: "persistence" | "perfect", planningWindowDays, activationLeadDays, repairLeadDays, backlogPenaltyFraction,
//   policies: [...], lambda, lookaheadDays, maxExactCandidates, clairvoyant, clairvoyantMaxCandidates.
// Every field is type- and range-checked; throws std::invalid_argument naming the field. Work caps are NOT applied
// here (they are API policy, see ApiService).
MultiDayRequest multiDayRequestFromJson(const nlohmann::json& body);

// The scenario as the dashboard needs it: nodes, routes with transit days, the event schedule, daily demand,
// supply lead and the planning window actually used.
nlohmann::json multiDayScenarioToJson(const MultiDaySimulator& simulator);
// One policy's run, day by day (with per-node and per-route snapshots when the run kept them).
nlohmann::json multiDayRunToJson(const MultiDaySimulator& simulator, const MultiDayRunResult& run);
nlohmann::json clairvoyantToJson(const MultiDaySimulator& simulator, const ClairvoyantBound& bound);

}  // namespace scx
