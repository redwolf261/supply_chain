#pragma once
// JSON in/out for the simulation. The schema is documented in docs/SYSTEM_ARCHITECTURE.md.
//
// Loading is strict: wrong types, unknown node names, duplicate names, negative quantities and
// structurally invalid networks all raise ScenarioError with a message naming the offending item,
// so the API can return a useful 400 instead of crashing.

#include <stdexcept>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "simulation/Disruption.h"
#include "simulation/Simulation.h"

namespace scx {

class ScenarioError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Scenario {
    std::string name;
    long long seed = 0;
    SupplyChainGraph graph;
    std::vector<Disruption> events;   // optional scripted disruptions
};

Scenario scenarioFromJson(const nlohmann::json& document);
Scenario loadScenarioFile(const std::string& path);   // also throws ScenarioError if unreadable / not JSON

// Round-trips with scenarioFromJson (edges are written by node name, current capacity/active included).
nlohmann::json scenarioToJson(const Scenario& scenario);

// One event: {"type": "supplier_failure", "node": "S1"} | {"type":"route_failure","edge":3} |
// {"type":"capacity_reduction","edge":3,"percentRemaining":40} | {"type":"demand_spike","node":"C1","percentIncrease":50} |
// {"type":"restore_node","node":"S1"} | {"type":"restore_route","edge":3}.
// A node may be given by name (string) or id (integer). Throws ScenarioError.
Disruption disruptionFromJson(const nlohmann::json& document, const SupplyChainGraph& graph);
nlohmann::json disruptionToJson(const SupplyChainGraph& graph, const Disruption& event);

// For the UI / API. Each node and edge carries its ids and, when `plan` is given, its flow.
nlohmann::json networkToJson(const SupplyChainGraph& graph, const FlowSolution* plan = nullptr);
nlohmann::json metricsToJson(const ScenarioMetrics& metrics);
nlohmann::json recoveryPlanToJson(const SupplyChainGraph& disrupted, const RecoveryPlan& plan);
nlohmann::json scenarioRunToJson(const ScenarioRunResult& run);

}  // namespace scx
