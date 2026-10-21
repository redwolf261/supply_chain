#include "experiments/ExperimentRunner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>

#include "algorithms/SupplyChainFlow.h"
#include "util/AllocationTracker.h"

namespace scx {

const char* toString(ScenarioKind kind) {
    switch (kind) {
        case ScenarioKind::RandomDisruption: return "random_disruption";
        case ScenarioKind::SupplierFailures: return "supplier_failures";
        case ScenarioKind::DemandSpike: return "demand_spike";
        case ScenarioKind::RouteCapacityLoss: return "route_capacity_loss";
        case ScenarioKind::NetworkSize: return "network_size";
    }
    return "unknown";
}

ScenarioKind scenarioKindFromString(const std::string& text) {
    for (ScenarioKind kind : {ScenarioKind::RandomDisruption, ScenarioKind::SupplierFailures, ScenarioKind::DemandSpike,
                              ScenarioKind::RouteCapacityLoss, ScenarioKind::NetworkSize}) {
        if (text == toString(kind)) return kind;
    }
    throw std::invalid_argument("unknown experiment kind '" + text + "'");
}

std::vector<StrategySpec> defaultStrategies(const std::vector<std::string>& names, double lambda) {
    std::vector<StrategySpec> specs;
    for (const std::string& name : names) {
        StrategySpec spec{name, name, {}};
        spec.config.lambda = lambda;
        specs.push_back(spec);
    }
    return specs;
}

namespace {

// Layer sizes of the network that `config` will generate for scale `x` (NetworkSize) or unchanged.
GeneratorParams paramsFor(const ExperimentConfig& config, int run) {
    GeneratorParams params = config.network;
    params.seed = config.baseSeed + static_cast<std::uint64_t>(run);
    if (config.kind == ScenarioKind::NetworkSize) {
        auto scaled = [&](int base, int minimum) { return std::max(minimum, static_cast<int>(std::lround(base * config.x))); };
        params.suppliers = scaled(params.suppliers, 1);
        params.backupSuppliers = scaled(params.backupSuppliers, 0);
        params.warehouses = scaled(params.warehouses, 1);
        params.distributionCenters = scaled(params.distributionCenters, 1);
        params.customers = scaled(params.customers, 1);
        params.edges = 0;                       // automatic: enough to connect every layer plus half again
    }
    return params;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return n % 2 == 1 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

}  // namespace

void validateExperimentConfig(const ExperimentConfig& c) {
    if (c.runs < 1) throw std::invalid_argument("runs must be at least 1");
    if (c.timingRepeats < 1 || c.timingRepeats > 100) throw std::invalid_argument("timingRepeats must be between 1 and 100");
    if (c.exactMaxCandidates < 0 || c.exactMaxCandidates > 16) throw std::invalid_argument("exactMaxCandidates must be between 0 and 16");
    if (c.strategies.empty()) throw std::invalid_argument("at least one strategy is required");
    if (!(c.severity >= 0.0 && c.severity <= 1.0)) throw std::invalid_argument("severity must be in [0, 1]");

    std::set<std::string> labels;
    const std::vector<std::string>& known = strategyNames();
    for (const StrategySpec& s : c.strategies) {
        if (!labels.insert(s.label).second) throw std::invalid_argument("duplicate strategy label '" + s.label + "'");
        if (std::find(known.begin(), known.end(), s.strategy) == known.end()) throw std::invalid_argument("unknown strategy '" + s.strategy + "'");
        if (s.config.lambda < 0) throw std::invalid_argument("lambda must be non-negative");
    }

    switch (c.kind) {
        case ScenarioKind::RandomDisruption:
            if (!(c.x >= 0.0 && c.x <= 1.0)) throw std::invalid_argument("severity (x) must be in [0, 1]");
            break;
        case ScenarioKind::SupplierFailures:
            if (!(c.x >= 0.0 && c.x <= 1000.0) || c.x != std::floor(c.x)) throw std::invalid_argument("x must be a whole number of failing suppliers");
            break;
        case ScenarioKind::DemandSpike:
            if (!(c.x >= 0.0 && c.x <= 1000.0)) throw std::invalid_argument("demand increase (x) must be between 0 and 1000 percent");
            break;
        case ScenarioKind::RouteCapacityLoss:
            if (!(c.x >= 0.0 && c.x <= 100.0)) throw std::invalid_argument("capacity loss (x) must be between 0 and 100 percent");
            break;
        case ScenarioKind::NetworkSize:
            if (!(c.x > 0.0 && c.x <= 20.0)) throw std::invalid_argument("size multiplier (x) must be in (0, 20]");
            break;
    }
    const GeneratorParams p = paramsFor(c, 0);
    const long long nodes = static_cast<long long>(p.suppliers) + p.backupSuppliers + p.warehouses + p.distributionCenters + p.customers;
    if (nodes > 2000) throw std::invalid_argument("network too large (more than 2000 nodes)");
    generateNetwork(p);                          // throws std::invalid_argument for an unusable generator configuration
}

ExperimentScenario buildExperimentScenario(const ExperimentConfig& config, int run) {
    const GeneratorParams params = paramsFor(config, run);
    Scenario generated = generateNetwork(params);
    ExperimentScenario out;
    out.initial = generated.graph;
    out.disrupted = generated.graph;
    const SupplyChainGraph& g = out.initial;

    std::vector<Disruption> events;
    switch (config.kind) {
        case ScenarioKind::RandomDisruption:
            events = generateDisruptions(g, config.x, params.seed);
            break;
        case ScenarioKind::NetworkSize:
            events = generateDisruptions(g, config.severity, params.seed);
            break;
        case ScenarioKind::SupplierFailures: {
            std::vector<int> suppliers;
            for (const Node& n : g.nodes()) if (n.type == NodeType::Supplier && n.active && !n.isBackup) suppliers.push_back(n.id);
            std::stable_sort(suppliers.begin(), suppliers.end(), [&](int a, int b) { return g.node(a).supply > g.node(b).supply; });
            const size_t count = std::min(suppliers.size(), static_cast<size_t>(config.x));
            for (size_t i = 0; i < count; ++i) events.push_back({DisruptionType::NodeFailure, suppliers[i], 0});
            break;
        }
        case ScenarioKind::DemandSpike:
            if (config.x > 0) {
                for (const Node& n : g.nodes()) if (n.type == NodeType::Customer) events.push_back({DisruptionType::DemandSpike, n.id, std::llround(config.x)});
            }
            break;
        case ScenarioKind::RouteCapacityLoss: {
            if (config.x <= 0) break;
            const MinCostFlowResult optimal = solveMinCostFlow(g);
            std::vector<int> busy;
            for (const Edge& e : g.edges()) if (optimal.plan.edgeFlow[e.id] > 0) busy.push_back(e.id);
            std::stable_sort(busy.begin(), busy.end(), [&](int a, int b) { return optimal.plan.edgeFlow[a] > optimal.plan.edgeFlow[b]; });
            const size_t count = busy.empty() ? 0 : std::max<size_t>(1, (busy.size() + 3) / 4);      // the busiest quarter
            for (size_t i = 0; i < count; ++i) {
                if (config.x >= 100.0) events.push_back({DisruptionType::RouteFailure, busy[i], 0});
                else events.push_back({DisruptionType::CapacityReduction, busy[i], std::llround(100.0 - config.x)});
            }
            break;
        }
    }
    for (const Disruption& event : events) applyDisruption(out.disrupted, event);
    out.disruptions = static_cast<int>(events.size());
    return out;
}

ExperimentResult runExperiment(const ExperimentConfig& config) {
    validateExperimentConfig(config);
    ExperimentResult result;
    result.config = config;
    double nodeSum = 0, edgeSum = 0;

    for (int run = 0; run < config.runs; ++run) {
        const ExperimentScenario scenario = buildExperimentScenario(config, run);
        const int candidates = static_cast<int>(enumerateCandidateActions(scenario.disrupted).size());
        nodeSum += scenario.initial.nodeCount();
        edgeSum += scenario.initial.edgeCount();

        for (const StrategySpec& spec : config.strategies) {
            RunRecord record;
            record.run = run;
            record.seed = config.baseSeed + static_cast<std::uint64_t>(run);
            record.label = spec.label;
            record.nodes = scenario.initial.nodeCount();
            record.edges = scenario.initial.edgeCount();
            record.candidateActions = candidates;
            record.disruptions = scenario.disruptions;

            if (spec.strategy == "exact" && candidates > config.exactMaxCandidates) {
                record.skipped = true;
                record.skipReason = std::to_string(candidates) + " candidate actions exceed the exhaustive-search limit of " + std::to_string(config.exactMaxCandidates);
                result.records.push_back(record);
                continue;
            }

            std::vector<double> times;
            RecoveryPlan plan;
            for (int repeat = 0; repeat < config.timingRepeats; ++repeat) {
                AllocationScope scope;
                const auto started = std::chrono::steady_clock::now();
                RecoveryPlan current = runStrategy(spec.strategy, scenario.disrupted, spec.config);
                times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
                if (repeat == 0) {
                    plan = std::move(current);
                    if (AllocationTracker::isActive()) record.peakBytes = static_cast<long long>(scope.peakBytesSinceStart());
                }
            }
            const ScenarioMetrics m = evaluatePlan(scenario.initial, scenario.disrupted, plan);
            record.totalCost = m.totalCost;
            record.unmetDemand = m.unmetDemand;
            record.totalDemand = m.totalDemand;
            record.fulfilment = m.fulfilmentRatio;
            record.utilisation = m.networkUtilisation;
            record.feasible = m.feasible;
            record.actions = static_cast<int>(plan.actions.size());
            record.runtimeMs = median(times);
            result.records.push_back(record);
        }
    }
    result.meanNodes = nodeSum / config.runs;
    result.meanEdges = edgeSum / config.runs;

    // Best cost per run among completed strategies, for regret and win counts.
    std::vector<long long> best(config.runs, -1);
    for (const RunRecord& r : result.records) {
        if (r.skipped || !r.feasible) continue;
        if (best[r.run] < 0 || r.totalCost < best[r.run]) best[r.run] = r.totalCost;
    }

    for (const StrategySpec& spec : config.strategies) {
        StrategyAggregate agg;
        agg.spec = spec;
        std::vector<double> cost, unmet, fulfilment, utilisation, runtime, actions, peak, regret;
        int feasible = 0;
        for (const RunRecord& r : result.records) {
            if (r.label != spec.label) continue;
            if (r.skipped) { ++agg.skipped; continue; }
            ++agg.completed;
            cost.push_back(static_cast<double>(r.totalCost));
            unmet.push_back(static_cast<double>(r.unmetDemand));
            fulfilment.push_back(r.fulfilment);
            utilisation.push_back(r.utilisation);
            runtime.push_back(r.runtimeMs);
            actions.push_back(r.actions);
            if (r.peakBytes >= 0) peak.push_back(static_cast<double>(r.peakBytes));
            if (r.feasible) ++feasible;
            if (best[r.run] >= 0) {
                regret.push_back(100.0 * static_cast<double>(r.totalCost - best[r.run]) / static_cast<double>(std::max<long long>(best[r.run], 1)));
                if (r.totalCost == best[r.run]) ++agg.wins;
            }
        }
        agg.cost = summarize(cost);
        agg.unmet = summarize(unmet);
        agg.fulfilment = summarize(fulfilment);
        agg.utilisation = summarize(utilisation);
        agg.runtimeMs = summarize(runtime);
        agg.actions = summarize(actions);
        agg.memoryMeasured = !peak.empty();
        agg.peakBytes = summarize(peak);
        agg.feasibilityRate = agg.completed > 0 ? static_cast<double>(feasible) / agg.completed : 1.0;
        agg.meanRegretPercent = summarize(regret).mean;
        result.aggregates.push_back(agg);
    }
    return result;
}

PairedSummary pairedCost(const ExperimentResult& result, const std::string& labelA, const std::string& labelB) {
    std::vector<double> a, b;
    for (int run = 0; run < result.config.runs; ++run) {
        const RunRecord *ra = nullptr, *rb = nullptr;
        for (const RunRecord& r : result.records) {
            if (r.run != run) continue;
            if (r.label == labelA) ra = &r;
            if (r.label == labelB) rb = &r;
        }
        if (ra && rb && !ra->skipped && !rb->skipped) {
            a.push_back(static_cast<double>(ra->totalCost));
            b.push_back(static_cast<double>(rb->totalCost));
        }
    }
    return pairedCompare(a, b);
}

}  // namespace scx
