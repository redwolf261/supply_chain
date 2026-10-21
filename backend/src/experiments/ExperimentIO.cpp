#include "experiments/ExperimentIO.h"

#include <iomanip>
#include <sstream>

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

std::string fixed(double value, int digits = 6) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(digits) << value;
    return out.str();
}

std::string csvText(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

}  // namespace

ExperimentConfig experimentConfigFromJson(const json& body) {
    if (!body.is_object()) throw std::invalid_argument("experiment request must be a JSON object");
    ExperimentConfig config;
    if (body.contains("kind")) {
        if (!body.at("kind").is_string()) throw std::invalid_argument("'kind' must be text");
        config.kind = scenarioKindFromString(body.at("kind").get<std::string>());
    }
    config.x = numberField(body, "x", config.x, -1e9, 1e9);
    config.severity = numberField(body, "severity", config.severity, 0.0, 1.0);
    config.runs = integerField(body, "runs", config.runs, 1, 1000000);
    config.baseSeed = static_cast<std::uint64_t>(integerField(body, "seed", 1, 0, 1000000000));
    config.timingRepeats = integerField(body, "timingRepeats", 1, 1, 100);
    config.exactMaxCandidates = integerField(body, "exactMaxCandidates", config.exactMaxCandidates, 0, 16);

    BottleneckConfig bottleneck;
    bottleneck.lambda = numberField(body, "lambda", bottleneck.lambda, 0.0, 1e6);
    bottleneck.saturationThreshold = numberField(body, "saturationThreshold", bottleneck.saturationThreshold, 0.01, 1.0);

    std::vector<std::string> names = strategyNames();
    if (body.contains("strategies") && !body.at("strategies").is_null()) {
        const json& list = body.at("strategies");
        if (!list.is_array() || list.empty()) throw std::invalid_argument("'strategies' must be a non-empty list of names");
        names.clear();
        for (const json& item : list) {
            if (!item.is_string()) throw std::invalid_argument("'strategies' must contain text names");
            names.push_back(item.get<std::string>());
        }
    }
    config.strategies = defaultStrategies(names, bottleneck.lambda);
    for (StrategySpec& spec : config.strategies) spec.config = bottleneck;

    if (body.contains("network") && !body.at("network").is_null()) {
        const json& n = body.at("network");
        if (!n.is_object()) throw std::invalid_argument("'network' must be an object");
        GeneratorParams& g = config.network;
        g.suppliers = integerField(n, "suppliers", g.suppliers, 1, 2000);
        g.backupSuppliers = integerField(n, "backupSuppliers", g.backupSuppliers, 0, 2000);
        g.warehouses = integerField(n, "warehouses", g.warehouses, 1, 2000);
        g.distributionCenters = integerField(n, "distributionCenters", g.distributionCenters, 1, 2000);
        g.customers = integerField(n, "customers", g.customers, 1, 2000);
        g.edges = integerField(n, "edges", g.edges, 0, 100000);
        g.capacityFactor = numberField(n, "capacityFactor", g.capacityFactor, 0.05, 100.0);
        g.demandMin = integerField(n, "demandMin", g.demandMin, 1, 1000000);
        g.demandMax = integerField(n, "demandMax", g.demandMax, 1, 1000000);
    }
    return config;
}

json summaryToJson(const Summary& s) {
    return {{"n", s.n}, {"mean", s.mean}, {"stddev", s.stddev}, {"min", s.min}, {"max", s.max}, {"ci95", s.ci95HalfWidth}};
}

json pairedToJson(const PairedSummary& p) {
    return {{"n", p.n}, {"meanDifference", p.meanDifference}, {"stddev", p.stddev}, {"ci95", p.ci95HalfWidth},
            {"firstBetter", p.firstBetter}, {"secondBetter", p.secondBetter}, {"ties", p.ties}};
}

json experimentResultToJson(const ExperimentResult& result, bool includeRecords, const std::string& pairedAgainst) {
    json strategies = json::array();
    for (const StrategyAggregate& a : result.aggregates) {
        json row = {{"label", a.spec.label}, {"strategy", a.spec.strategy}, {"lambda", a.spec.config.lambda},
                    {"completed", a.completed}, {"skipped", a.skipped}, {"cost", summaryToJson(a.cost)},
                    {"unmetDemand", summaryToJson(a.unmet)}, {"fulfilment", summaryToJson(a.fulfilment)},
                    {"utilisation", summaryToJson(a.utilisation)}, {"runtimeMs", summaryToJson(a.runtimeMs)},
                    {"actions", summaryToJson(a.actions)}, {"feasibilityRate", a.feasibilityRate},
                    {"meanRegretPercent", a.meanRegretPercent}, {"wins", a.wins}, {"memoryMeasured", a.memoryMeasured},
                    {"peakMemoryBytes", a.memoryMeasured ? summaryToJson(a.peakBytes) : json(nullptr)}};
        if (!pairedAgainst.empty() && a.spec.label != pairedAgainst) row["pairedVs"] = {{"against", pairedAgainst}, {"cost", pairedToJson(pairedCost(result, a.spec.label, pairedAgainst))}};
        strategies.push_back(row);
    }
    json out = {{"kind", toString(result.config.kind)}, {"x", result.config.x}, {"severity", result.config.severity},
                {"runs", result.config.runs}, {"seed", result.config.baseSeed}, {"meanNodes", result.meanNodes},
                {"meanEdges", result.meanEdges}, {"strategies", strategies},
                {"note", "Every strategy ran on the same disrupted network in each run (paired design). Costs, demand and flows are measured by the C++ engine; runtime is the median wall-clock time of the strategy call."}};
    if (includeRecords) {
        json records = json::array();
        for (const RunRecord& r : result.records) {
            json item = {{"run", r.run}, {"seed", r.seed}, {"label", r.label}, {"skipped", r.skipped}, {"nodes", r.nodes},
                         {"edges", r.edges}, {"candidateActions", r.candidateActions}, {"disruptions", r.disruptions}};
            if (r.skipped) item["skipReason"] = r.skipReason;
            else item.update({{"actions", r.actions}, {"totalCost", r.totalCost}, {"unmetDemand", r.unmetDemand}, {"totalDemand", r.totalDemand},
                              {"fulfilment", r.fulfilment}, {"utilisation", r.utilisation}, {"runtimeMs", r.runtimeMs},
                              {"peakMemoryBytes", r.peakBytes >= 0 ? json(r.peakBytes) : json(nullptr)}, {"feasible", r.feasible}});
            records.push_back(item);
        }
        out["records"] = records;
    }
    return out;
}

std::string summaryCsvHeader() {
    return "experiment,x,label,strategy,lambda,runs_completed,runs_skipped,cost_mean,cost_sd,cost_min,cost_max,cost_ci95,"
           "unmet_mean,unmet_sd,fulfilment_mean,fulfilment_sd,utilisation_mean,runtime_ms_mean,runtime_ms_sd,peak_bytes_mean,"
           "feasibility_rate,regret_percent_mean,wins,actions_mean,mean_nodes,mean_edges\n";
}

std::string summaryCsvRows(const std::string& experiment, double x, const ExperimentResult& result) {
    std::ostringstream out;
    for (const StrategyAggregate& a : result.aggregates) {
        out << csvText(experiment) << ',' << fixed(x, 4) << ',' << csvText(a.spec.label) << ',' << csvText(a.spec.strategy) << ','
            << fixed(a.spec.config.lambda, 3) << ',' << a.completed << ',' << a.skipped << ','
            << fixed(a.cost.mean) << ',' << fixed(a.cost.stddev) << ',' << fixed(a.cost.min) << ',' << fixed(a.cost.max) << ',' << fixed(a.cost.ci95HalfWidth) << ','
            << fixed(a.unmet.mean) << ',' << fixed(a.unmet.stddev) << ',' << fixed(a.fulfilment.mean) << ',' << fixed(a.fulfilment.stddev) << ','
            << fixed(a.utilisation.mean) << ',' << fixed(a.runtimeMs.mean) << ',' << fixed(a.runtimeMs.stddev) << ','
            << (a.memoryMeasured ? fixed(a.peakBytes.mean, 1) : std::string()) << ',' << fixed(a.feasibilityRate, 4) << ','
            << fixed(a.meanRegretPercent, 4) << ',' << a.wins << ',' << fixed(a.actions.mean, 3) << ','
            << fixed(result.meanNodes, 1) << ',' << fixed(result.meanEdges, 1) << '\n';
    }
    return out.str();
}

std::string runsCsvHeader() {
    return "experiment,x,run,seed,label,skipped,nodes,edges,candidate_actions,disruptions,actions,total_cost,unmet_demand,total_demand,"
           "fulfilment,utilisation,runtime_ms,peak_bytes,feasible\n";
}

std::string runsCsvRows(const std::string& experiment, double x, const ExperimentResult& result) {
    std::ostringstream out;
    for (const RunRecord& r : result.records) {
        out << csvText(experiment) << ',' << fixed(x, 4) << ',' << r.run << ',' << r.seed << ',' << csvText(r.label) << ',' << (r.skipped ? 1 : 0) << ','
            << r.nodes << ',' << r.edges << ',' << r.candidateActions << ',' << r.disruptions << ',';
        if (r.skipped) out << ",,,,,,,,\n";
        else out << r.actions << ',' << r.totalCost << ',' << r.unmetDemand << ',' << r.totalDemand << ',' << fixed(r.fulfilment) << ',' << fixed(r.utilisation) << ','
                 << fixed(r.runtimeMs) << ',' << (r.peakBytes >= 0 ? std::to_string(r.peakBytes) : std::string()) << ',' << (r.feasible ? 1 : 0) << '\n';
    }
    return out.str();
}

}  // namespace scx
