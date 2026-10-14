#include <chrono>
#include <functional>
#include <sstream>
#include <thread>
#include <vector>

#include "api/ApiService.h"
#include "api/HttpServer.h"
#include "multiday/MultiDayJson.h"
#include "httplib/httplib.h"
#include "test_framework.h"
#include "util/AllocationTracker.h"

using namespace scx;
using nlohmann::json;

namespace {
struct Reply {
    int status;
    json body;
    std::string raw;
};

Reply call(ApiService& service, const std::string& method, const std::string& path, const std::string& body = "",
           std::map<std::string, std::string> query = {}) {
    ApiResponse response = service.handle({method, path, query, body});
    Reply reply{response.status, json(), response.body};
    if (response.contentType == "application/json") reply.body = json::parse(response.body);
    return reply;
}
Reply post(ApiService& s, const std::string& path, const json& body) { return call(s, "POST", path, body.dump()); }
Reply get(ApiService& s, const std::string& path) { return call(s, "GET", path); }

std::string errorCode(const Reply& r) { return r.body.contains("error") ? r.body["error"]["code"].get<std::string>() : ""; }

// 62-node network whose script fails ~15% of all routes (severity 1.0): well over 16 recovery candidates.
json severeStressNetwork() {
    return {{"generate", {{"suppliers", 12}, {"backupSuppliers", 4}, {"warehouses", 8}, {"distributionCenters", 8},
                          {"customers", 30}, {"capacityFactor", 1.1}, {"seed", 2}}},
            {"disruptionSeverity", 1.0}};
}

// small_demo with both regular suppliers failed: big shortage, one backup supplier available.
void loadSmallDemoWithSuppliersDown(ApiService& s) {
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"file", "small_demo"}}).status, 200);
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"type", "supplier_failure"}, {"node", "S1"}}).status, 200);
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"type", "supplier_failure"}, {"node", "S2"}}).status, 200);
}
}  // namespace

json smallMultiDayBody() {
    return {{"seed", 7}, {"horizon", 12}, {"network", {{"suppliers", 3}, {"backupSuppliers", 2}, {"warehouses", 2}, {"distributionCenters", 2}, {"customers", 3}}},
            {"supplierFailures", {{"count", 1}, {"day", 2}, {"duration", 4}}}, {"routeFailures", {{"count", 1}, {"day", 3}, {"duration", 3}}},
            {"lookaheadDays", 4}};
}

void testMultiDayEndpointReturnsRealTimelines() {
    ApiService s(SCX_DATA_DIR);
    const json body = smallMultiDayBody();
    Reply r = post(s, "/api/multiday/run", body);
    SCX_CHECK_EQ(r.status, 200);
    const json& scenario = r.body["scenario"];
    SCX_CHECK_EQ(scenario["horizon"].get<int>(), 12);
    SCX_CHECK_EQ(scenario["events"].size(), size_t{4});                       // two failures, two restorations
    SCX_CHECK(scenario["planningWindowDays"].get<int>() >= scenario["supplyLeadDays"].get<int>() + 1);
    SCX_CHECK_EQ(r.body["normalService"].size(), size_t{12});
    SCX_CHECK_EQ(r.body["policies"].size(), multiDayPolicyNames().size());

    // The numbers are the C++ simulator's own: an independent run of the same request gives the same totals.
    const MultiDayRequest req = multiDayRequestFromJson(body);
    const MultiDaySimulator sim(buildMultiDayScenario(req.scenario), req.simulation);
    const size_t nodes = scenario["nodes"].size(), edges = scenario["edges"].size();
    const json& clairvoyant = r.body["clairvoyant"];
    for (const json& p : r.body["policies"]) {
        if (p["skipped"].get<bool>()) continue;
        auto policy = makeMultiDayPolicy(p["policy"].get<std::string>(), req.lambda, req.lookaheadDays, req.maxExactCandidates);
        const MultiDayRunResult direct = sim.run(*policy, false);
        SCX_CHECK_EQ(p["totals"]["totalCost"].get<long long>(), direct.totalCost);
        SCX_CHECK_EQ(p["totals"]["totalServed"].get<long long>(), direct.totalServed);
        const json& days = p["days"];
        SCX_CHECK_EQ(days.size(), size_t{12});
        long long served = 0;
        for (const json& d : days) {
            SCX_CHECK(d["served"].get<long long>() <= d["due"].get<long long>());
            SCX_CHECK_EQ(d["stockByNode"].size(), nodes);
            SCX_CHECK_EQ(d["inTransitByEdge"].size(), edges);
            for (size_t e = 0; e < edges; ++e) SCX_CHECK(d["dispatchedByEdge"][e].get<long long>() <= d["capacityByEdge"][e].get<long long>());
            served += d["served"].get<long long>();
        }
        SCX_CHECK_EQ(served, direct.totalServed);
        SCX_CHECK_EQ(days.back()["cumulativeCost"].get<long long>() + p["totals"]["settlement"].get<long long>(), p["totals"]["totalCost"].get<long long>());
        const json& rec = p["recovery"];
        SCX_CHECK(rec["applicable"].get<bool>());
        SCX_CHECK(rec["recovered"].get<bool>() == !rec["days"].is_null());   // a number only when the definition is met
        if (clairvoyant["computed"].get<bool>()) SCX_CHECK(clairvoyant["cost"].get<long long>() <= direct.totalCost);
    }
    SCX_CHECK(clairvoyant["computed"].get<bool>());
    SCX_CHECK(r.body["lowestTotalCost"].is_string());
    SCX_CHECK(get(s, "/api/health").body["scenarioLoaded"] == false);         // the live scenario is untouched

    json subset = body;
    subset["policies"] = {"none", "greedy", "greedy"};
    subset["clairvoyant"] = false;
    Reply two = post(s, "/api/multiday/run", subset);
    SCX_CHECK_EQ(two.status, 200);
    SCX_CHECK_EQ(two.body["policies"].size(), size_t{2});                      // duplicates removed
    SCX_CHECK(two.body["clairvoyant"]["computed"] == false);
    SCX_CHECK(two.body["clairvoyant"]["cost"].is_null());
}

void testMultiDayEndpointValidationAndLimits() {
    ApiService s(SCX_DATA_DIR);
    auto codeFor = [&](const std::function<void(json&)>& change) {
        json body = smallMultiDayBody();
        change(body);
        Reply r = post(s, "/api/multiday/run", body);
        return std::make_pair(r.status, errorCode(r));
    };
    SCX_CHECK((codeFor([](json& b) { b["horizon"] = 4; }) == std::make_pair(400, std::string("invalid_field"))));
    SCX_CHECK((codeFor([](json& b) { b["horizon"] = 31; }) == std::make_pair(400, std::string("invalid_field"))));
    SCX_CHECK((codeFor([](json& b) { b["horizon"] = "ten"; }) == std::make_pair(400, std::string("invalid_field"))));
    SCX_CHECK((codeFor([](json& b) { b["policies"] = {"magic"}; }) == std::make_pair(400, std::string("invalid_field"))));
    SCX_CHECK((codeFor([](json& b) { b["forecast"] = "crystal_ball"; }) == std::make_pair(400, std::string("invalid_field"))));
    SCX_CHECK((codeFor([](json& b) { b["transitMin"] = 3; b["transitMax"] = 1; }) == std::make_pair(400, std::string("invalid_scenario"))));
    SCX_CHECK((codeFor([](json& b) { b["lookaheadDays"] = 13; }) == std::make_pair(422, std::string("too_much_work"))));
    SCX_CHECK((codeFor([](json& b) { b["clairvoyantMaxCandidates"] = 11; }) == std::make_pair(422, std::string("too_much_work"))));
    SCX_CHECK((codeFor([](json& b) { b["network"]["customers"] = 60; }) == std::make_pair(422, std::string("too_much_work"))));
    SCX_CHECK_EQ(get(s, "/api/multiday/run").status, 405);
}

void testHealthCatalogAndScenarioList() {
    ApiService s(SCX_DATA_DIR);
    Reply health = get(s, "/api/health");
    SCX_CHECK_EQ(health.status, 200);
    SCX_CHECK(health.body["status"] == "ok");
    SCX_CHECK(health.body["scenarioLoaded"] == false);
    SCX_CHECK(health.body["memoryTrackingActive"] == true);     // this test binary links the allocation hooks

    Reply algorithms = get(s, "/api/algorithms");
    SCX_CHECK_EQ(algorithms.body["algorithms"].size(), size_t{10});
    SCX_CHECK(algorithms.body["algorithms"][0]["complexity"]["time"] == "O(V + E)");

    Reply scenarios = get(s, "/api/scenarios");
    SCX_CHECK_EQ(scenarios.body["standard"].size(), size_t{8});
    SCX_CHECK(scenarios.body["files"][0] == "small_demo");
    SCX_CHECK_EQ(scenarios.body["strategies"].size(), size_t{6});
}

void testRoutingErrors() {
    ApiService s(SCX_DATA_DIR);
    SCX_CHECK_EQ(call(s, "POST", "/api/health").status, 405);
    SCX_CHECK_EQ(call(s, "GET", "/api/nothing").status, 404);
    SCX_CHECK(errorCode(call(s, "GET", "/api/network")) == "no_scenario");
    SCX_CHECK_EQ(call(s, "GET", "/api/network").status, 409);                         // nothing loaded yet
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bfs"}}).status, 409);
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"step", true}}).status, 409);
    SCX_CHECK_EQ(post(s, "/api/scenario/reset", json::object()).status, 409);
}

void testLoadingAndRejectingBadScenarios() {
    ApiService s(SCX_DATA_DIR);
    Reply loaded = post(s, "/api/scenario/load", {{"standard", "supplier_failure"}, {"seed", 3}});
    SCX_CHECK_EQ(loaded.status, 200);
    SCX_CHECK_EQ(loaded.body["network"]["nodes"].size(), size_t{18});
    SCX_CHECK_EQ(loaded.body["pendingEvents"].size(), size_t{1});
    SCX_CHECK_EQ(loaded.body["appliedEvents"].size(), size_t{0});
    SCX_CHECK_EQ(loaded.body["metrics"]["unmetDemand"].get<int>(), 0);                // healthy network serves everything
    const std::string before = loaded.body["network"].dump();

    SCX_CHECK_EQ(call(s, "POST", "/api/scenario/load", "{ not json").status, 400);
    SCX_CHECK(errorCode(call(s, "POST", "/api/scenario/load", "{ not json")) == "invalid_json");
    SCX_CHECK_EQ(call(s, "POST", "/api/scenario/load", "[1,2]").status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", json::object()).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"standard", "asteroid"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"standard", 7}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"file", "../secret"}}).status, 400);   // no path traversal
    // These resolve to the REAL small_demo.json if the name were used as a path, so only validation stops them.
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"file", "../scenarios/small_demo"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"file", "..\\scenarios\\small_demo"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"file", "small_demo.json"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"file", "does_not_exist"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"scenario", {{"nodes", json::array()}, {"edges", json::array()}}}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"generate", {{"customers", 0}}}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"generate", {{"customers", "many"}}}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"generate", {{"edges", 3}}}}).status, 400);        // too few to connect
    SCX_CHECK(post(s, "/api/scenario/load", {{"file", "does_not_exist"}}).body["error"]["message"].get<std::string>().size() > 5);

    // Failed loads must not disturb the scenario that was already loaded.
    Reply still = get(s, "/api/network");
    SCX_CHECK_EQ(still.body["network"].dump(), before);

    Reply generated = post(s, "/api/scenario/load", {{"generate", {{"customers", 10}, {"seed", 4}}}, {"disruptionSeverity", 0.5}});
    SCX_CHECK_EQ(generated.status, 200);
    SCX_CHECK(generated.body["pendingEvents"].size() > 0);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", {{"scenario", json::parse(R"({"nodes":[{"name":"S","type":"supplier","supply":5},{"name":"C","type":"customer","demand":3,"shortagePenaltyPerUnit":9}],"edges":[{"from":"S","to":"C","capacity":5,"costPerUnit":1}]})")}}).status, 200);
}

void testDisruptionEndpoint() {
    ApiService s(SCX_DATA_DIR);
    post(s, "/api/scenario/load", {{"standard", "multiple_disruptions"}});
    Reply stepped = post(s, "/api/disruption/apply", {{"step", true}});
    SCX_CHECK_EQ(stepped.status, 200);
    SCX_CHECK_EQ(stepped.body["appliedEvents"].size(), size_t{1});
    SCX_CHECK_EQ(stepped.body["pendingEvents"].size(), size_t{2});
    SCX_CHECK(stepped.body["applied"].get<std::string>().find("fails") != std::string::npos);
    post(s, "/api/disruption/apply", {{"step", true}});
    post(s, "/api/disruption/apply", {{"step", true}});
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"step", true}}).status, 409);          // script exhausted
    SCX_CHECK(errorCode(post(s, "/api/disruption/apply", {{"step", true}})) == "script_finished");

    const std::string before = get(s, "/api/network").body.dump();
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"type", "route_failure"}, {"edge", 9999}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"type", "capacity_reduction"}, {"edge", 0}, {"percentRemaining", 500}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"type", "restore_node"}, {"node", "S1"}}).status, 400);  // S1 is healthy
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"type", "demand_spike"}, {"node", "S1"}, {"percentIncrease", 10}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"type", "meteor"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/disruption/apply", {{"step", "yes"}}).status, 400);
    SCX_CHECK_EQ(get(s, "/api/network").body.dump(), before);                              // rejected events change nothing
}

void testAnalysisAlgorithms() {
    ApiService s(SCX_DATA_DIR);
    post(s, "/api/scenario/load", {{"file", "small_demo"}});
    Reply bfs = post(s, "/api/algorithm/run", {{"algorithm", "bfs"}, {"source", "S1"}});
    SCX_CHECK_EQ(bfs.status, 200);
    SCX_CHECK(bfs.body["result"]["visitOrder"][0]["name"] == "S1");
    SCX_CHECK(bfs.body["result"]["lostSinceInitial"].empty());
    SCX_CHECK(bfs.body["runtimeMs"].get<double>() >= 0.0);
    SCX_CHECK(bfs.body["peakMemoryBytes"].is_number());
    SCX_CHECK(bfs.body["complexity"]["time"] == "O(V + E)");

    Reply dfs = post(s, "/api/algorithm/run", {{"algorithm", "dfs"}, {"source", 0}});          // by id
    SCX_CHECK_EQ(dfs.body["result"]["finishOrder"].size(), dfs.body["result"]["visitOrder"].size());

    Reply path = post(s, "/api/algorithm/run", {{"algorithm", "dijkstra"}, {"source", "S1"}, {"target", "C1"}});
    SCX_CHECK_EQ(path.body["result"]["cost"].get<int>(), 3);                                 // S1 -2-> W1 -1-> C1
    SCX_CHECK_EQ(path.body["result"]["pathNodes"].size(), size_t{3});

    Reply flow = post(s, "/api/algorithm/run", {{"algorithm", "max_flow"}});
    SCX_CHECK_EQ(flow.body["result"]["maxDeliverable"], flow.body["result"]["minCutCapacity"]);   // max-flow min-cut
    SCX_CHECK(flow.body["result"]["network"]["edges"][0].contains("flow"));

    // Failure impact: cutting both routes out of S1 strands everything downstream of it.
    post(s, "/api/disruption/apply", {{"type", "route_failure"}, {"edge", 0}});
    post(s, "/api/disruption/apply", {{"type", "route_failure"}, {"edge", 1}});
    Reply impact = post(s, "/api/algorithm/run", {{"algorithm", "bfs"}, {"source", "S1"}});
    SCX_CHECK_EQ(impact.body["result"]["reachedCount"].get<int>(), 1);
    SCX_CHECK(impact.body["result"]["lostSinceInitial"].size() >= 5);
    Reply unreachable = post(s, "/api/algorithm/run", {{"algorithm", "dijkstra"}, {"source", "S1"}, {"target", "C1"}});
    SCX_CHECK(unreachable.body["result"]["reachable"] == false);
    SCX_CHECK(unreachable.body["result"]["cost"].is_null());                                 // unreachable handled, not a crash
}

void testAlgorithmRunValidation() {
    ApiService s(SCX_DATA_DIR);
    post(s, "/api/scenario/load", {{"file", "small_demo"}});
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", json::object()).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bogosort"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bfs"}}).status, 400);                         // no source
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bfs"}, {"source", "Nowhere"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bfs"}, {"source", 99}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bfs"}, {"source", 1.5}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bfs"}, {"source", "S1"}, {"execute", true}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bottleneck_aware"}, {"lambda", -1}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "bottleneck_aware"}, {"lambda", "high"}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", 5}}).status, 400);

    // The stress scenario loads with its events still pending, so only the 4 backups are candidates and
    // exhaustive search is allowed. Apply a severe disruption script to push candidates past the limit.
    post(s, "/api/scenario/load", {{"standard", "stress_test"}});
    SCX_CHECK_EQ(post(s, "/api/algorithm/run", {{"algorithm", "exact"}}).status, 200);
    SCX_CHECK_EQ(post(s, "/api/scenario/load", severeStressNetwork()).status, 200);
    while (post(s, "/api/disruption/apply", {{"step", true}}).status == 200) {}
    Reply tooBig = post(s, "/api/algorithm/run", {{"algorithm", "exact"}});
    SCX_CHECK_EQ(tooBig.status, 422);                                                                         // too big to enumerate is reported clearly
    SCX_CHECK(errorCode(tooBig) == "too_many_actions");
}

void testRecoveryRunExecuteAndTwoKindsOfReset() {
    ApiService s(SCX_DATA_DIR);
    loadSmallDemoWithSuppliersDown(s);
    const Reply disrupted = get(s, "/api/network");
    const std::string disruptedNetwork = disrupted.body["network"].dump();
    const std::string disruptedMetrics = disrupted.body["metrics"].dump();
    const long long unmetBefore = disrupted.body["metrics"]["unmetDemand"].get<long long>();
    SCX_CHECK(unmetBefore > 0);
    SCX_CHECK_EQ(disrupted.body["metrics"]["activeDisruptions"].get<int>(), 2);

    Reply preview = post(s, "/api/algorithm/run", {{"algorithm", "greedy"}});             // preview only
    SCX_CHECK_EQ(preview.status, 200);
    SCX_CHECK(preview.body["executed"] == false);
    SCX_CHECK_EQ(preview.body["plan"]["actions"].size(), size_t{1});
    SCX_CHECK(preview.body["plan"]["actions"][0]["label"] == "S3-backup");
    SCX_CHECK(preview.body["metrics"]["feasible"] == true);
    SCX_CHECK(preview.body["metrics"]["unmetDemand"].get<long long>() < unmetBefore);
    SCX_CHECK(preview.body["peakMemoryBytes"].get<long long>() > 0);                      // real heap measurement
    SCX_CHECK_EQ(get(s, "/api/network").body["network"].dump(), disruptedNetwork);        // preview changed nothing

    Reply executed = post(s, "/api/algorithm/run", {{"algorithm", "greedy"}, {"execute", true}});
    SCX_CHECK(executed.body["executed"] == true);
    SCX_CHECK(executed.body["snapshot"]["recoveryExecuted"] == true);
    SCX_CHECK_EQ(executed.body["snapshot"]["metrics"]["totalCost"], executed.body["metrics"]["totalCost"]);
    SCX_CHECK(get(s, "/api/network").body["network"].dump() != disruptedNetwork);

    Reply back = post(s, "/api/scenario/reset", {{"to", "disrupted"}});                   // the SAME disrupted state again
    SCX_CHECK_EQ(back.body["network"].dump(), disruptedNetwork);
    SCX_CHECK_EQ(back.body["metrics"].dump(), disruptedMetrics);
    SCX_CHECK_EQ(post(s, "/api/scenario/reset", {{"to", "disrupted"}}).status, 409);      // nothing left to undo

    Reply flowOnly = post(s, "/api/algorithm/run", {{"algorithm", "min_cost_flow"}});
    SCX_CHECK(flowOnly.body["metrics"]["totalCost"].get<long long>() > executed.body["metrics"]["totalCost"].get<long long>());

    Reply initial = post(s, "/api/scenario/reset", {{"to", "initial"}});
    SCX_CHECK_EQ(initial.body["appliedEvents"].size(), size_t{0});
    SCX_CHECK_EQ(initial.body["metrics"]["activeDisruptions"].get<int>(), 0);
    SCX_CHECK_EQ(post(s, "/api/scenario/reset", {{"to", "sideways"}}).status, 400);
}

void testCompareEndpoint() {
    ApiService s(SCX_DATA_DIR);
    loadSmallDemoWithSuppliersDown(s);
    const std::string before = get(s, "/api/network").body.dump();
    Reply compare = post(s, "/api/algorithms/compare", json::object());
    SCX_CHECK_EQ(compare.status, 200);
    SCX_CHECK_EQ(compare.body["results"].size(), size_t{6});
    long long exactCost = -1;
    for (const json& row : compare.body["results"]) {
        SCX_CHECK(row["feasible"] == true);
        SCX_CHECK(row["runtimeMs"].get<double>() >= 0);
        if (row["strategy"] == "exact") exactCost = row["metrics"]["totalCost"].get<long long>();
    }
    SCX_CHECK(exactCost > 0);
    for (const json& row : compare.body["results"]) SCX_CHECK(row["metrics"]["totalCost"].get<long long>() >= exactCost);
    SCX_CHECK(compare.body["lowestTotalCost"].is_string());
    SCX_CHECK_EQ(get(s, "/api/network").body.dump(), before);                              // comparing never mutates the scenario

    Reply subset = post(s, "/api/algorithms/compare", {{"strategies", {"greedy", "min_cost_flow", "greedy"}}});
    SCX_CHECK_EQ(subset.body["results"].size(), size_t{2});                                 // duplicates removed
    SCX_CHECK_EQ(post(s, "/api/algorithms/compare", {{"strategies", {"bfs"}}}).status, 400); // BFS is not a recovery optimiser
    SCX_CHECK_EQ(post(s, "/api/algorithms/compare", {{"strategies", {"nope"}}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithms/compare", {{"strategies", json::array()}}).status, 400);
    SCX_CHECK_EQ(post(s, "/api/algorithms/compare", {{"strategies", "greedy"}}).status, 400);

    post(s, "/api/scenario/load", severeStressNetwork());
    while (post(s, "/api/disruption/apply", {{"step", true}}).status == 200) {}
    Reply stress = post(s, "/api/algorithms/compare", json::object());
    bool exactSkipped = false;
    for (const json& row : stress.body["results"]) if (row["strategy"] == "exact") exactSkipped = row.contains("skipped");
    SCX_CHECK(exactSkipped);                                                                // too large: skipped with a reason, not crashed
}

void testResultsAndExport() {
    ApiService s(SCX_DATA_DIR);
    SCX_CHECK_EQ(get(s, "/api/results").body["results"].size(), size_t{0});
    loadSmallDemoWithSuppliersDown(s);
    post(s, "/api/algorithm/run", {{"algorithm", "bfs"}, {"source", "S3-backup"}});
    post(s, "/api/algorithm/run", {{"algorithm", "greedy"}});
    post(s, "/api/algorithms/compare", {{"strategies", {"greedy", "min_cost_flow"}}});
    Reply results = get(s, "/api/results");
    SCX_CHECK_EQ(results.body["results"].size(), size_t{4});                                // 1 analysis + 1 run + 2 compared
    SCX_CHECK(results.body["results"][0]["totalCost"].is_null() || !results.body["results"][0].contains("totalCost"));
    SCX_CHECK(results.body["results"][1]["totalCost"].is_number());

    Reply csv = call(s, "GET", "/api/results", "", {{"format", "csv"}});
    SCX_CHECK_EQ(csv.status, 200);
    std::istringstream lines(csv.raw);
    std::string line;
    int count = 0;
    while (std::getline(lines, line)) ++count;
    SCX_CHECK_EQ(count, 5);                                                                 // header + 4 rows
    SCX_CHECK(csv.raw.rfind("id,timestamp,scenario", 0) == 0);
    SCX_CHECK_EQ(call(s, "GET", "/api/results", "", {{"format", "xml"}}).status, 400);
}

void testAllocationScopeMeasuresRealAllocations() {
    SCX_CHECK(AllocationTracker::isActive());
    AllocationScope scope;
    {
        std::vector<char> big(2'000'000);
        volatile char* touched = big.data();   // volatile: the compiler may not elide an allocation that is really used
        touched[0] = 1;
    }
    SCX_CHECK(scope.peakBytesSinceStart() >= 2'000'000);
    SCX_CHECK(scope.peakBytesSinceStart() < 4'000'000);
    AllocationScope quiet;
    SCX_CHECK(quiet.peakBytesSinceStart() < 100'000);                                       // nothing allocated => tiny
}

json smallExperiment() {
    return {{"kind", "random_disruption"}, {"x", 0.5}, {"runs", 6}, {"seed", 3},
            {"network", {{"suppliers", 4}, {"backupSuppliers", 3}, {"warehouses", 3}, {"distributionCenters", 3}, {"customers", 6}}},
            {"strategies", {"min_cost_flow", "greedy", "bottleneck_aware"}}};
}

void testExperimentEndpointReturnsMeasuredAggregates() {
    ApiService s(SCX_DATA_DIR);
    Reply r = post(s, "/api/experiments/run", smallExperiment());
    SCX_CHECK_EQ(r.status, 200);
    SCX_CHECK_EQ(r.body["strategies"].size(), size_t{3});
    SCX_CHECK_EQ(r.body["records"].size(), size_t{18});                       // 6 runs x 3 strategies, every one reported
    SCX_CHECK(r.body["pairedAgainst"] == "greedy");                           // default reference when greedy is selected
    for (const json& row : r.body["strategies"]) {
        SCX_CHECK_EQ(row["completed"].get<int>(), 6);
        SCX_CHECK(row["cost"]["n"] == 6 && row["cost"]["stddev"].get<double>() >= 0.0);
        SCX_CHECK(row["cost"]["min"].get<double>() <= row["cost"]["mean"].get<double>() && row["cost"]["mean"].get<double>() <= row["cost"]["max"].get<double>());
        SCX_CHECK(row["feasibilityRate"].get<double>() == 1.0);
        SCX_CHECK(row["memoryMeasured"] == true && row["peakMemoryBytes"]["mean"].get<double>() > 0.0);   // real heap numbers (hooks linked here)
        if (row["label"] == "greedy") SCX_CHECK(!row.contains("pairedVs"));
        else SCX_CHECK(row["pairedVs"]["cost"]["n"] == 6);
    }
    const json again = post(s, "/api/experiments/run", smallExperiment()).body;
    for (size_t i = 0; i < r.body["strategies"].size(); ++i) SCX_CHECK(again["strategies"][i]["cost"] == r.body["strategies"][i]["cost"]);   // seeded: reproducible

    json other = smallExperiment();
    other["pairedAgainst"] = "min_cost_flow";
    SCX_CHECK(post(s, "/api/experiments/run", other).body["pairedAgainst"] == "min_cost_flow");
    other["x"] = 0.0;                                                          // nothing disrupted: recovery has nothing to do, costs equal
    const json calm = post(s, "/api/experiments/run", other).body;
    SCX_CHECK(calm["strategies"][0]["cost"]["mean"] == calm["strategies"][1]["cost"]["mean"]);
}

void testExperimentEndpointValidationAndLimits() {
    ApiService s(SCX_DATA_DIR);
    auto with = [](const char* key, json value) { json b = smallExperiment(); b[key] = std::move(value); return b; };
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("runs", 0)).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("runs", "many")).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("kind", "meteor")).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("kind", 3)).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("x", 1.5)).status, 400);                 // severity above 1
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("x", "high")).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("strategies", json::array())).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("strategies", {"warp_drive"})).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("strategies", "greedy")).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("pairedAgainst", "exact")).status, 400);  // not among the selected strategies
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("lambda", -2)).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("network", 5)).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("network", {{"customers", 0}})).status, 400);
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("network", {{"suppliers", "four"}})).status, 400);
    SCX_CHECK_EQ(call(s, "POST", "/api/experiments/run", "[1]").status, 400);
    SCX_CHECK_EQ(call(s, "POST", "/api/experiments/run", "{ nope").status, 400);
    SCX_CHECK_EQ(call(s, "GET", "/api/experiments/run").status, 405);

    // Work limits are refused with 422 and a message that names the limit.
    Reply tooManyRuns = post(s, "/api/experiments/run", with("runs", 201));
    SCX_CHECK_EQ(tooManyRuns.status, 422);
    SCX_CHECK(errorCode(tooManyRuns) == "too_much_work");
    Reply tooBig = post(s, "/api/experiments/run", with("network", {{"customers", 450}}));
    SCX_CHECK_EQ(tooBig.status, 422);
    Reply tooMuch = post(s, "/api/experiments/run", with("runs", 150));                         // ~19 nodes x 150 runs is fine; 150 x 60 nodes is not
    SCX_CHECK_EQ(tooMuch.status, 200);
    json heavy = with("runs", 150);
    heavy["network"] = {{"suppliers", 20}, {"backupSuppliers", 5}, {"warehouses", 10}, {"distributionCenters", 10}, {"customers", 15}};
    SCX_CHECK_EQ(post(s, "/api/experiments/run", heavy).status, 422);
    SCX_CHECK(heavy.contains("network"));
    SCX_CHECK_EQ(post(s, "/api/experiments/run", with("timingRepeats", 50)).status, 422);
}

void testExperimentDoesNotTouchTheLiveScenario() {
    ApiService s(SCX_DATA_DIR);
    loadSmallDemoWithSuppliersDown(s);
    const std::string before = get(s, "/api/network").body.dump();
    SCX_CHECK_EQ(post(s, "/api/experiments/run", smallExperiment()).status, 200);
    SCX_CHECK_EQ(get(s, "/api/network").body.dump(), before);
}

// ---- real sockets ----

namespace {
struct RunningServer {
    ApiService service{SCX_DATA_DIR};
    HttpServer server{service};
    std::thread worker;
    int port = -1;
    RunningServer() {
        server.allowOrigin("http://localhost:5173");
        port = server.bindToAnyPort("127.0.0.1");
        worker = std::thread([this] { server.serveUntilStopped(); });
        httplib::Client probe("127.0.0.1", port);
        for (int attempt = 0; attempt < 100; ++attempt) {          // wait until the listener is accepting
            if (auto r = probe.Get("/api/health")) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ~RunningServer() { server.stop(); if (worker.joinable()) worker.join(); }
};
json parsed(const httplib::Result& r) { return json::parse(r->body); }
}  // namespace

void testHttpTransportAndCors() {
    RunningServer rs;
    SCX_CHECK(rs.port > 0);
    httplib::Client client("127.0.0.1", rs.port);
    auto health = client.Get("/api/health");
    SCX_CHECK(static_cast<bool>(health));
    SCX_CHECK_EQ(health->status, 200);
    SCX_CHECK(parsed(health)["status"] == "ok");
    SCX_CHECK(health->get_header_value("Content-Type").find("application/json") != std::string::npos);

    httplib::Headers allowed{{"Origin", "http://localhost:5173"}};
    SCX_CHECK_EQ(client.Get("/api/health", allowed)->get_header_value("Access-Control-Allow-Origin"), std::string("http://localhost:5173"));
    httplib::Headers stranger{{"Origin", "http://evil.example"}};
    SCX_CHECK(client.Get("/api/health", stranger)->get_header_value("Access-Control-Allow-Origin").empty());
    auto preflight = client.Options("/api/scenario/load", allowed);
    SCX_CHECK_EQ(preflight->status, 204);
    SCX_CHECK(preflight->get_header_value("Access-Control-Allow-Methods").find("POST") != std::string::npos);

    auto bad = client.Post("/api/scenario/load", "{ broken", "application/json");
    SCX_CHECK_EQ(bad->status, 400);
    SCX_CHECK(parsed(bad)["error"]["code"] == "invalid_json");
    SCX_CHECK_EQ(client.Get("/api/missing")->status, 404);
}

// The workflow from the specification, over real HTTP, with every number coming from the C++ engine.
void testEndToEndWorkflowOverHttp() {
    RunningServer rs;
    httplib::Client client("127.0.0.1", rs.port);
    auto postJson = [&](const std::string& path, const json& body) { return client.Post(path, body.dump(), "application/json"); };

    // 2-3. load a scenario and view the supply network
    auto loaded = postJson("/api/scenario/load", {{"file", "small_demo"}});
    SCX_CHECK_EQ(loaded->status, 200);
    SCX_CHECK_EQ(parsed(loaded)["network"]["nodes"].size(), size_t{9});
    SCX_CHECK_EQ(parsed(client.Get("/api/network"))["metrics"]["unmetDemand"].get<int>(), 0);

    // 4. trigger a supplier failure (both regular suppliers, so recovery has something to fix)
    SCX_CHECK_EQ(postJson("/api/disruption/apply", {{"type", "supplier_failure"}, {"node", "S1"}})->status, 200);
    auto failed = postJson("/api/disruption/apply", {{"type", "supplier_failure"}, {"node", "S2"}});
    const json disrupted = parsed(failed);
    const std::string disruptedNetwork = disrupted["network"].dump();
    SCX_CHECK_EQ(disrupted["metrics"]["activeDisruptions"].get<int>(), 2);

    // 5. run greedy recovery and execute it
    auto greedy = parsed(postJson("/api/algorithm/run", {{"algorithm", "greedy"}, {"execute", true}}));
    SCX_CHECK(greedy["executed"] == true);
    SCX_CHECK(greedy["metrics"]["feasible"] == true);
    const long long greedyCost = greedy["metrics"]["totalCost"].get<long long>();

    // 6. reset to the SAME disrupted state
    auto reset = parsed(postJson("/api/scenario/reset", {{"to", "disrupted"}}));
    SCX_CHECK_EQ(reset["network"].dump(), disruptedNetwork);

    // 7. run minimum-cost flow on that identical state
    auto flow = parsed(postJson("/api/algorithm/run", {{"algorithm", "min_cost_flow"}}));
    const long long flowCost = flow["metrics"]["totalCost"].get<long long>();

    // 8. compare
    auto compare = parsed(postJson("/api/algorithms/compare", {{"strategies", {"min_cost_flow", "greedy", "dijkstra_dispatch"}}}));
    long long comparedGreedy = -1, comparedFlow = -1;
    for (const json& row : compare["results"]) {
        if (row["strategy"] == "greedy") comparedGreedy = row["metrics"]["totalCost"].get<long long>();
        if (row["strategy"] == "min_cost_flow") comparedFlow = row["metrics"]["totalCost"].get<long long>();
    }
    SCX_CHECK_EQ(comparedGreedy, greedyCost);          // identical inputs => identical results, run by run or compared
    SCX_CHECK_EQ(comparedFlow, flowCost);
    SCX_CHECK(greedyCost < flowCost);                  // the paid backup beats re-routing alone in this scenario

    // 9. view updated network flows
    auto network = parsed(client.Get("/api/network"));
    SCX_CHECK(network["network"]["edges"][0].contains("flow"));

    // 10. export the results
    auto csv = client.Get("/api/results?format=csv");
    SCX_CHECK_EQ(csv->status, 200);
    SCX_CHECK(csv->get_header_value("Content-Type").find("text/csv") != std::string::npos);
    SCX_CHECK(csv->body.find("greedy") != std::string::npos);
    auto json_export = parsed(client.Get("/api/results"));
    SCX_CHECK(json_export["results"].size() >= 5);     // greedy, min_cost_flow, 3 compared
}

int main() {
    SCX_RUN(testHealthCatalogAndScenarioList);
    SCX_RUN(testRoutingErrors);
    SCX_RUN(testLoadingAndRejectingBadScenarios);
    SCX_RUN(testDisruptionEndpoint);
    SCX_RUN(testAnalysisAlgorithms);
    SCX_RUN(testAlgorithmRunValidation);
    SCX_RUN(testRecoveryRunExecuteAndTwoKindsOfReset);
    SCX_RUN(testCompareEndpoint);
    SCX_RUN(testResultsAndExport);
    SCX_RUN(testExperimentEndpointReturnsMeasuredAggregates);
    SCX_RUN(testExperimentEndpointValidationAndLimits);
    SCX_RUN(testExperimentDoesNotTouchTheLiveScenario);
    SCX_RUN(testMultiDayEndpointReturnsRealTimelines);
    SCX_RUN(testMultiDayEndpointValidationAndLimits);
    SCX_RUN(testAllocationScopeMeasuresRealAllocations);
    SCX_RUN(testHttpTransportAndCors);
    SCX_RUN(testEndToEndWorkflowOverHttp);
    return scxTestExitCode();
}
