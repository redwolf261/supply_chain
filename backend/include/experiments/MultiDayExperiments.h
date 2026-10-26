#pragma once
// Multi-day experiments: seeded batches of multi-day scenarios in which every policy receives the identical initial
// state, disruption schedule, demand sequence, forecast mode and cost rules (paired design).
//
// Per run and policy we record total cost, fill rate, final backlog, action cost, recovery time (only when defined),
// decision time and feasibility; per day we record service level, backlog, costs and the planned saturated share
// (the bottleneck-risk signal), which lets the analysis test whether that signal predicts later shortages.
// The clairvoyant lower bound is computed once per scenario when the candidate set is small enough.

#include <cstdint>
#include <string>
#include <vector>

#include "experiments/Statistics.h"
#include "multiday/MultiDayPolicies.h"
#include "multiday/MultiDayScenarioBuilder.h"

namespace scx {

struct MultiDayPolicySpec {
    std::string label;          // unique in reports, e.g. "lookahead(5)"
    std::string policy;         // a name from multiDayPolicyNames()
    double lambda = 1.0;
    int lookaheadDays = 5;
    int maxExactCandidates = 8;
};

struct MultiDayExperimentConfig {
    MultiDayScenarioParams scenario;     // its seed is replaced by baseSeed + run
    MultiDayConfig simulation;
    int runs = 10;
    std::uint64_t baseSeed = 1;
    std::vector<MultiDayPolicySpec> policies;
    bool clairvoyant = true;
    int clairvoyantMaxCandidates = 10;
    bool keepDailyRecords = true;
};

struct MultiDayRunRow {
    int run = 0;
    std::uint64_t seed = 0;
    std::string label;
    bool skipped = false;
    std::string skipReason;
    long long totalCost = 0, settlement = 0, actionCost = 0, finalBacklog = 0, totalDemand = 0, totalServed = 0;
    int actions = 0;
    double fillRate = 1.0;
    bool recoveryApplicable = false, recovered = false;
    int recoveryDays = 0;
    double decisionMs = 0.0, runtimeMs = 0.0;
    bool feasible = true;
    bool boundComputed = false;
    long long bound = 0;                 // clairvoyant lower bound for this run's scenario (same for every label)
};

struct MultiDayDailyRow {
    int run = 0;
    std::string label;
    int day = 0;
    long long demand = 0, due = 0, served = 0, backlog = 0, newBackorders = 0, inTransit = 0, stock = 0;
    long long dayCost = 0, backlogCost = 0, cumulativeCost = 0, actionCost = 0;
    double serviceLevel = 1.0, plannedSaturatedShare = 0.0, decisionMs = 0.0;
    double normalService = -1.0;     // service without disruptions or actions; -1 = no disruption in the scenario
    int actions = 0, activeDisruptions = 0;
};

struct MultiDayAggregate {
    MultiDayPolicySpec spec;
    int completed = 0, skipped = 0;
    Summary cost, fillRate, finalBacklog, actionCost, decisionMs, recoveryDays;
    int recoveredRuns = 0, recoveryApplicableRuns = 0;
    double feasibilityRate = 1.0;
    Summary gapToBoundPercent;           // (cost - bound) / bound over runs with a bound
    int wins = 0;                        // runs with the lowest cost among completed policies (ties count for all)
};

struct MultiDayExperimentResult {
    MultiDayExperimentConfig config;
    std::vector<MultiDayRunRow> runs;
    std::vector<MultiDayDailyRow> daily;
    std::vector<MultiDayAggregate> aggregates;
    int boundsComputed = 0;
    double meanNodes = 0.0, meanEdges = 0.0;
};

// Throws std::invalid_argument for runs < 1, no policies, duplicate labels or unknown policy names.
void validateMultiDayExperiment(const MultiDayExperimentConfig& config);
MultiDayExperimentResult runMultiDayExperiment(const MultiDayExperimentConfig& config);

// Paired cost comparison (a - b) over the runs where both completed.
PairedSummary pairedMultiDayCost(const MultiDayExperimentResult& result, const std::string& labelA, const std::string& labelB);

// Default specs for policy names (labels = names; lookahead gets its horizon in the label when not 5).
std::vector<MultiDayPolicySpec> multiDayPolicySpecs(const std::vector<std::string>& names, double lambda = 1.0, int lookaheadDays = 5);

// CSV with `experiment` and `x` as the first two columns so sweeps can be concatenated.
std::string multiDayRunsCsvHeader();
std::string multiDayRunsCsvRows(const std::string& experiment, double x, const MultiDayExperimentResult& result);
std::string multiDayDailyCsvHeader();
std::string multiDayDailyCsvRows(const std::string& experiment, double x, const MultiDayExperimentResult& result);
std::string multiDaySummaryCsvHeader();
std::string multiDaySummaryCsvRows(const std::string& experiment, double x, const MultiDayExperimentResult& result);

// The standard multi-day experiments (definitions in docs/MULTI_DAY_SIMULATION.md, section 13).
struct MultiDayPreset {
    std::string id, title, question, xLabel;
    std::vector<double> xValues;
    MultiDayExperimentConfig config;     // template
    // How x modifies the template (one of: supplier_failures, route_failures, capacity_factor, transit_days,
    // demand_amplitude, penalty_scale, lookahead_days, lambda, forecast, none).
    std::string sweep;
    std::string reference;               // label others are paired against in reports
};
std::vector<MultiDayPreset> standardMultiDayExperiments();
MultiDayExperimentConfig applySweep(const MultiDayPreset& preset, double x);

}  // namespace scx
