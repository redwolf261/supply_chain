#include "api/ApiService.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <sstream>

#include "algorithms/Dijkstra.h"
#include "algorithms/GraphTraversal.h"
#include "algorithms/SupplyChainFlow.h"
#include "experiments/ExperimentIO.h"
#include "multiday/MultiDayJson.h"
#include "simulation/NetworkGenerator.h"
#include "util/AllocationTracker.h"

namespace scx {

using Json = nlohmann::json;

namespace {

constexpr const char* kVersion = "0.6.0";
constexpr int kMaxExactCandidates = 16;
constexpr size_t kMaxStoredResults = 500;
// Work limits for POST /experiments/run (the server is single-threaded per request): at most 200 runs,
// 400 nodes, 5 timing repeats, and runs x nodes <= 6000 so one request stays within a few seconds.
constexpr int kMaxExperimentRuns = 200;
constexpr int kMaxExperimentNodes = 400;
constexpr int kMaxExperimentRepeats = 5;
constexpr long long kMaxExperimentWork = 6000;
// Limits for POST /multiday/run: the horizon range of the multi-day model as specified (5-30 days), small networks,
// and caps on the look-ahead and the two exhaustive searches (2^k plans) so a request stays within seconds.
constexpr int kMinMultiDayHorizon = 5;
constexpr int kMaxMultiDayHorizon = 30;
constexpr int kMaxMultiDayNodes = 60;
constexpr int kMaxMultiDayLookahead = 12;
constexpr int kMaxMultiDayExhaustive = 10;

// Thrown anywhere in a handler; translated into a JSON error body with this HTTP status.
class ApiError : public std::runtime_error {
public:
    ApiError(int status, std::string code, const std::string& message)
        : std::runtime_error(message), status(status), code(std::move(code)) {}
    int status;
    std::string code;
};

// ---- request-body helpers: every field is type-checked so bad input becomes a 400, never a crash ----

std::optional<std::string> optionalString(const Json& body, const char* key) {
    if (!body.contains(key) || body.at(key).is_null()) return std::nullopt;
    if (!body.at(key).is_string()) throw ApiError(400, "invalid_field", std::string("'") + key + "' must be text");
    return body.at(key).get<std::string>();
}
std::string requireString(const Json& body, const char* key) {
    auto value = optionalString(body, key);
    if (!value) throw ApiError(400, "missing_field", std::string("missing required text field '") + key + "'");
    return *value;
}
std::optional<long long> optionalInteger(const Json& body, const char* key, long long low, long long high) {
    if (!body.contains(key) || body.at(key).is_null()) return std::nullopt;
    if (!body.at(key).is_number_integer()) throw ApiError(400, "invalid_field", std::string("'") + key + "' must be an integer");
    const long long value = body.at(key).get<long long>();
    if (value < low || value > high) {
        throw ApiError(400, "invalid_field", std::string("'") + key + "' must be between " + std::to_string(low) + " and " + std::to_string(high));
    }
    return value;
}
std::optional<double> optionalNumber(const Json& body, const char* key, double low, double high) {
    if (!body.contains(key) || body.at(key).is_null()) return std::nullopt;
    if (!body.at(key).is_number()) throw ApiError(400, "invalid_field", std::string("'") + key + "' must be a number");
    const double value = body.at(key).get<double>();
    if (!(value >= low && value <= high)) {
        throw ApiError(400, "invalid_field", std::string("'") + key + "' must be between " + std::to_string(low) + " and " + std::to_string(high));
    }
    return value;
}
bool optionalBool(const Json& body, const char* key, bool fallback) {
    if (!body.contains(key) || body.at(key).is_null()) return fallback;
    if (!body.at(key).is_boolean()) throw ApiError(400, "invalid_field", std::string("'") + key + "' must be true or false");
    return body.at(key).get<bool>();
}

int resolveNode(const Json& body, const char* key, const SupplyChainGraph& graph) {
    if (!body.contains(key)) throw ApiError(400, "missing_field", std::string("missing node reference '") + key + "'");
    const Json& ref = body.at(key);
    if (ref.is_number_integer()) {
        const long long id = ref.get<long long>();
        if (id < 0 || id >= graph.nodeCount()) throw ApiError(400, "unknown_node", "node id " + std::to_string(id) + " does not exist");
        return static_cast<int>(id);
    }
    if (ref.is_string()) {
        for (const Node& node : graph.nodes()) if (node.name == ref.get<std::string>()) return node.id;
        throw ApiError(400, "unknown_node", "unknown node '" + ref.get<std::string>() + "'");
    }
    throw ApiError(400, "invalid_field", std::string("'") + key + "' must be a node name or id");
}

Json nodeRef(const SupplyChainGraph& graph, int id) { return {{"id", id}, {"name", graph.node(id).name}}; }
Json nodeRefs(const SupplyChainGraph& graph, const std::vector<int>& ids) {
    Json list = Json::array();
    for (int id : ids) list.push_back(nodeRef(graph, id));
    return list;
}

// ---- timing + memory ----

struct Measurement {
    double milliseconds = 0.0;
    long long peakBytes = -1;   // -1 = hooks not installed, memory not measured
};
Measurement measure(const std::function<void()>& work) {
    AllocationScope scope;
    const auto started = std::chrono::steady_clock::now();
    work();
    Measurement m;
    m.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (AllocationTracker::isActive()) m.peakBytes = static_cast<long long>(scope.peakBytesSinceStart());
    return m;
}
Json memoryJson(long long bytes) { return bytes < 0 ? Json(nullptr) : Json(bytes); }

struct AlgorithmInfo {
    const char* id; const char* title; const char* category; const char* description;
    const char* timeComplexity; const char* spaceComplexity; const char* parameters;
};
const std::vector<AlgorithmInfo>& catalog() {
    static const std::vector<AlgorithmInfo> items = {
        {"bfs", "Breadth-First Search", "analysis", "Which facilities can goods from a source still reach, and in how few hops? Reports what a disruption cut off compared with the initial network.", "O(V + E)", "O(V)", "source"},
        {"dfs", "Depth-First Search", "analysis", "Explores everything downstream of a source along edge direction; also reports finish order.", "O(V + E)", "O(V)", "source"},
        {"dijkstra", "Dijkstra shortest path", "analysis", "Cheapest per-unit route from a source. Ignores capacities, so it cannot allocate network-wide shipments.", "O((V + E) log V)", "O(V)", "source, target (optional)"},
        {"max_flow", "Maximum flow (Edmonds-Karp)", "analysis", "The most goods the network can deliver regardless of cost, plus the minimum cut that limits it.", "O(V * E^2)", "O(V + E)", ""},
        {"min_cost_flow", "Minimum-cost flow", "recovery", "Cheapest shipment plan on the network as it is (no paid recovery actions). Optimal for the current set of active suppliers and routes.", "O(V*E + F*E*log V), F = units shipped", "O(V + E)", ""},
        {"dijkstra_dispatch", "Shortest-path dispatch (baseline)", "recovery", "Serves customers one at a time along the cheapest path with spare capacity; never undoes a shipment, so it can be suboptimal.", "O(D * (V + E) log V), D = total demand", "O(V + E)", ""},
        {"greedy", "Greedy recovery", "recovery", "Repeatedly takes the recovery action with the best unmet-demand-removed-per-cost ratio. A heuristic, not optimal.", "O(k^2) min-cost-flow solves, k = candidate actions", "O(V + E) per trial copy", ""},
        {"bottleneck_aware", "Bottleneck-aware recovery (experimental)", "recovery", "Greedy with a penalty for plans whose flow rides on near-saturated routes. Experimental: no superiority is claimed.", "O(k^2) min-cost-flow solves", "O(V + E) per trial copy", "lambda, saturationThreshold"},
        {"bottleneck_aware_lambda0", "Bottleneck-aware, risk term off (ablation)", "recovery", "Same scoring without the bottleneck-risk term, to isolate the term's effect.", "O(k^2) min-cost-flow solves", "O(V + E) per trial copy", ""},
        {"exact", "Exhaustive recovery search (reference)", "recovery", "Tries every subset of recovery actions; the true optimum for small instances.", "O(2^k) min-cost-flow solves", "O(V + E)", ""},
    };
    return items;
}
const AlgorithmInfo* findAlgorithm(const std::string& id) {
    for (const AlgorithmInfo& info : catalog()) if (id == info.id) return &info;
    return nullptr;
}
Json complexityJson(const AlgorithmInfo& info) { return {{"time", info.timeComplexity}, {"space", info.spaceComplexity}}; }

BottleneckConfig configFrom(const Json& body) {
    BottleneckConfig config;
    if (auto lambda = optionalNumber(body, "lambda", 0.0, 1e6)) config.lambda = *lambda;
    if (auto threshold = optionalNumber(body, "saturationThreshold", 0.01, 1.0)) config.saturationThreshold = *threshold;
    return config;
}

std::string utcTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return text;
}

std::string csvEscape(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

ApiResponse jsonResponse(int status, const Json& body) { return {status, body.dump(), "application/json"}; }
ApiResponse errorResponse(int status, const std::string& code, const std::string& message) {
    return jsonResponse(status, {{"error", {{"code", code}, {"message", message}}}});
}

bool safeFileStem(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

}  // namespace

ApiService::ApiService(std::string dataDirectory) : dataDirectory_(std::move(dataDirectory)) {}

Simulation& ApiService::requireSimulation() {
    if (!simulation_) throw ApiError(409, "no_scenario", "no scenario is loaded; POST /api/scenario/load first");
    return *simulation_;
}

void ApiService::record(const std::string& kind, const std::string& algorithm, const ScenarioMetrics* metrics,
                        double runtimeMs, long long peakBytes) {
    Json row = {{"id", nextResultId_++}, {"timestamp", utcTimestamp()}, {"scenario", scenarioName_}, {"kind", kind},
                {"algorithm", algorithm}, {"eventsApplied", simulation_ ? simulation_->appliedEvents().size() : 0},
                {"runtimeMs", runtimeMs}, {"peakMemoryBytes", memoryJson(peakBytes)}};
    if (metrics) {
        row["totalCost"] = metrics->totalCost;
        row["unmetDemand"] = metrics->unmetDemand;
        row["fulfilmentRatio"] = metrics->fulfilmentRatio;
        row["networkUtilisation"] = metrics->networkUtilisation;
        row["feasible"] = metrics->feasible;
    }
    results_.push_back(row);
    if (results_.size() > kMaxStoredResults) results_.erase(results_.begin());
}

ApiResponse ApiService::handle(const ApiRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        const std::string& m = request.method;
        const std::string& path = request.path;
        auto parseBody = [&]() -> Json {
            if (request.body.empty()) return Json::object();
            Json parsed = Json::parse(request.body, nullptr, /*allow_exceptions=*/false);
            if (parsed.is_discarded()) throw ApiError(400, "invalid_json", "request body is not valid JSON");
            if (!parsed.is_object()) throw ApiError(400, "invalid_json", "request body must be a JSON object");
            return parsed;
        };
        struct Route { const char* method; const char* path; std::function<ApiResponse()> run; };
        const std::vector<Route> routes = {
            {"GET", "/api/health", [&] { return jsonResponse(200, handleHealth()); }},
            {"GET", "/api/algorithms", [&] { return jsonResponse(200, handleAlgorithmCatalog()); }},
            {"GET", "/api/scenarios", [&] { return jsonResponse(200, handleScenarioList()); }},
            {"POST", "/api/scenario/load", [&] { return jsonResponse(200, handleLoad(parseBody())); }},
            {"POST", "/api/scenario/reset", [&] { return jsonResponse(200, handleReset(parseBody())); }},
            {"GET", "/api/network", [&] { return jsonResponse(200, handleNetwork()); }},
            {"POST", "/api/disruption/apply", [&] { return jsonResponse(200, handleDisruption(parseBody())); }},
            {"POST", "/api/algorithm/run", [&] { return jsonResponse(200, handleRun(parseBody())); }},
            {"POST", "/api/algorithms/compare", [&] { return jsonResponse(200, handleCompare(parseBody())); }},
            {"POST", "/api/experiments/run", [&] { return jsonResponse(200, handleExperiment(parseBody())); }},
            {"POST", "/api/multiday/run", [&] { return jsonResponse(200, handleMultiDay(parseBody())); }},
            {"GET", "/api/results", [&] { return handleResults(request.query); }},
        };
        bool pathKnown = false;
        for (const Route& route : routes) {
            if (path != route.path) continue;
            pathKnown = true;
            if (m == route.method) return route.run();
        }
        if (pathKnown) return errorResponse(405, "method_not_allowed", m + " is not allowed on " + path);
        return errorResponse(404, "not_found", "no such endpoint: " + path);
    } catch (const ApiError& error) {
        return errorResponse(error.status, error.code, error.what());
    } catch (const ScenarioError& error) {
        return errorResponse(400, "invalid_scenario", error.what());
    } catch (const nlohmann::json::exception& error) {
        return errorResponse(400, "invalid_json", error.what());
    } catch (const std::invalid_argument& error) {
        return errorResponse(400, "invalid_request", error.what());
    } catch (const std::out_of_range& error) {
        return errorResponse(400, "invalid_request", error.what());
    } catch (const std::logic_error& error) {
        return errorResponse(409, "conflict", error.what());
    } catch (const std::exception& error) {
        return errorResponse(500, "internal_error", error.what());
    }
}

// ------------------------------------------------------------------------------------------------

Json ApiService::handleHealth() {
    return {{"status", "ok"}, {"version", kVersion}, {"scenarioLoaded", simulation_.has_value()},
            {"memoryTrackingActive", AllocationTracker::isActive()}};
}

Json ApiService::handleAlgorithmCatalog() {
    Json items = Json::array();
    for (const AlgorithmInfo& a : catalog()) {
        items.push_back({{"id", a.id}, {"title", a.title}, {"category", a.category}, {"description", a.description},
                         {"complexity", complexityJson(a)}, {"parameters", a.parameters}});
    }
    return {{"algorithms", items}};
}

Json ApiService::handleScenarioList() {
    std::vector<std::string> files;
    const std::filesystem::path directory = std::filesystem::path(dataDirectory_) / "scenarios";
    std::error_code ignored;
    if (std::filesystem::is_directory(directory, ignored)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory, ignored)) {
            if (entry.path().extension() == ".json") files.push_back(entry.path().stem().string());
        }
    }
    std::sort(files.begin(), files.end());
    return {{"standard", standardScenarioNames()}, {"files", files}, {"strategies", strategyNames()}};
}

Json ApiService::handleLoad(const Json& body) {
    Scenario scenario;
    if (body.contains("standard")) {
        const std::string name = requireString(body, "standard");
        const auto seed = optionalInteger(body, "seed", 0, 1000000000LL).value_or(1);
        scenario = buildStandardScenario(name, static_cast<std::uint64_t>(seed));
    } else if (body.contains("file")) {
        const std::string stem = requireString(body, "file");
        if (!safeFileStem(stem)) throw ApiError(400, "invalid_field", "'file' must be a plain scenario name (letters, digits, _ and -)");
        scenario = loadScenarioFile((std::filesystem::path(dataDirectory_) / "scenarios" / (stem + ".json")).string());
    } else if (body.contains("scenario")) {
        scenario = scenarioFromJson(body.at("scenario"));
    } else if (body.contains("generate")) {
        const Json& g = body.at("generate");
        if (!g.is_object()) throw ApiError(400, "invalid_field", "'generate' must be an object");
        GeneratorParams params;
        auto number = [&](const char* key, int& target, long long low, long long high) {
            if (auto v = optionalInteger(g, key, low, high)) target = static_cast<int>(*v);
        };
        number("suppliers", params.suppliers, 1, 500);
        number("backupSuppliers", params.backupSuppliers, 0, 100);
        number("warehouses", params.warehouses, 1, 500);
        number("distributionCenters", params.distributionCenters, 1, 500);
        number("customers", params.customers, 1, 2000);
        number("edges", params.edges, 0, 20000);
        number("demandMin", params.demandMin, 1, 1000000);
        number("demandMax", params.demandMax, 1, 1000000);
        if (auto factor = optionalNumber(g, "capacityFactor", 0.05, 100.0)) params.capacityFactor = *factor;
        if (auto seed = optionalInteger(g, "seed", 0, 1000000000LL)) params.seed = static_cast<std::uint64_t>(*seed);
        scenario = generateNetwork(params);
        if (auto severity = optionalNumber(body, "disruptionSeverity", 0.0, 1.0)) {
            scenario.events = generateDisruptions(scenario.graph, *severity, params.seed);
        }
    } else {
        throw ApiError(400, "missing_field", "provide one of: standard, file, scenario, generate");
    }

    Simulation fresh(scenario.graph);          // validates; throws before touching the live state
    fresh.loadScript(scenario.events);
    simulation_.emplace(std::move(fresh));
    scenarioName_ = scenario.name;
    scenarioSeed_ = scenario.seed;
    executedPlan_.reset();
    return networkSnapshot();
}

Json ApiService::handleReset(const Json& body) {
    Simulation& sim = requireSimulation();
    const std::string target = optionalString(body, "to").value_or("initial");
    if (target == "initial") {
        sim.resetToInitial();
    } else if (target == "disrupted") {
        if (!sim.recoveryExecuted()) throw ApiError(409, "nothing_to_undo", "no recovery has been executed, so there is no disrupted checkpoint to return to");
        sim.resetToDisruptedState();
    } else {
        throw ApiError(400, "invalid_field", "'to' must be \"initial\" or \"disrupted\"");
    }
    executedPlan_.reset();
    return networkSnapshot();
}

Json ApiService::networkSnapshot() {
    Simulation& sim = requireSimulation();
    if (!sim.recoveryExecuted()) executedPlan_.reset();

    FlowSolution flow;
    ScenarioMetrics metrics;
    std::string flowSource;
    if (executedPlan_ && sim.stateBeforeRecovery()) {
        metrics = evaluatePlan(sim.initial(), *sim.stateBeforeRecovery(), *executedPlan_);
        flow = executedPlan_->flow;
        flowSource = "executed plan: " + executedPlan_->algorithm;
    } else {
        const RecoveryPlan plan = runStrategy("min_cost_flow", sim.current());
        metrics = sim.metricsFor(plan);
        flow = plan.flow;
        flowSource = "min_cost_flow (optimal re-routing on the current network, no paid actions)";
    }

    Json applied = Json::array(), pending = Json::array();
    for (const Disruption& e : sim.appliedEvents()) applied.push_back(disruptionToJson(sim.current(), e));
    for (size_t i = sim.nextScriptIndex(); i < sim.scriptEvents().size(); ++i) pending.push_back(disruptionToJson(sim.current(), sim.scriptEvents()[i]));
    return {{"scenario", {{"name", scenarioName_}, {"seed", scenarioSeed_}}},
            {"network", networkToJson(sim.current(), &flow)}, {"metrics", metricsToJson(metrics)},
            {"flowSource", flowSource}, {"recoveryExecuted", sim.recoveryExecuted()},
            {"appliedEvents", applied}, {"pendingEvents", pending}};
}

Json ApiService::handleNetwork() { requireSimulation(); return networkSnapshot(); }

Json ApiService::handleDisruption(const Json& body) {
    Simulation& sim = requireSimulation();
    Json response;
    if (optionalBool(body, "step", false)) {
        auto description = sim.stepForward();
        if (!description) throw ApiError(409, "script_finished", "no scripted events remain");
        response = {{"applied", *description}};
    } else {
        const Disruption event = disruptionFromJson(body, sim.current());
        const std::string description = describe(sim.current(), event);
        sim.applyDisruption(event);
        response = {{"applied", description}};
    }
    executedPlan_.reset();
    Json snapshot = networkSnapshot();
    snapshot["applied"] = response["applied"];
    return snapshot;
}

Json ApiService::handleRun(const Json& body) {
    Simulation& sim = requireSimulation();
    const std::string name = requireString(body, "algorithm");
    const AlgorithmInfo* info = findAlgorithm(name);
    if (!info) throw ApiError(400, "unknown_algorithm", "unknown algorithm '" + name + "'; see GET /api/algorithms");
    const SupplyChainGraph& g = sim.current();
    const bool execute = optionalBool(body, "execute", false);
    Json out = {{"algorithm", name}, {"title", info->title}, {"category", info->category}, {"complexity", complexityJson(*info)}};

    if (name == "bfs" || name == "dfs") {
        if (execute) throw ApiError(400, "invalid_field", "'execute' only applies to recovery strategies");
        const int source = resolveNode(body, "source", g);
        TraversalResult result;
        const Measurement m = measure([&] { result = name == "bfs" ? breadthFirstSearch(g, source) : depthFirstSearch(g, source); });
        const TraversalResult initialView = name == "bfs" ? breadthFirstSearch(sim.initial(), source) : depthFirstSearch(sim.initial(), source);
        std::vector<int> unreachable, treeEdges;
        Json hops = Json::array();
        for (int v = 0; v < g.nodeCount(); ++v) {
            if (!result.reached[v]) unreachable.push_back(v);
            if (result.parentEdge[v] >= 0) treeEdges.push_back(result.parentEdge[v]);
            if (name == "bfs" && result.reached[v]) hops.push_back({{"node", g.node(v).name}, {"hops", result.distanceInHops[v]}});
        }
        out["result"] = {{"source", nodeRef(g, source)}, {"visitOrder", nodeRefs(g, result.visitOrder)},
                         {"reachedCount", result.visitOrder.size()}, {"unreachable", nodeRefs(g, unreachable)},
                         {"treeEdgeIds", treeEdges}, {"lostSinceInitial", nodeRefs(g, nodesLostBetween(initialView, result))}};
        if (name == "bfs") out["result"]["hops"] = hops;
        else out["result"]["finishOrder"] = nodeRefs(g, result.finishOrder);
        out["runtimeMs"] = m.milliseconds;
        out["peakMemoryBytes"] = memoryJson(m.peakBytes);
        record("analysis", name, nullptr, m.milliseconds, m.peakBytes);
        return out;
    }

    if (name == "dijkstra") {
        if (execute) throw ApiError(400, "invalid_field", "'execute' only applies to recovery strategies");
        const int source = resolveNode(body, "source", g);
        ShortestPathResult result;
        const Measurement m = measure([&] { result = dijkstra(g, source); });
        Json distances = Json::array();
        std::vector<int> treeEdges;
        for (int v = 0; v < g.nodeCount(); ++v) {
            distances.push_back({{"node", g.node(v).name}, {"cost", result.isReachable(v) ? Json(result.distance[v]) : Json(nullptr)}});
            if (result.parentEdge[v] >= 0) treeEdges.push_back(result.parentEdge[v]);
        }
        out["result"] = {{"source", nodeRef(g, source)}, {"distances", distances}, {"treeEdgeIds", treeEdges}};
        if (body.contains("target") && !body.at("target").is_null()) {
            const int target = resolveNode(body, "target", g);
            out["result"]["target"] = nodeRef(g, target);
            out["result"]["reachable"] = result.isReachable(target);
            out["result"]["pathNodes"] = nodeRefs(g, result.nodePathTo(target));
            out["result"]["pathEdgeIds"] = result.edgePathTo(target);
            out["result"]["cost"] = result.isReachable(target) ? Json(result.distance[target]) : Json(nullptr);
        }
        out["runtimeMs"] = m.milliseconds;
        out["peakMemoryBytes"] = memoryJson(m.peakBytes);
        record("analysis", name, nullptr, m.milliseconds, m.peakBytes);
        return out;
    }

    if (name == "max_flow") {
        if (execute) throw ApiError(400, "invalid_field", "'execute' only applies to recovery strategies");
        MaxFlowResult result;
        const Measurement m = measure([&] { result = computeMaxFlow(g); });
        FlowSolution overlay = FlowSolution::empty(g);
        overlay.edgeFlow = result.edgeFlow;
        std::vector<int> demandLimited = result.demandLimitedNodeIds, supplyLimited = result.supplyLimitedNodeIds;
        out["result"] = {{"maxDeliverable", result.maxDeliverable}, {"totalDemand", result.totalDemand},
                         {"minCutCapacity", result.minCutCapacity}, {"minCutEdgeIds", result.minCutEdgeIds},
                         {"supplyLimitedNodes", nodeRefs(g, supplyLimited)}, {"demandLimitedNodes", nodeRefs(g, demandLimited)},
                         {"network", networkToJson(g, &overlay)}};
        out["runtimeMs"] = m.milliseconds;
        out["peakMemoryBytes"] = memoryJson(m.peakBytes);
        record("analysis", name, nullptr, m.milliseconds, m.peakBytes);
        return out;
    }

    // Everything else in the catalog is a recovery strategy.
    const BottleneckConfig config = configFrom(body);
    if (name == "exact" && enumerateCandidateActions(g).size() > static_cast<size_t>(kMaxExactCandidates)) {
        throw ApiError(422, "too_many_actions", "exhaustive search needs 2^k solves; this network has more than " +
                                                    std::to_string(kMaxExactCandidates) + " candidate actions");
    }
    RecoveryPlan plan;
    const Measurement m = measure([&] { plan = runStrategy(name, g, config); });
    const ScenarioMetrics metrics = sim.metricsFor(plan);
    out["runtimeMs"] = m.milliseconds;
    out["peakMemoryBytes"] = memoryJson(m.peakBytes);
    out["plan"] = recoveryPlanToJson(g, plan);
    out["metrics"] = metricsToJson(metrics);
    out["executed"] = false;
    record("recovery", name, &metrics, m.milliseconds, m.peakBytes);
    if (execute) {
        sim.executeRecovery(plan);
        executedPlan_ = plan;
        out["executed"] = true;
        out["snapshot"] = networkSnapshot();
    }
    return out;
}

Json ApiService::handleCompare(const Json& body) {
    Simulation& sim = requireSimulation();
    std::vector<std::string> names;
    if (body.contains("strategies") && !body.at("strategies").is_null()) {
        if (!body.at("strategies").is_array() || body.at("strategies").empty()) throw ApiError(400, "invalid_field", "'strategies' must be a non-empty list");
        for (const Json& item : body.at("strategies")) {
            if (!item.is_string()) throw ApiError(400, "invalid_field", "'strategies' must contain text names");
            const std::string name = item.get<std::string>();
            const AlgorithmInfo* info = findAlgorithm(name);
            if (!info || std::string(info->category) != "recovery") {
                throw ApiError(400, "unknown_strategy", "'" + name + "' is not a recovery strategy; only strategies that solve the same problem can be compared");
            }
            if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        }
    } else {
        names = strategyNames();
    }
    const BottleneckConfig config = configFrom(body);
    const SupplyChainGraph& g = sim.current();
    const bool exactTooLarge = enumerateCandidateActions(g).size() > static_cast<size_t>(kMaxExactCandidates);

    Json rows = Json::array();
    std::string cheapest;
    long long cheapestCost = 0;
    for (const std::string& name : names) {
        if (name == "exact" && exactTooLarge) {
            rows.push_back({{"strategy", name}, {"skipped", "more than 16 candidate actions: 2^k exhaustive search is too large"}});
            continue;
        }
        RecoveryPlan plan;
        const Measurement m = measure([&] { plan = runStrategy(name, g, config); });
        const ScenarioMetrics metrics = sim.metricsFor(plan);
        record("compare", name, &metrics, m.milliseconds, m.peakBytes);
        rows.push_back({{"strategy", name}, {"feasible", metrics.feasible}, {"metrics", metricsToJson(metrics)},
                        {"runtimeMs", m.milliseconds}, {"peakMemoryBytes", memoryJson(m.peakBytes)},
                        {"plan", recoveryPlanToJson(g, plan)}});
        if (metrics.feasible && (cheapest.empty() || metrics.totalCost < cheapestCost)) { cheapest = name; cheapestCost = metrics.totalCost; }
    }
    return {{"scenario", {{"name", scenarioName_}, {"seed", scenarioSeed_}}}, {"eventsApplied", sim.appliedEvents().size()},
            {"before", networkToJson(g)}, {"results", rows},
            {"lowestTotalCost", cheapest.empty() ? Json(nullptr) : Json(cheapest)},
            {"note", "Every strategy solves the same problem on the same disrupted network and is scored by the same objective, so total cost is directly comparable."}};
}

Json ApiService::handleExperiment(const Json& body) {
    ExperimentConfig config;
    try { config = experimentConfigFromJson(body); }
    catch (const std::invalid_argument& error) { throw ApiError(400, "invalid_field", error.what()); }
    if (config.runs > kMaxExperimentRuns) throw ApiError(422, "too_much_work", "at most " + std::to_string(kMaxExperimentRuns) + " runs per request");
    if (config.timingRepeats > kMaxExperimentRepeats) throw ApiError(422, "too_much_work", "at most " + std::to_string(kMaxExperimentRepeats) + " timing repeats per request");
    validateExperimentConfig(config);                       // std::invalid_argument -> 400 with a precise message

    const int nodes = buildExperimentScenario(config, 0).initial.nodeCount();
    if (nodes > kMaxExperimentNodes) throw ApiError(422, "too_much_work", "networks above " + std::to_string(kMaxExperimentNodes) + " nodes are not accepted here (this one has " + std::to_string(nodes) + ")");
    if (static_cast<long long>(nodes) * config.runs > kMaxExperimentWork) {
        throw ApiError(422, "too_much_work", "runs x nodes must be at most " + std::to_string(kMaxExperimentWork) + " (requested " + std::to_string(static_cast<long long>(nodes) * config.runs) + "); use fewer runs or a smaller network, or the scx_experiments tool for large batches");
    }

    std::string reference = optionalString(body, "pairedAgainst").value_or("");
    if (reference.empty()) {
        for (const StrategySpec& spec : config.strategies) if (spec.label == "greedy") reference = "greedy";
        if (reference.empty()) reference = config.strategies.front().label;
    }
    bool known = false;
    for (const StrategySpec& spec : config.strategies) known = known || spec.label == reference;
    if (!known) throw ApiError(400, "invalid_field", "'pairedAgainst' must be one of the selected strategies");

    ExperimentResult result;
    const Measurement m = measure([&] { result = runExperiment(config); });
    Json out = experimentResultToJson(result, true, reference);
    out["pairedAgainst"] = reference;
    out["totalRuntimeMs"] = m.milliseconds;
    out["limits"] = {{"maxRuns", kMaxExperimentRuns}, {"maxNodes", kMaxExperimentNodes}, {"maxRunsTimesNodes", kMaxExperimentWork}};
    return out;
}

Json ApiService::handleMultiDay(const Json& body) {
    MultiDayRequest req;
    try { req = multiDayRequestFromJson(body); }
    catch (const std::invalid_argument& error) { throw ApiError(400, "invalid_field", error.what()); }
    if (req.scenario.horizon < kMinMultiDayHorizon || req.scenario.horizon > kMaxMultiDayHorizon) {
        throw ApiError(400, "invalid_field", "'horizon' must be between " + std::to_string(kMinMultiDayHorizon) + " and " + std::to_string(kMaxMultiDayHorizon) + " days");
    }
    if (req.lookaheadDays > kMaxMultiDayLookahead) throw ApiError(422, "too_much_work", "'lookaheadDays' must be at most " + std::to_string(kMaxMultiDayLookahead));
    if (req.maxExactCandidates > kMaxMultiDayExhaustive || req.clairvoyantMaxCandidates > kMaxMultiDayExhaustive) {
        throw ApiError(422, "too_much_work", "exhaustive searches are limited to " + std::to_string(kMaxMultiDayExhaustive) + " candidate actions here");
    }
    const GeneratorParams& n = req.scenario.network;
    const int nodes = n.suppliers + n.backupSuppliers + n.warehouses + n.distributionCenters + n.customers;
    if (nodes > kMaxMultiDayNodes) throw ApiError(422, "too_much_work", "networks above " + std::to_string(kMaxMultiDayNodes) + " nodes are not accepted here (this one has " + std::to_string(nodes) + "); use scx_multiday_experiments for larger runs");

    std::optional<MultiDaySimulator> sim;
    try { sim.emplace(buildMultiDayScenario(req.scenario), req.simulation); }
    catch (const std::invalid_argument& error) { throw ApiError(400, "invalid_scenario", error.what()); }

    Json policies = Json::array();
    std::string cheapest;
    long long cheapestCost = 0;
    ClairvoyantBound bound;
    const Measurement m = measure([&] {
        for (const std::string& name : req.policies) {
            auto policy = makeMultiDayPolicy(name, req.lambda, req.lookaheadDays, req.maxExactCandidates);
            MultiDayRunResult run;
            try { run = sim->run(*policy, /*snapshots=*/true); }
            catch (const std::invalid_argument&) { throw; }
            catch (const std::logic_error& error) { throw ApiError(500, "internal_error", std::string("simulation invariant violated: ") + error.what()); }
            if (!run.skipped && (cheapest.empty() || run.totalCost < cheapestCost)) { cheapest = name; cheapestCost = run.totalCost; }
            policies.push_back(multiDayRunToJson(*sim, run));
        }
        if (req.clairvoyant) bound = computeClairvoyantBound(*sim, req.clairvoyantMaxCandidates);
    });
    Json normal = Json::array();
    if (sim->scenario().firstDisruptionDay >= 0) normal = sim->normalService();
    return {{"scenario", multiDayScenarioToJson(*sim)}, {"normalService", normal}, {"policies", policies},
            {"clairvoyant", clairvoyantToJson(*sim, bound)}, {"lowestTotalCost", cheapest.empty() ? Json(nullptr) : Json(cheapest)},
            {"totalRuntimeMs", m.milliseconds},
            {"limits", {{"minHorizon", kMinMultiDayHorizon}, {"maxHorizon", kMaxMultiDayHorizon}, {"maxNodes", kMaxMultiDayNodes},
                        {"maxLookaheadDays", kMaxMultiDayLookahead}, {"maxExhaustiveCandidates", kMaxMultiDayExhaustive}}},
            {"note", "Every policy runs on the same scenario: same initial state, disruption schedule, demand, costs and forecast. "
                     "Recovery days are reported only when the recovery definition is met inside the horizon."}};
}

ApiResponse ApiService::handleResults(const std::map<std::string, std::string>& query) {
    const auto format = query.find("format");
    if (format != query.end() && format->second == "csv") {
        std::ostringstream csv;
        csv << "id,timestamp,scenario,kind,algorithm,eventsApplied,totalCost,unmetDemand,fulfilmentRatio,networkUtilisation,feasible,runtimeMs,peakMemoryBytes\n";
        for (const Json& row : results_) {
            auto field = [&](const char* key) -> std::string {
                if (!row.contains(key) || row.at(key).is_null()) return "";
                return row.at(key).is_string() ? csvEscape(row.at(key).get<std::string>()) : row.at(key).dump();
            };
            for (const char* key : {"id", "timestamp", "scenario", "kind", "algorithm", "eventsApplied", "totalCost", "unmetDemand",
                                    "fulfilmentRatio", "networkUtilisation", "feasible", "runtimeMs"}) csv << field(key) << ',';
            csv << field("peakMemoryBytes") << '\n';
        }
        return {200, csv.str(), "text/csv"};
    }
    if (format != query.end() && format->second != "json") throw ApiError(400, "invalid_field", "'format' must be json or csv");
    return jsonResponse(200, {{"results", results_}});
}

}  // namespace scx
