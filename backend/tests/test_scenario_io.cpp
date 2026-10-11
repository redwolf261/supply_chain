#include <cstdio>
#include <fstream>
#include <string>

#include "simulation/ScenarioIO.h"
#include "test_framework.h"

using namespace scx;
using nlohmann::json;

namespace {
const std::string kSmallDemo = std::string(SCX_DATA_DIR) + "/scenarios/small_demo.json";

json minimalScenario() {
    return json::parse(R"({
      "name": "tiny",
      "nodes": [ {"name": "S", "type": "supplier", "supply": 10},
                 {"name": "C", "type": "customer", "demand": 5, "shortagePenaltyPerUnit": 9} ],
      "edges": [ {"from": "S", "to": "C", "capacity": 10, "costPerUnit": 2} ] })");
}

// True if loading `document` fails with a ScenarioError whose message contains `fragment`.
bool failsWith(const json& document, const std::string& fragment) {
    try { scenarioFromJson(document); }
    catch (const ScenarioError& error) { return std::string(error.what()).find(fragment) != std::string::npos; }
    return false;
}
}  // namespace

void testLoadsBundledScenario() {
    Scenario s = loadScenarioFile(kSmallDemo);
    SCX_CHECK_EQ(s.name, std::string("small_demo"));
    SCX_CHECK_EQ(s.graph.nodeCount(), 9);
    SCX_CHECK_EQ(s.graph.edgeCount(), 10);
    SCX_CHECK(s.graph.node(2).isBackup);
    SCX_CHECK(!s.graph.node(2).active);        // backups start offline
    SCX_CHECK_EQ(s.graph.node(2).activationCost, 40);
    SCX_CHECK_EQ(s.graph.edge(0).capacity, 60);
    SCX_CHECK_EQ(s.graph.edge(0).baseCapacity, 60);
    SCX_CHECK(s.graph.validate().empty());
}

void testRoundTrip() {
    Scenario original = loadScenarioFile(kSmallDemo);
    original.events.push_back({DisruptionType::NodeFailure, 0, 0});
    original.events.push_back({DisruptionType::CapacityReduction, 3, 40});
    original.graph.setEdgeCapacity(1, 25);          // a non-default current capacity must survive
    original.graph.setEdgeActive(2, false);

    Scenario copy = scenarioFromJson(scenarioToJson(original));
    SCX_CHECK_EQ(networkToJson(copy.graph).dump(), networkToJson(original.graph).dump());
    SCX_CHECK_EQ(copy.events.size(), size_t{2});
    SCX_CHECK(copy.events[1].type == DisruptionType::CapacityReduction && copy.events[1].value == 40);
    SCX_CHECK_EQ(copy.graph.edge(1).baseCapacity, 40);
    SCX_CHECK_EQ(copy.graph.edge(1).capacity, 25);
}

void testMalformedScenariosGiveClearErrors() {
    SCX_CHECK(failsWith(json::array(), "JSON object"));
    SCX_CHECK(failsWith(json::object(), "'nodes'"));

    json duplicate = minimalScenario();
    duplicate["nodes"].push_back({{"name", "S"}, {"type", "warehouse"}});
    SCX_CHECK(failsWith(duplicate, "duplicate node name"));

    json badType = minimalScenario();
    badType["nodes"][0]["type"] = "spaceship";
    SCX_CHECK(failsWith(badType, "unknown node type"));

    json unknownEndpoint = minimalScenario();
    unknownEndpoint["edges"][0]["to"] = "Nowhere";
    SCX_CHECK(failsWith(unknownEndpoint, "unknown node 'Nowhere'"));

    json negative = minimalScenario();
    negative["nodes"][0]["supply"] = -5;
    SCX_CHECK(failsWith(negative, "negative"));

    json wrongType = minimalScenario();
    wrongType["edges"][0]["capacity"] = "lots";
    SCX_CHECK(failsWith(wrongType, "must be an integer"));

    json fractional = minimalScenario();
    fractional["edges"][0]["costPerUnit"] = 2.5;
    SCX_CHECK(failsWith(fractional, "must be an integer"));

    json customerWithSupply = minimalScenario();
    customerWithSupply["nodes"][1]["supply"] = 3;
    SCX_CHECK(failsWith(customerWithSupply, "must not have supply"));

    json selfLoop = minimalScenario();
    selfLoop["edges"][0]["to"] = "S";
    SCX_CHECK(failsWith(selfLoop, "self-loop"));

    json reducedAboveBase = minimalScenario();
    reducedAboveBase["edges"][0]["baseCapacity"] = 10;
    reducedAboveBase["edges"][0]["capacity"] = 11;
    SCX_CHECK(failsWith(reducedAboveBase, "capacity"));
}

void testFileErrors() {
    SCX_CHECK_THROWS(loadScenarioFile("no/such/file.json"), ScenarioError);
    const std::string path = "scx_test_bad.json";
    { std::ofstream out(path); out << "{ this is not json"; }
    SCX_CHECK_THROWS(loadScenarioFile(path), ScenarioError);
    std::remove(path.c_str());
}

void testEventParsing() {
    Scenario s = loadScenarioFile(kSmallDemo);   // S1=0, S2=1, S3-backup=2, ..., C1=6
    Disruption byName = disruptionFromJson({{"type", "supplier_failure"}, {"node", "S1"}}, s.graph);
    SCX_CHECK(byName.type == DisruptionType::NodeFailure && byName.targetId == 0);
    Disruption byId = disruptionFromJson({{"type", "demand_spike"}, {"node", 6}, {"percentIncrease", 50}}, s.graph);
    SCX_CHECK(byId.targetId == 6 && byId.value == 50);
    Disruption cut = disruptionFromJson({{"type", "capacity_reduction"}, {"edge", 4}, {"percentRemaining", 25}}, s.graph);
    SCX_CHECK(cut.targetId == 4 && cut.value == 25);

    SCX_CHECK_THROWS(disruptionFromJson({{"type", "supplier_failure"}, {"node", "W1"}}, s.graph), ScenarioError);   // not a supplier
    SCX_CHECK_THROWS(disruptionFromJson({{"type", "supplier_failure"}, {"node", "Nobody"}}, s.graph), ScenarioError);
    SCX_CHECK_THROWS(disruptionFromJson({{"type", "route_failure"}, {"edge", 99}}, s.graph), ScenarioError);
    SCX_CHECK_THROWS(disruptionFromJson({{"type", "capacity_reduction"}, {"edge", 1}, {"percentRemaining", 140}}, s.graph), ScenarioError);
    SCX_CHECK_THROWS(disruptionFromJson({{"type", "meteor"}}, s.graph), ScenarioError);
    SCX_CHECK_THROWS(disruptionFromJson(json::array(), s.graph), ScenarioError);

    const json written = disruptionToJson(s.graph, cut);
    Disruption again = disruptionFromJson(written, s.graph);
    SCX_CHECK(again.type == cut.type && again.targetId == cut.targetId && again.value == cut.value);
}

void testNetworkJsonCarriesFlowAndStatus() {
    Scenario s = loadScenarioFile(kSmallDemo);
    s.graph.setEdgeActive(0, false);
    json plain = networkToJson(s.graph);
    SCX_CHECK_EQ(plain["nodes"].size(), size_t{9});
    SCX_CHECK(plain["edges"][0]["active"] == false);
    SCX_CHECK(plain["edges"][0]["usable"] == false);
    SCX_CHECK(!plain["edges"][1].contains("flow"));

    FlowSolution plan = FlowSolution::empty(s.graph);
    plan.edgeFlow[1] = 20;
    json withFlow = networkToJson(s.graph, &plan);
    SCX_CHECK_EQ(withFlow["edges"][1]["flow"].get<int>(), 20);
    SCX_CHECK(withFlow["edges"][1]["utilisation"].get<double>() == 0.5);   // 20 of capacity 40
}

int main() {
    SCX_RUN(testLoadsBundledScenario);
    SCX_RUN(testRoundTrip);
    SCX_RUN(testMalformedScenariosGiveClearErrors);
    SCX_RUN(testFileErrors);
    SCX_RUN(testEventParsing);
    SCX_RUN(testNetworkJsonCarriesFlowAndStatus);
    return scxTestExitCode();
}
