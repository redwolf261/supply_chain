#pragma once
// HTTP-independent API logic: takes a request (method, path, query, body), returns a response.
// The network layer (HttpServer) only translates to and from this, so every endpoint can be tested
// directly, and again over a real socket.
//
// Endpoints (all JSON, all under /api):
//   GET  /health                 server status
//   GET  /algorithms             catalog: description, category, complexity of every algorithm
//   GET  /scenarios              standard scenarios, bundled scenario files, strategies
//   POST /scenario/load          {standard,seed} | {file} | {scenario:{...}} | {generate:{...},disruptionSeverity}
//   POST /scenario/reset         {to:"initial"|"disrupted"}
//   GET  /network                current network, metrics, applied/pending events
//   POST /disruption/apply       one event {type,...} or {step:true} to apply the next scripted event
//   POST /algorithm/run          {algorithm, ...parameters, execute?}  (real C++ result, runtime, memory)
//   POST /algorithms/compare     {strategies?: [...]}  same scenario, same objective
//   POST /experiments/run        seeded batch of scenarios, every strategy on identical inputs (see ExperimentRunner.h)
//   POST /multiday/run           5-30 day simulation: every chosen policy on the same scenario, day-by-day timelines
//   GET  /results[?format=csv]   rows of every run/compare so far
//
// The service owns one live Simulation (a single-user local tool) and serialises all requests
// behind a mutex, which also keeps per-run memory measurements clean.

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "simulation/ScenarioIO.h"
#include "simulation/Simulation.h"

namespace scx {

struct ApiRequest {
    std::string method;
    std::string path;                             // e.g. "/api/network"
    std::map<std::string, std::string> query;
    std::string body;
};

struct ApiResponse {
    int status = 200;
    std::string body;
    std::string contentType = "application/json";
};

class ApiService {
public:
    explicit ApiService(std::string dataDirectory);
    ApiResponse handle(const ApiRequest& request);

private:
    using Json = nlohmann::json;
    Json handleHealth();
    Json handleAlgorithmCatalog();
    Json handleScenarioList();
    Json handleLoad(const Json& body);
    Json handleReset(const Json& body);
    Json handleNetwork();
    Json handleDisruption(const Json& body);
    Json handleRun(const Json& body);
    Json handleCompare(const Json& body);
    Json handleExperiment(const Json& body);
    Json handleMultiDay(const Json& body);
    ApiResponse handleResults(const std::map<std::string, std::string>& query);

    Simulation& requireSimulation();
    Json networkSnapshot();                // network + metrics + event lists, used by several endpoints
    void record(const std::string& kind, const std::string& algorithm, const ScenarioMetrics* metrics,
                double runtimeMs, long long peakBytes);   // metrics == nullptr for analysis algorithms

    std::string dataDirectory_;
    std::mutex mutex_;
    std::optional<Simulation> simulation_;
    std::string scenarioName_;
    long long scenarioSeed_ = 0;
    std::optional<RecoveryPlan> executedPlan_;
    std::vector<Json> results_;
    long long nextResultId_ = 1;
};

}  // namespace scx
