#include "experiments/MultiDayExperiments.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace scx {

namespace {
std::string fixed(double value, int digits = 6) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(digits) << value;
    return out.str();
}
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}
}  // namespace

std::vector<MultiDayPolicySpec> multiDayPolicySpecs(const std::vector<std::string>& names, double lambda, int lookaheadDays) {
    std::vector<MultiDayPolicySpec> specs;
    for (const std::string& n : names) {
        MultiDayPolicySpec s;
        s.label = (n == "lookahead" && lookaheadDays != 5) ? "lookahead(" + std::to_string(lookaheadDays) + ")" : n;
        s.policy = n;
        s.lambda = lambda;
        s.lookaheadDays = lookaheadDays;
        specs.push_back(s);
    }
    return specs;
}

void validateMultiDayExperiment(const MultiDayExperimentConfig& c) {
    if (c.runs < 1) throw std::invalid_argument("runs must be at least 1");
    if (c.policies.empty()) throw std::invalid_argument("at least one policy is required");
    std::set<std::string> labels;
    const std::vector<std::string>& known = multiDayPolicyNames();
    for (const MultiDayPolicySpec& p : c.policies) {
        if (!labels.insert(p.label).second) throw std::invalid_argument("duplicate policy label '" + p.label + "'");
        if (std::find(known.begin(), known.end(), p.policy) == known.end()) throw std::invalid_argument("unknown policy '" + p.policy + "'");
        makeMultiDayPolicy(p.policy, p.lambda, p.lookaheadDays, p.maxExactCandidates);   // validates lambda, look-ahead
    }
    if (c.clairvoyantMaxCandidates < 0 || c.clairvoyantMaxCandidates > 16) throw std::invalid_argument("clairvoyant candidate limit must be between 0 and 16");
    MultiDayScenarioParams first = c.scenario;
    first.seed = c.baseSeed;
    MultiDaySimulator(buildMultiDayScenario(first), c.simulation);                          // validates scenario and config
}

MultiDayExperimentResult runMultiDayExperiment(const MultiDayExperimentConfig& config) {
    validateMultiDayExperiment(config);
    MultiDayExperimentResult result;
    result.config = config;
    double nodes = 0, edges = 0;
    for (int run = 0; run < config.runs; ++run) {
        MultiDayScenarioParams params = config.scenario;
        params.seed = config.baseSeed + static_cast<std::uint64_t>(run);
        const MultiDaySimulator sim(buildMultiDayScenario(params), config.simulation);
        nodes += sim.scenario().network.nodeCount();
        edges += sim.scenario().network.edgeCount();
        ClairvoyantBound bound;
        if (config.clairvoyant) bound = computeClairvoyantBound(sim, config.clairvoyantMaxCandidates);
        if (bound.computed) ++result.boundsComputed;

        for (const MultiDayPolicySpec& spec : config.policies) {
            auto policy = makeMultiDayPolicy(spec.policy, spec.lambda, spec.lookaheadDays, spec.maxExactCandidates);
            const MultiDayRunResult r = sim.run(*policy, /*snapshots=*/false);
            MultiDayRunRow row;
            row.run = run;
            row.seed = params.seed;
            row.label = spec.label;
            row.skipped = r.skipped;
            row.skipReason = r.skipReason;
            row.boundComputed = bound.computed;
            row.bound = bound.cost;
            if (!r.skipped) {
                row.totalCost = r.totalCost; row.settlement = r.settlement; row.actionCost = r.actionCost;
                row.finalBacklog = r.finalBacklog; row.totalDemand = r.totalDemand; row.totalServed = r.totalServed;
                row.actions = r.actionCount; row.fillRate = r.fillRate;
                row.recoveryApplicable = r.recovery.applicable; row.recovered = r.recovery.recovered; row.recoveryDays = r.recovery.days;
                row.decisionMs = r.decisionMsTotal; row.runtimeMs = r.runtimeMs; row.feasible = r.feasible;
                if (bound.computed && bound.cost > r.totalCost) throw std::logic_error("a policy beat the clairvoyant lower bound");
                if (config.keepDailyRecords) {
                    for (const DayRecord& d : r.days) {
                        MultiDayDailyRow dr;
                        dr.run = run; dr.label = spec.label; dr.day = d.day;
                        dr.demand = d.demand; dr.due = d.due; dr.served = d.served; dr.backlog = d.backlog; dr.newBackorders = d.newBackorders;
                        dr.inTransit = d.inTransit; dr.stock = d.stock; dr.dayCost = d.cost.total(); dr.backlogCost = d.cost.backlog;
                        dr.cumulativeCost = d.cumulativeCost; dr.actionCost = d.cost.actions; dr.serviceLevel = d.serviceLevel;
                        dr.plannedSaturatedShare = d.plannedSaturatedShare; dr.decisionMs = d.decisionMs;
                        dr.actions = static_cast<int>(d.actions.size()); dr.activeDisruptions = d.activeDisruptions;
                        if (!r.normalService.empty()) dr.normalService = r.normalService[d.day];
                        result.daily.push_back(dr);
                    }
                }
            }
            result.runs.push_back(row);
        }
    }
    result.meanNodes = nodes / config.runs;
    result.meanEdges = edges / config.runs;

    std::vector<long long> best(config.runs, -1);
    for (const MultiDayRunRow& r : result.runs) if (!r.skipped && (best[r.run] < 0 || r.totalCost < best[r.run])) best[r.run] = r.totalCost;
    for (const MultiDayPolicySpec& spec : config.policies) {
        MultiDayAggregate a;
        a.spec = spec;
        std::vector<double> cost, fill, backlog, actionCost, decision, recovery, gap;
        int feasible = 0;
        for (const MultiDayRunRow& r : result.runs) {
            if (r.label != spec.label) continue;
            if (r.skipped) { ++a.skipped; continue; }
            ++a.completed;
            cost.push_back(static_cast<double>(r.totalCost));
            fill.push_back(r.fillRate);
            backlog.push_back(static_cast<double>(r.finalBacklog));
            actionCost.push_back(static_cast<double>(r.actionCost));
            decision.push_back(r.decisionMs);
            if (r.recoveryApplicable) ++a.recoveryApplicableRuns;
            if (r.recovered) { ++a.recoveredRuns; recovery.push_back(r.recoveryDays); }
            if (r.feasible) ++feasible;
            if (r.boundComputed && r.bound > 0) gap.push_back(100.0 * static_cast<double>(r.totalCost - r.bound) / static_cast<double>(r.bound));
            if (r.totalCost == best[r.run]) ++a.wins;
        }
        a.cost = summarize(cost); a.fillRate = summarize(fill); a.finalBacklog = summarize(backlog);
        a.actionCost = summarize(actionCost); a.decisionMs = summarize(decision); a.recoveryDays = summarize(recovery);
        a.gapToBoundPercent = summarize(gap);
        a.feasibilityRate = a.completed > 0 ? static_cast<double>(feasible) / a.completed : 1.0;
        result.aggregates.push_back(a);
    }
    return result;
}

PairedSummary pairedMultiDayCost(const MultiDayExperimentResult& result, const std::string& labelA, const std::string& labelB) {
    std::vector<double> a, b;
    for (int run = 0; run < result.config.runs; ++run) {
        const MultiDayRunRow *ra = nullptr, *rb = nullptr;
        for (const MultiDayRunRow& r : result.runs) {
            if (r.run != run) continue;
            if (r.label == labelA) ra = &r;
            if (r.label == labelB) rb = &r;
        }
        if (ra && rb && !ra->skipped && !rb->skipped) { a.push_back(static_cast<double>(ra->totalCost)); b.push_back(static_cast<double>(rb->totalCost)); }
    }
    return pairedCompare(a, b);
}

// ------------------------------------------------------------------------------------------------ CSV

std::string multiDayRunsCsvHeader() {
    return "experiment,x,run,seed,label,skipped,total_cost,settlement,action_cost,actions,total_demand,total_served,final_backlog,"
           "fill_rate,recovery_applicable,recovered,recovery_days,decision_ms,runtime_ms,feasible,bound_computed,bound\n";
}

std::string multiDayRunsCsvRows(const std::string& experiment, double x, const MultiDayExperimentResult& result) {
    std::ostringstream out;
    for (const MultiDayRunRow& r : result.runs) {
        out << quoted(experiment) << ',' << fixed(x, 4) << ',' << r.run << ',' << r.seed << ',' << quoted(r.label) << ',' << (r.skipped ? 1 : 0) << ',';
        if (r.skipped) out << ",,,,,,,,,,,,,,";
        else out << r.totalCost << ',' << r.settlement << ',' << r.actionCost << ',' << r.actions << ',' << r.totalDemand << ',' << r.totalServed << ','
                 << r.finalBacklog << ',' << fixed(r.fillRate) << ',' << (r.recoveryApplicable ? 1 : 0) << ',' << (r.recovered ? 1 : 0) << ','
                 << (r.recovered ? std::to_string(r.recoveryDays) : std::string()) << ',' << fixed(r.decisionMs, 4) << ',' << fixed(r.runtimeMs, 4) << ','
                 << (r.feasible ? 1 : 0) << ',';
        out << (r.boundComputed ? 1 : 0) << ',' << (r.boundComputed ? std::to_string(r.bound) : std::string()) << '\n';
    }
    return out.str();
}

std::string multiDayDailyCsvHeader() {
    return "experiment,x,run,label,day,demand,due,served,backlog,new_backorders,in_transit,stock,day_cost,backlog_cost,cumulative_cost,"
           "action_cost,actions,service_level,normal_service,planned_saturated_share,active_disruptions,decision_ms\n";
}

std::string multiDayDailyCsvRows(const std::string& experiment, double x, const MultiDayExperimentResult& result) {
    std::ostringstream out;
    for (const MultiDayDailyRow& d : result.daily) {
        out << quoted(experiment) << ',' << fixed(x, 4) << ',' << d.run << ',' << quoted(d.label) << ',' << d.day << ',' << d.demand << ',' << d.due << ','
            << d.served << ',' << d.backlog << ',' << d.newBackorders << ',' << d.inTransit << ',' << d.stock << ',' << d.dayCost << ',' << d.backlogCost << ','
            << d.cumulativeCost << ',' << d.actionCost << ',' << d.actions << ',' << fixed(d.serviceLevel) << ','
            << (d.normalService >= 0.0 ? fixed(d.normalService) : std::string()) << ',' << fixed(d.plannedSaturatedShare) << ','
            << d.activeDisruptions << ',' << fixed(d.decisionMs, 4) << '\n';
    }
    return out.str();
}

std::string multiDaySummaryCsvHeader() {
    return "experiment,x,label,policy,lambda,lookahead_days,runs_completed,runs_skipped,cost_mean,cost_sd,cost_min,cost_max,cost_ci95,"
           "fill_rate_mean,fill_rate_sd,final_backlog_mean,action_cost_mean,decision_ms_mean,decision_ms_sd,recovered_runs,recovery_applicable_runs,"
           "recovery_days_mean,recovery_days_sd,gap_to_bound_percent_mean,gap_to_bound_percent_sd,bound_runs,feasibility_rate,wins,mean_nodes,mean_edges\n";
}

std::string multiDaySummaryCsvRows(const std::string& experiment, double x, const MultiDayExperimentResult& result) {
    std::ostringstream out;
    for (const MultiDayAggregate& a : result.aggregates) {
        out << quoted(experiment) << ',' << fixed(x, 4) << ',' << quoted(a.spec.label) << ',' << quoted(a.spec.policy) << ',' << fixed(a.spec.lambda, 3) << ','
            << a.spec.lookaheadDays << ',' << a.completed << ',' << a.skipped << ',' << fixed(a.cost.mean) << ',' << fixed(a.cost.stddev) << ','
            << fixed(a.cost.min) << ',' << fixed(a.cost.max) << ',' << fixed(a.cost.ci95HalfWidth) << ',' << fixed(a.fillRate.mean) << ',' << fixed(a.fillRate.stddev) << ','
            << fixed(a.finalBacklog.mean) << ',' << fixed(a.actionCost.mean) << ',' << fixed(a.decisionMs.mean, 4) << ',' << fixed(a.decisionMs.stddev, 4) << ','
            << a.recoveredRuns << ',' << a.recoveryApplicableRuns << ',' << (a.recoveredRuns > 0 ? fixed(a.recoveryDays.mean, 3) : std::string()) << ','
            << (a.recoveredRuns > 1 ? fixed(a.recoveryDays.stddev, 3) : std::string()) << ','
            << (a.gapToBoundPercent.n > 0 ? fixed(a.gapToBoundPercent.mean, 4) : std::string()) << ','
            << (a.gapToBoundPercent.n > 1 ? fixed(a.gapToBoundPercent.stddev, 4) : std::string()) << ',' << a.gapToBoundPercent.n << ','
            << fixed(a.feasibilityRate, 4) << ',' << a.wins << ',' << fixed(result.meanNodes, 1) << ',' << fixed(result.meanEdges, 1) << '\n';
    }
    return out.str();
}

// ------------------------------------------------------------------------------------------------ presets

namespace {
MultiDayExperimentConfig baseConfig(int runs) {
    MultiDayExperimentConfig c;
    MultiDayScenarioParams& s = c.scenario;
    s.network.suppliers = 4; s.network.backupSuppliers = 3; s.network.warehouses = 3; s.network.distributionCenters = 3; s.network.customers = 6;
    s.network.capacityFactor = 1.2;
    s.horizon = 30;
    s.transitMin = 1; s.transitMax = 2;
    s.demandAmplitude = 0.1;
    s.supplierFailures = 2; s.supplierFailureDay = 2; s.supplierFailureDuration = 6;
    s.routeFailures = 0; s.routeFailureDay = 3; s.routeFailureDuration = 5;
    s.initialStockDays = 1.0;
    c.simulation = MultiDayConfig{};
    c.runs = runs;
    c.baseSeed = 5000;
    c.policies = multiDayPolicySpecs({"none", "greedy", "bottleneck_aware", "bottleneck_aware_lambda0", "lookahead", "rolling_exact"});
    return c;
}
MultiDayPreset preset(std::string id, std::string title, std::string question, std::string xLabel, std::vector<double> xs, std::string sweep, MultiDayExperimentConfig c, std::string reference = "greedy") {
    MultiDayPreset p;
    p.id = std::move(id); p.title = std::move(title); p.question = std::move(question); p.xLabel = std::move(xLabel);
    p.xValues = std::move(xs); p.sweep = std::move(sweep); p.config = std::move(c); p.reference = std::move(reference);
    return p;
}
}  // namespace

std::vector<MultiDayPreset> standardMultiDayExperiments() {
    std::vector<MultiDayPreset> out;
    out.push_back(preset("M1", "Supplier failures", "How do the policies recover when the k largest regular suppliers fail for 6 days?",
                         "failed suppliers (of 4)", {1, 2, 3}, "supplier_failures", baseConfig(20)));
    MultiDayExperimentConfig routes = baseConfig(20);
    routes.scenario.supplierFailures = 0;
    out.push_back(preset("M2", "Route failures", "How do the policies recover when the busiest routes fail for 5 days?",
                         "failed routes", {1, 2, 4}, "route_failures", routes));
    MultiDayExperimentConfig multiple = baseConfig(20);
    multiple.scenario.routeFailures = 2;
    multiple.scenario.spikePercent = 50; multiple.scenario.spikeStart = 4; multiple.scenario.spikeDuration = 3;
    out.push_back(preset("M3", "Multiple disruptions", "Supplier failures, route failures and a demand spike together.",
                         "(single setting)", {1}, "none", multiple));
    out.push_back(preset("M4", "Spare capacity", "Does the ranking of policies change between tight and loose networks?",
                         "capacity factor (supply and routes relative to demand)", {1.05, 1.2, 1.5, 2.0}, "capacity_factor", baseConfig(20)));
    out.push_back(preset("M5", "Transit times", "How do longer transit times change recovery?",
                         "transit days on every route", {0, 1, 2, 3}, "transit_days", baseConfig(20)));
    out.push_back(preset("M6", "Demand fluctuation", "How does day-to-day demand noise affect the policies?",
                         "demand amplitude", {0.0, 0.2, 0.4, 0.6}, "demand_amplitude", baseConfig(20)));
    out.push_back(preset("M7", "Shortage penalties", "How does the price of a shortage change what is worth buying?",
                         "penalty scale", {0.5, 1.0, 2.0, 4.0}, "penalty_scale", baseConfig(20)));
    MultiDayExperimentConfig horizon = baseConfig(20);
    horizon.policies = multiDayPolicySpecs({"none", "greedy", "lookahead"});
    out.push_back(preset("M8", "Look-ahead horizon", "How much look-ahead is worth its computation?",
                         "look-ahead days", {1, 2, 3, 5, 8, 12}, "lookahead_days", horizon));
    MultiDayExperimentConfig ablation = multiple;
    ablation.runs = 40;
    ablation.policies = multiDayPolicySpecs({"greedy", "bottleneck_aware_lambda0", "bottleneck_aware"});
    out.push_back(preset("M9", "Bottleneck-risk ablation", "Does the risk component change outcomes? Same scoring with lambda = 0 and with lambda = x.",
                         "lambda for bottleneck_aware", {0.5, 1.0, 2.0, 5.0}, "lambda", ablation, "bottleneck_aware_lambda0"));
    out.push_back(preset("M10", "Forecast information", "What is perfect knowledge of the schedule worth to each policy? 0 = persistence, 1 = perfect.",
                         "forecast (0 persistence, 1 perfect)", {0, 1}, "forecast", multiple));
    return out;
}

MultiDayExperimentConfig applySweep(const MultiDayPreset& p, double x) {
    MultiDayExperimentConfig c = p.config;
    const int ix = static_cast<int>(std::lround(x));
    if (p.sweep == "supplier_failures") c.scenario.supplierFailures = ix;
    else if (p.sweep == "route_failures") c.scenario.routeFailures = ix;
    else if (p.sweep == "capacity_factor") c.scenario.network.capacityFactor = x;
    else if (p.sweep == "transit_days") { c.scenario.transitMin = ix; c.scenario.transitMax = ix; }
    else if (p.sweep == "demand_amplitude") c.scenario.demandAmplitude = x;
    else if (p.sweep == "penalty_scale") c.scenario.penaltyScale = x;
    else if (p.sweep == "lookahead_days") {
        for (MultiDayPolicySpec& s : c.policies) if (s.policy == "lookahead") { s.lookaheadDays = ix; s.label = "lookahead"; }
    } else if (p.sweep == "lambda") {
        for (MultiDayPolicySpec& s : c.policies) if (s.policy == "bottleneck_aware") s.lambda = x;
    } else if (p.sweep == "forecast") c.simulation.forecast = ix == 1 ? ForecastMode::Perfect : ForecastMode::Persistence;
    else if (p.sweep != "none") throw std::invalid_argument("unknown sweep '" + p.sweep + "'");
    return c;
}

}  // namespace scx
