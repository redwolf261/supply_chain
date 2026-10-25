#include "multiday/MultiDayJson.h"

#include <algorithm>
#include <stdexcept>

#include "simulation/Disruption.h"

namespace scx {

using nlohmann::json;

namespace {

double numberField(const json& body, const char* key, double fallback, double low, double high) {
    if (!body.contains(key) || body.at(key).is_null()) return fallback;
    if (!body.at(key).is_number()) throw std::invalid_argument(std::string("'") + key + "' must be a number");
    const double value = body.at(key).get<double>();
    if (!(value >= low && value <= high)) {
        throw std::invalid_argument(std::string("'") + key + "' must be between " + std::to_string(low) + " and " + std::to_string(high));
    }
    return value;
}

int integerField(const json& body, const char* key, int fallback, long long low, long long high) {
    if (!body.contains(key) || body.at(key).is_null()) return fallback;
    if (!body.at(key).is_number_integer()) throw std::invalid_argument(std::string("'") + key + "' must be an integer");
    const long long value = body.at(key).get<long long>();
    if (value < low || value > high) {
        throw std::invalid_argument(std::string("'") + key + "' must be between " + std::to_string(low) + " and " + std::to_string(high));
    }
    return static_cast<int>(value);
}

bool boolField(const json& body, const char* key, bool fallback) {
    if (!body.contains(key) || body.at(key).is_null()) return fallback;
    if (!body.at(key).is_boolean()) throw std::invalid_argument(std::string("'") + key + "' must be true or false");
    return body.at(key).get<bool>();
}

const json* objectField(const json& body, const char* key) {
    if (!body.contains(key) || body.at(key).is_null()) return nullptr;
    if (!body.at(key).is_object()) throw std::invalid_argument(std::string("'") + key + "' must be an object");
    return &body.at(key);
}

json actionToJson(const SupplyChainGraph& g, const RecoveryAction& a) {
    const bool supplier = a.type == RecoveryActionType::ActivateSupplier;
    std::string target = supplier ? g.node(a.targetId).name : g.node(g.edge(a.targetId).from).name + " -> " + g.node(g.edge(a.targetId).to).name;
    return {{"type", supplier ? "activate_supplier" : "reopen_route"}, {"targetId", a.targetId}, {"target", target}, {"cost", a.cost}};
}

json recoveryToJson(const RecoveryTime& r) {
    json out = {{"applicable", r.applicable}, {"recovered", r.recovered}};
    out["days"] = r.recovered ? json(r.days) : json(nullptr);              // no number unless the definition is met
    out["recoveryDay"] = r.recovered ? json(r.recoveryDay) : json(nullptr);
    return out;
}

}  // namespace

MultiDayRequest multiDayRequestFromJson(const json& body) {
    if (!body.is_object()) throw std::invalid_argument("multi-day request must be a JSON object");
    MultiDayRequest r;
    MultiDayScenarioParams& p = r.scenario;
    p.seed = static_cast<std::uint64_t>(integerField(body, "seed", 1, 0, 1000000000));
    p.horizon = integerField(body, "horizon", 20, 1, 365);
    p.transitMin = integerField(body, "transitMin", p.transitMin, 0, 30);
    p.transitMax = integerField(body, "transitMax", std::max(p.transitMax, p.transitMin), 0, 30);
    p.demandAmplitude = numberField(body, "demandAmplitude", 0.1, 0.0, 1.0);
    p.initialStockDays = numberField(body, "initialStockDays", p.initialStockDays, 0.0, 30.0);
    p.penaltyScale = numberField(body, "penaltyScale", p.penaltyScale, 0.01, 100.0);
    p.warmStart = boolField(body, "warmStart", p.warmStart);
    if (const json* n = objectField(body, "network")) {
        p.network.suppliers = integerField(*n, "suppliers", p.network.suppliers, 1, 1000);
        p.network.backupSuppliers = integerField(*n, "backupSuppliers", p.network.backupSuppliers, 0, 1000);
        p.network.warehouses = integerField(*n, "warehouses", p.network.warehouses, 1, 1000);
        p.network.distributionCenters = integerField(*n, "distributionCenters", p.network.distributionCenters, 0, 1000);
        p.network.customers = integerField(*n, "customers", p.network.customers, 1, 1000);
        p.network.capacityFactor = numberField(*n, "capacityFactor", p.network.capacityFactor, 0.1, 10.0);
    }
    p.spikePercent = 0;
    if (const json* s = objectField(body, "spike")) {
        p.spikePercent = integerField(*s, "percent", 0, 0, 1000);
        p.spikeStart = integerField(*s, "start", p.spikeStart, 0, 365);
        p.spikeDuration = integerField(*s, "duration", p.spikeDuration, 0, 365);
    }
    if (const json* s = objectField(body, "supplierFailures")) {
        p.supplierFailures = integerField(*s, "count", p.supplierFailures, 0, 1000);
        p.supplierFailureDay = integerField(*s, "day", p.supplierFailureDay, 0, 365);
        p.supplierFailureDuration = integerField(*s, "duration", p.supplierFailureDuration, 0, 365);
    }
    if (const json* s = objectField(body, "routeFailures")) {
        p.routeFailures = integerField(*s, "count", p.routeFailures, 0, 1000);
        p.routeFailureDay = integerField(*s, "day", p.routeFailureDay, 0, 365);
        p.routeFailureDuration = integerField(*s, "duration", p.routeFailureDuration, 0, 365);
        p.routeCapacityLossPercent = integerField(*s, "capacityLossPercent", p.routeCapacityLossPercent, 1, 100);
    }

    MultiDayConfig& c = r.simulation;
    if (body.contains("forecast") && !body.at("forecast").is_null()) {
        if (!body.at("forecast").is_string()) throw std::invalid_argument("'forecast' must be text");
        const std::string f = body.at("forecast").get<std::string>();
        if (f == "persistence") c.forecast = ForecastMode::Persistence;
        else if (f == "perfect") c.forecast = ForecastMode::Perfect;
        else throw std::invalid_argument("'forecast' must be persistence or perfect");
    }
    c.planningWindowDays = integerField(body, "planningWindowDays", c.planningWindowDays, 1, 60);
    c.activationLeadDays = integerField(body, "activationLeadDays", c.activationLeadDays, 0, 30);
    c.repairLeadDays = integerField(body, "repairLeadDays", c.repairLeadDays, 0, 30);
    c.backlogPenaltyFraction = numberField(body, "backlogPenaltyFraction", c.backlogPenaltyFraction, 0.0, 10.0);

    r.policies = multiDayPolicyNames();
    if (body.contains("policies") && !body.at("policies").is_null()) {
        const json& list = body.at("policies");
        if (!list.is_array() || list.empty()) throw std::invalid_argument("'policies' must be a non-empty list of names");
        r.policies.clear();
        const std::vector<std::string>& known = multiDayPolicyNames();
        for (const json& item : list) {
            if (!item.is_string()) throw std::invalid_argument("'policies' must contain text names");
            const std::string name = item.get<std::string>();
            if (std::find(known.begin(), known.end(), name) == known.end()) throw std::invalid_argument("unknown multi-day policy '" + name + "'");
            if (std::find(r.policies.begin(), r.policies.end(), name) == r.policies.end()) r.policies.push_back(name);
        }
    }
    r.lambda = numberField(body, "lambda", r.lambda, 0.0, 1e6);
    r.lookaheadDays = integerField(body, "lookaheadDays", r.lookaheadDays, 1, 60);
    r.maxExactCandidates = integerField(body, "maxExactCandidates", r.maxExactCandidates, 0, 16);
    r.clairvoyant = boolField(body, "clairvoyant", r.clairvoyant);
    r.clairvoyantMaxCandidates = integerField(body, "clairvoyantMaxCandidates", r.clairvoyantMaxCandidates, 0, 16);
    return r;
}

json multiDayScenarioToJson(const MultiDaySimulator& sim) {
    const MultiDayScenario& s = sim.scenario();
    const SupplyChainGraph& g = s.network;
    json nodes = json::array(), edges = json::array(), events = json::array();
    for (const Node& n : g.nodes()) {
        nodes.push_back({{"id", n.id}, {"name", n.name}, {"type", toString(n.type)}, {"isBackup", n.isBackup}, {"active", n.active},
                         {"supplyPerDay", n.supply}, {"openingStock", n.inventory}, {"unitPurchaseCost", n.unitPurchaseCost},
                         {"holdingCostPerUnit", n.holdingCostPerUnit}, {"shortagePenaltyPerUnit", n.shortagePenaltyPerUnit},
                         {"activationCost", n.activationCost}});
    }
    for (const Edge& e : g.edges()) {
        edges.push_back({{"id", e.id}, {"from", e.from}, {"to", e.to}, {"capacityPerDay", e.capacity}, {"costPerUnit", e.costPerUnit},
                         {"transitDays", s.transitDays[e.id]}, {"reopenCost", e.reopenCost}, {"active", e.active}});
    }
    for (const ScheduledEvent& e : s.events) {
        events.push_back({{"day", e.day}, {"type", toString(e.event.type)}, {"targetId", e.event.targetId}, {"value", e.event.value},
                          {"description", describe(g, e.event)}});
    }
    json demand = json::array();
    for (int t = 0; t < s.horizon; ++t) {
        long long total = 0;
        for (const Node& n : g.nodes()) total += s.demand[n.id][t];
        demand.push_back(total);
    }
    long long pipeline = 0;
    for (const Shipment& sh : s.initialPipeline) pipeline += sh.quantity;
    return {{"name", s.name}, {"seed", s.seed}, {"horizon", s.horizon}, {"nodes", nodes}, {"edges", edges}, {"events", events},
            {"demandPerDay", demand}, {"firstDisruptionDay", s.firstDisruptionDay < 0 ? json(nullptr) : json(s.firstDisruptionDay)},
            {"initialInTransit", pipeline}, {"supplyLeadDays", supplyLeadDays(s)},
            {"planningWindowDays", sim.config().planningWindowDays}, {"forecast", toString(sim.config().forecast)},
            {"activationLeadDays", sim.config().activationLeadDays}, {"repairLeadDays", sim.config().repairLeadDays},
            {"serviceThreshold", sim.config().serviceThreshold}, {"stabilityDays", sim.config().stabilityDays}};
}

json multiDayRunToJson(const MultiDaySimulator& sim, const MultiDayRunResult& r) {
    const SupplyChainGraph& g = sim.scenario().network;
    if (r.skipped) return {{"policy", r.policy}, {"skipped", true}, {"skipReason", r.skipReason}};
    json days = json::array();
    for (const DayRecord& d : r.days) {
        json actions = json::array();
        for (const RecoveryAction& a : d.actions) actions.push_back(actionToJson(g, a));
        json day = {{"day", d.day}, {"demand", d.demand}, {"due", d.due}, {"served", d.served}, {"newBackorders", d.newBackorders},
                    {"backlog", d.backlog}, {"stock", d.stock}, {"inTransit", d.inTransit}, {"serviceLevel", d.serviceLevel},
                    {"cost", {{"purchase", d.cost.purchase}, {"transport", d.cost.transport}, {"holding", d.cost.holding},
                              {"backlog", d.cost.backlog}, {"actions", d.cost.actions}, {"total", d.cost.total()}}},
                    {"cumulativeCost", d.cumulativeCost}, {"actions", actions}, {"events", d.events},
                    {"activeDisruptions", d.activeDisruptions}, {"decisionMs", d.decisionMs}, {"planningMs", d.planningMs},
                    {"plannedSaturatedShare", d.plannedSaturatedShare}};
        if (!r.normalService.empty()) day["normalService"] = r.normalService[d.day];
        if (!d.stockByNode.empty()) {
            day["stockByNode"] = d.stockByNode;
            day["backlogByNode"] = d.backlogByNode;
            day["dispatchedByEdge"] = d.dispatchedByEdge;
            day["inTransitByEdge"] = d.inTransitByEdge;
            day["nodeActive"] = d.nodeActive;
            day["edgeUsable"] = d.edgeUsable;
            day["capacityByEdge"] = d.capacityByEdge;
        }
        days.push_back(std::move(day));
    }
    return {{"policy", r.policy}, {"skipped", false},
            {"totals", {{"totalCost", r.totalCost}, {"settlement", r.settlement}, {"actionCost", r.actionCost}, {"actions", r.actionCount},
                        {"totalDemand", r.totalDemand}, {"totalServed", r.totalServed}, {"finalBacklog", r.finalBacklog},
                        {"finalInTransit", r.finalInTransit}, {"finalStock", r.finalStock}, {"fillRate", r.fillRate},
                        {"decisionMs", r.decisionMsTotal}, {"runtimeMs", r.runtimeMs}, {"feasible", r.feasible}}},
            {"recovery", recoveryToJson(r.recovery)}, {"days", days}};
}

json clairvoyantToJson(const MultiDaySimulator& sim, const ClairvoyantBound& b) {
    const SupplyChainGraph& g = sim.scenario().network;
    json actions = json::array();
    for (const PendingAction& a : b.actions) {
        json item = actionToJson(g, a.action);
        item["decidedDay"] = a.decidedDay;
        item["effectiveDay"] = a.effectiveDay;
        actions.push_back(item);
    }
    return {{"computed", b.computed}, {"reason", b.reason}, {"cost", b.computed ? json(b.cost) : json(nullptr)}, {"actions", actions},
            {"candidates", b.candidates}, {"runtimeMs", b.runtimeMs},
            {"note", "Full-horizon perfect information, each action available from its earliest day: no online policy can cost less."}};
}

}  // namespace scx
