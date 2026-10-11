#include "simulation/ScenarioIO.h"

#include <fstream>
#include <unordered_map>

namespace scx {

using nlohmann::json;

namespace {

long long integerField(const json& object, const char* key, long long fallback, const std::string& where) {
    if (!object.contains(key)) return fallback;
    const json& value = object.at(key);
    if (!value.is_number_integer()) throw ScenarioError(where + ": field '" + key + "' must be an integer");
    return value.get<long long>();
}

bool boolField(const json& object, const char* key, bool fallback, const std::string& where) {
    if (!object.contains(key)) return fallback;
    if (!object.at(key).is_boolean()) throw ScenarioError(where + ": field '" + key + "' must be true or false");
    return object.at(key).get<bool>();
}

std::string stringField(const json& object, const char* key, const std::string& where) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        throw ScenarioError(where + ": missing text field '" + key + "'");
    }
    return object.at(key).get<std::string>();
}

int intField(const json& object, const char* key, int fallback, const std::string& where) {
    const long long value = integerField(object, key, fallback, where);
    if (value < -1000000000LL || value > 1000000000LL) throw ScenarioError(where + ": field '" + key + "' is out of range");
    return static_cast<int>(value);
}

int nodeReference(const json& object, const SupplyChainGraph& graph, const std::string& where) {
    if (!object.contains("node")) throw ScenarioError(where + ": missing 'node'");
    const json& ref = object.at("node");
    if (ref.is_number_integer()) {
        const long long id = ref.get<long long>();
        if (id < 0 || id >= graph.nodeCount()) throw ScenarioError(where + ": node id " + std::to_string(id) + " does not exist");
        return static_cast<int>(id);
    }
    if (ref.is_string()) {
        for (const Node& node : graph.nodes()) if (node.name == ref.get<std::string>()) return node.id;
        throw ScenarioError(where + ": unknown node '" + ref.get<std::string>() + "'");
    }
    throw ScenarioError(where + ": 'node' must be a name or an id");
}

int edgeReference(const json& object, const SupplyChainGraph& graph, const std::string& where) {
    if (!object.contains("edge") || !object.at("edge").is_number_integer()) throw ScenarioError(where + ": missing integer 'edge'");
    const long long id = object.at("edge").get<long long>();
    if (id < 0 || id >= graph.edgeCount()) throw ScenarioError(where + ": edge id " + std::to_string(id) + " does not exist");
    return static_cast<int>(id);
}

}  // namespace

Scenario scenarioFromJson(const json& document) {
    if (!document.is_object()) throw ScenarioError("scenario must be a JSON object");
    Scenario scenario;
    scenario.name = document.contains("name") && document.at("name").is_string() ? document.at("name").get<std::string>() : "unnamed";
    scenario.seed = integerField(document, "seed", 0, "scenario");
    if (!document.contains("nodes") || !document.at("nodes").is_array() || document.at("nodes").empty()) {
        throw ScenarioError("scenario needs a non-empty 'nodes' array");
    }
    if (!document.contains("edges") || !document.at("edges").is_array()) throw ScenarioError("scenario needs an 'edges' array");

    std::unordered_map<std::string, int> idByName;
    int index = 0;
    for (const json& item : document.at("nodes")) {
        const std::string where = "node #" + std::to_string(index++);
        if (!item.is_object()) throw ScenarioError(where + " must be an object");
        Node node;
        node.name = stringField(item, "name", where);
        const std::string label = where + " ('" + node.name + "')";
        try { node.type = nodeTypeFromString(stringField(item, "type", label)); }
        catch (const std::invalid_argument& error) { throw ScenarioError(label + ": " + error.what()); }
        node.supply = intField(item, "supply", 0, label);
        node.demand = intField(item, "demand", 0, label);
        node.inventory = intField(item, "inventory", 0, label);
        node.processingCapacity = intField(item, "processingCapacity", 0, label);
        node.unitPurchaseCost = intField(item, "unitPurchaseCost", 0, label);
        node.holdingCostPerUnit = intField(item, "holdingCostPerUnit", 0, label);
        node.shortagePenaltyPerUnit = intField(item, "shortagePenaltyPerUnit", 0, label);
        node.activationCost = intField(item, "activationCost", 0, label);
        node.isBackup = boolField(item, "isBackup", false, label);
        node.active = boolField(item, "active", !node.isBackup, label);   // backups start offline unless stated
        if (idByName.count(node.name)) throw ScenarioError(label + ": duplicate node name");
        try { idByName[node.name] = scenario.graph.addNode(node); }
        catch (const std::invalid_argument& error) { throw ScenarioError(label + ": " + error.what()); }
    }

    index = 0;
    for (const json& item : document.at("edges")) {
        const std::string where = "edge #" + std::to_string(index++);
        if (!item.is_object()) throw ScenarioError(where + " must be an object");
        const std::string from = stringField(item, "from", where), to = stringField(item, "to", where);
        if (!idByName.count(from)) throw ScenarioError(where + ": unknown node '" + from + "'");
        if (!idByName.count(to)) throw ScenarioError(where + ": unknown node '" + to + "'");
        Edge edge;
        edge.from = idByName[from];
        edge.to = idByName[to];
        edge.baseCapacity = intField(item, "baseCapacity", intField(item, "capacity", 0, where), where);
        edge.costPerUnit = intField(item, "costPerUnit", 0, where);
        edge.transitTime = intField(item, "transitTime", 1, where);
        edge.reopenCost = intField(item, "reopenCost", 0, where);
        edge.active = boolField(item, "active", true, where);
        try {
            const int id = scenario.graph.addEdge(edge);
            // "capacity" is the base capacity unless "baseCapacity" is also given (then it is the current one).
            if (item.contains("baseCapacity") && item.contains("capacity")) {
                scenario.graph.setEdgeCapacity(id, intField(item, "capacity", edge.baseCapacity, where));
            }
        } catch (const std::invalid_argument& error) { throw ScenarioError(where + ": " + error.what()); }
    }

    const std::vector<std::string> problems = scenario.graph.validate();
    if (!problems.empty()) throw ScenarioError("invalid network: " + problems.front());

    if (document.contains("events")) {
        if (!document.at("events").is_array()) throw ScenarioError("'events' must be an array");
        for (const json& event : document.at("events")) scenario.events.push_back(disruptionFromJson(event, scenario.graph));
    }
    return scenario;
}

Scenario loadScenarioFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw ScenarioError("cannot open scenario file '" + path + "'");
    json document;
    try { document = json::parse(file); }
    catch (const json::parse_error& error) { throw ScenarioError("scenario file is not valid JSON: " + std::string(error.what())); }
    return scenarioFromJson(document);
}

json scenarioToJson(const Scenario& scenario) {
    json nodes = json::array(), edges = json::array();
    for (const Node& n : scenario.graph.nodes()) {
        nodes.push_back({{"name", n.name}, {"type", toString(n.type)}, {"supply", n.supply}, {"demand", n.demand},
                         {"inventory", n.inventory}, {"processingCapacity", n.processingCapacity},
                         {"unitPurchaseCost", n.unitPurchaseCost}, {"holdingCostPerUnit", n.holdingCostPerUnit},
                         {"shortagePenaltyPerUnit", n.shortagePenaltyPerUnit}, {"activationCost", n.activationCost},
                         {"isBackup", n.isBackup}, {"active", n.active}});
    }
    for (const Edge& e : scenario.graph.edges()) {
        edges.push_back({{"from", scenario.graph.node(e.from).name}, {"to", scenario.graph.node(e.to).name},
                         {"baseCapacity", e.baseCapacity}, {"capacity", e.capacity}, {"costPerUnit", e.costPerUnit},
                         {"transitTime", e.transitTime}, {"reopenCost", e.reopenCost}, {"active", e.active}});
    }
    json events = json::array();
    for (const Disruption& d : scenario.events) events.push_back(disruptionToJson(scenario.graph, d));
    return {{"name", scenario.name}, {"seed", scenario.seed}, {"nodes", nodes}, {"edges", edges}, {"events", events}};
}

Disruption disruptionFromJson(const json& document, const SupplyChainGraph& graph) {
    if (!document.is_object()) throw ScenarioError("event must be a JSON object");
    const std::string type = stringField(document, "type", "event");
    const std::string where = "event '" + type + "'";
    Disruption event;
    if (type == "node_failure" || type == "supplier_failure") {
        event.type = DisruptionType::NodeFailure;
        event.targetId = nodeReference(document, graph, where);
        if (type == "supplier_failure" && graph.node(event.targetId).type != NodeType::Supplier) {
            throw ScenarioError(where + ": '" + graph.node(event.targetId).name + "' is not a supplier");
        }
    } else if (type == "restore_node") {
        event.type = DisruptionType::RestoreNode;
        event.targetId = nodeReference(document, graph, where);
    } else if (type == "route_failure") {
        event.type = DisruptionType::RouteFailure;
        event.targetId = edgeReference(document, graph, where);
    } else if (type == "restore_route") {
        event.type = DisruptionType::RestoreRoute;
        event.targetId = edgeReference(document, graph, where);
    } else if (type == "capacity_reduction") {
        event.type = DisruptionType::CapacityReduction;
        event.targetId = edgeReference(document, graph, where);
        event.value = integerField(document, "percentRemaining", -1, where);
        if (event.value < 0 || event.value > 100) throw ScenarioError(where + ": 'percentRemaining' must be an integer in [0, 100]");
    } else if (type == "demand_spike") {
        event.type = DisruptionType::DemandSpike;
        event.targetId = nodeReference(document, graph, where);
        event.value = integerField(document, "percentIncrease", -1, where);
        if (event.value < 0 || event.value > 1000) throw ScenarioError(where + ": 'percentIncrease' must be an integer in [0, 1000]");
    } else {
        throw ScenarioError("unknown event type '" + type + "'");
    }
    return event;
}

json disruptionToJson(const SupplyChainGraph& graph, const Disruption& event) {
    json out = {{"type", toString(event.type)}, {"description", describe(graph, event)}};
    switch (event.type) {
        case DisruptionType::NodeFailure: case DisruptionType::RestoreNode:
        case DisruptionType::DemandSpike: out["node"] = graph.node(event.targetId).name; break;
        case DisruptionType::RouteFailure: case DisruptionType::RestoreRoute:
        case DisruptionType::CapacityReduction: out["edge"] = event.targetId; break;
    }
    if (event.type == DisruptionType::CapacityReduction) out["percentRemaining"] = event.value;
    if (event.type == DisruptionType::DemandSpike) out["percentIncrease"] = event.value;
    return out;
}

json networkToJson(const SupplyChainGraph& graph, const FlowSolution* plan) {
    json nodes = json::array(), edges = json::array();
    for (const Node& n : graph.nodes()) {
        json item = {{"id", n.id}, {"name", n.name}, {"type", toString(n.type)}, {"supply", n.supply},
                     {"demand", n.demand}, {"inventory", n.inventory}, {"isBackup", n.isBackup},
                     {"activationCost", n.activationCost}, {"unitPurchaseCost", n.unitPurchaseCost},
                     {"shortagePenaltyPerUnit", n.shortagePenaltyPerUnit}, {"active", n.active}};
        if (plan) {
            item["supplyDraw"] = plan->supplyDraw[n.id];
            item["inventoryDraw"] = plan->inventoryDraw[n.id];
            item["unmetDemand"] = plan->unmetDemand[n.id];
        }
        nodes.push_back(item);
    }
    for (const Edge& e : graph.edges()) {
        json item = {{"id", e.id}, {"from", e.from}, {"to", e.to}, {"fromName", graph.node(e.from).name},
                     {"toName", graph.node(e.to).name}, {"baseCapacity", e.baseCapacity}, {"capacity", e.capacity},
                     {"costPerUnit", e.costPerUnit}, {"transitTime", e.transitTime}, {"reopenCost", e.reopenCost},
                     {"active", e.active}, {"usable", graph.isEdgeUsable(e.id)}};
        if (plan) {
            const long long flow = plan->edgeFlow[e.id];
            item["flow"] = flow;
            item["utilisation"] = e.capacity > 0 ? static_cast<double>(flow) / e.capacity : 0.0;
        }
        edges.push_back(item);
    }
    return {{"nodes", nodes}, {"edges", edges}};
}

json metricsToJson(const ScenarioMetrics& m) {
    return {{"purchaseCost", m.flowCost.purchase}, {"transportCost", m.flowCost.transport},
            {"holdingCost", m.flowCost.holding}, {"shortagePenalty", m.flowCost.shortagePenalty},
            {"actionCost", m.actionCost}, {"totalCost", m.totalCost}, {"totalDemand", m.totalDemand},
            {"unmetDemand", m.unmetDemand}, {"deliveredDemand", m.deliveredDemand},
            {"fulfilmentRatio", m.fulfilmentRatio}, {"networkUtilisation", m.networkUtilisation},
            {"totalSuppliers", m.totalSuppliers}, {"activeSuppliers", m.activeSuppliers},
            {"factories", m.factories}, {"warehouses", m.warehouses},
            {"distributionCenters", m.distributionCenters}, {"customers", m.customers},
            {"failedNodes", m.failedNodes}, {"failedRoutes", m.failedRoutes}, {"reducedRoutes", m.reducedRoutes},
            {"demandSpikes", m.demandSpikes}, {"activeDisruptions", m.activeDisruptions}, {"feasible", m.feasible}};
}

json recoveryPlanToJson(const SupplyChainGraph& disrupted, const RecoveryPlan& plan) {
    json actions = json::array(), steps = json::array();
    for (const RecoveryAction& a : plan.actions) {
        const bool supplier = a.type == RecoveryActionType::ActivateSupplier;
        actions.push_back({{"type", supplier ? "activate_supplier" : "reopen_route"}, {"target", a.targetId},
                           {"label", supplier ? disrupted.node(a.targetId).name
                                              : disrupted.node(disrupted.edge(a.targetId).from).name + " -> " +
                                                disrupted.node(disrupted.edge(a.targetId).to).name},
                           {"cost", a.cost}});
    }
    for (const RecoveryStep& s : plan.steps) {
        steps.push_back({{"target", s.action.targetId}, {"type", s.action.type == RecoveryActionType::ActivateSupplier ? "activate_supplier" : "reopen_route"},
                         {"score", s.score}, {"shortageReduction", s.shortageReduction}, {"costGain", s.costGain},
                         {"bottleneckRisk", s.bottleneckRisk}});
    }
    SupplyChainGraph repaired = disrupted;
    for (const RecoveryAction& a : plan.actions) applyAction(repaired, a);
    return {{"algorithm", plan.algorithm}, {"actions", actions}, {"steps", steps}, {"actionCost", plan.actionCost},
            {"flowCost", plan.flowCost.total()}, {"totalCost", plan.totalCost()}, {"unmetDemand", plan.unmetDemand},
            {"candidateEvaluations", plan.candidateEvaluations}, {"runtimeMs", plan.runtimeMilliseconds},
            {"network", networkToJson(repaired, &plan.flow)}};
}

json scenarioRunToJson(const ScenarioRunResult& run) {
    return {{"strategy", run.strategy}, {"events", run.eventDescriptions},
            {"disruptedNetwork", networkToJson(run.disruptedNetwork)},
            {"normalOperations", metricsToJson(run.normalOperations)},
            {"disruptedNoRecovery", metricsToJson(run.disruptedNoRecovery)},
            {"recovered", metricsToJson(run.recovered)},
            {"plan", recoveryPlanToJson(run.disruptedNetwork, run.plan)}};
}

}  // namespace scx
