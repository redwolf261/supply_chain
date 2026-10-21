#pragma once
// Controlled, reproducible experiments.
//
// One experiment = `runs` seeded scenarios. For run r the network is generated from seed baseSeed + r and a
// disruption is applied; EVERY strategy then solves the identical disrupted network, so strategies are compared
// on the same inputs (paired design). Nothing is fabricated: every number is measured by running the real
// algorithm (cost, unmet demand, utilisation, validity), timed (wall clock) and, when the allocation hooks are
// linked, measured for peak heap use.
//
// What the swept value `x` means depends on the scenario kind:
//   RandomDisruption   x = severity in [0, 1] (supplier failures, route failures/cuts, demand spikes together)
//   SupplierFailures   x = number of regular suppliers that fail (largest first)
//   DemandSpike        x = percent increase of every customer's demand
//   RouteCapacityLoss  x = percent of capacity lost on the busiest quarter of routes (100 = those routes fail)
//   NetworkSize        x = multiplier on every layer size; disruption severity is `severity`

#include <cstdint>
#include <string>
#include <vector>

#include "experiments/Statistics.h"
#include "simulation/NetworkGenerator.h"
#include "simulation/Simulation.h"

namespace scx {

enum class ScenarioKind { RandomDisruption, SupplierFailures, DemandSpike, RouteCapacityLoss, NetworkSize };
const char* toString(ScenarioKind kind);
ScenarioKind scenarioKindFromString(const std::string& text);     // throws std::invalid_argument

struct StrategySpec {
    std::string label;               // unique name in reports, e.g. "bottleneck_aware(lambda=2)"
    std::string strategy;            // a name from strategyNames()
    BottleneckConfig config;
};

struct ExperimentConfig {
    ScenarioKind kind = ScenarioKind::RandomDisruption;
    double x = 0.3;
    double severity = 0.3;           // used by NetworkSize only
    GeneratorParams network;         // base network; its seed is replaced by baseSeed + run
    int runs = 10;
    std::uint64_t baseSeed = 1;
    std::vector<StrategySpec> strategies;
    int exactMaxCandidates = 10;     // `exact` is skipped (and reported as skipped) above this many candidate actions
    int timingRepeats = 1;           // each strategy is run this many times per scenario; the median time is reported
};

struct RunRecord {
    int run = 0;
    std::uint64_t seed = 0;
    std::string label;
    bool skipped = false;
    std::string skipReason;
    int nodes = 0, edges = 0, candidateActions = 0, disruptions = 0, actions = 0;
    long long totalCost = 0, unmetDemand = 0, totalDemand = 0;
    double fulfilment = 1.0, utilisation = 0.0, runtimeMs = 0.0;
    long long peakBytes = -1;        // -1 = not measured (allocation hooks not linked)
    bool feasible = true;
};

struct StrategyAggregate {
    StrategySpec spec;
    int completed = 0, skipped = 0;
    Summary cost, unmet, fulfilment, utilisation, runtimeMs, actions, peakBytes;
    bool memoryMeasured = false;
    double feasibilityRate = 1.0;
    double meanRegretPercent = 0.0;  // mean of (cost - best cost in that run) / best cost, in percent
    int wins = 0;                    // runs in which this strategy had the lowest cost (ties count for all tied)
};

struct ExperimentResult {
    ExperimentConfig config;
    std::vector<RunRecord> records;              // run-major order, one per (run, strategy)
    std::vector<StrategyAggregate> aggregates;   // in the order of config.strategies
    double meanNodes = 0.0, meanEdges = 0.0;
};

// Throws std::invalid_argument for runs < 1, duplicate labels, unknown strategies, x or severity out of range
// for the kind, or an invalid generator configuration.
void validateExperimentConfig(const ExperimentConfig& config);

struct ExperimentScenario {
    SupplyChainGraph initial;
    SupplyChainGraph disrupted;
    int disruptions = 0;
};
// The scenario of run `run` (deterministic in config and run).
ExperimentScenario buildExperimentScenario(const ExperimentConfig& config, int run);

ExperimentResult runExperiment(const ExperimentConfig& config);

// Paired comparison of total cost between two labels over the runs where both completed (a - b).
PairedSummary pairedCost(const ExperimentResult& result, const std::string& labelA, const std::string& labelB);

// The default strategy set for a label list: plain names, bottleneck_aware with `lambda`.
std::vector<StrategySpec> defaultStrategies(const std::vector<std::string>& names, double lambda = 1.0);

}  // namespace scx
