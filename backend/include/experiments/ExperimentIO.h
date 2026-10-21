#pragma once
// JSON / CSV conversion for experiments, shared by the API and the command-line experiment runner.

#include <string>

#include "experiments/ExperimentRunner.h"
#include "nlohmann/json.hpp"

namespace scx {

// Parses the API request body. Accepted fields (all optional except where noted):
//   kind, x, severity, runs, seed, timingRepeats, exactMaxCandidates, lambda, saturationThreshold,
//   strategies: ["greedy", ...] (names) -- default: every strategy,
//   network: {suppliers, backupSuppliers, warehouses, distributionCenters, customers, edges,
//             capacityFactor, demandMin, demandMax}
// Every field is type- and range-checked; throws std::invalid_argument with a message naming the field.
// Does NOT apply the work caps of the API (see ApiService); those are policy, not parsing.
ExperimentConfig experimentConfigFromJson(const nlohmann::json& body);

nlohmann::json summaryToJson(const Summary& summary);
nlohmann::json pairedToJson(const PairedSummary& paired);

// Aggregates (and optionally the raw per-run records) as JSON. When `pairedAgainst` names a label, every other
// strategy gets a paired cost comparison against it.
nlohmann::json experimentResultToJson(const ExperimentResult& result, bool includeRecords, const std::string& pairedAgainst = "");

// CSV text. `experiment` and `x` are written in the first two columns so sweeps can be concatenated.
std::string summaryCsvHeader();
std::string summaryCsvRows(const std::string& experiment, double x, const ExperimentResult& result);
std::string runsCsvHeader();
std::string runsCsvRows(const std::string& experiment, double x, const ExperimentResult& result);

}  // namespace scx
