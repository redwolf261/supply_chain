#include "experiments/ExperimentPresets.h"

#include <algorithm>
#include <cmath>

namespace scx {

namespace {

// Base network for every experiment except the size sweep: 6 + 3 suppliers, 4 warehouses, 4 distribution centers,
// 10 customers (27 nodes). Capacity factor 1.2 means supply and routes are only 20% above demand at first; the
// generator raises them just enough that the healthy network can serve everything.
GeneratorParams baseNetwork() {
    GeneratorParams p;
    p.suppliers = 6; p.backupSuppliers = 3; p.warehouses = 4; p.distributionCenters = 4; p.customers = 10;
    p.capacityFactor = 1.2;
    return p;
}

std::vector<StrategySpec> allStrategies() {
    return defaultStrategies({"dijkstra_dispatch", "min_cost_flow", "greedy", "bottleneck_aware", "bottleneck_aware_lambda0", "exact"}, 1.0);
}

std::vector<StrategySpec> withoutExact() {
    return defaultStrategies({"dijkstra_dispatch", "min_cost_flow", "greedy", "bottleneck_aware", "bottleneck_aware_lambda0"}, 1.0);
}

ExperimentPreset make(const std::string& id, const std::string& title, const std::string& question, const std::string& xLabel,
                      std::vector<double> xs, ScenarioKind kind, int runs, std::vector<StrategySpec> strategies, const std::string& reference) {
    ExperimentPreset p;
    p.id = id; p.title = title; p.question = question; p.xLabel = xLabel; p.xValues = std::move(xs);
    p.config.kind = kind;
    p.config.network = baseNetwork();
    p.config.runs = runs;
    p.config.baseSeed = 1000;
    p.config.strategies = std::move(strategies);
    p.pairedReference = reference;
    return p;
}

}  // namespace

std::vector<ExperimentPreset> standardExperiments() {
    std::vector<ExperimentPreset> presets;

    ExperimentPreset a = make("A", "Increasing network size",
        "How does each strategy's running time (and memory) grow with the size of the network?",
        "size multiplier (27 nodes at 1)", {1, 2, 3, 4, 6, 8}, ScenarioKind::NetworkSize, 5, withoutExact(), "min_cost_flow");
    a.config.severity = 0.3;
    a.config.timingRepeats = 3;
    presets.push_back(a);

    presets.push_back(make("B", "Increasing disruption severity",
        "How do total cost and demand fulfilment change as more of the network is disrupted?",
        "severity (0 to 1)", {0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.8, 1.0}, ScenarioKind::RandomDisruption, 30, allStrategies(), "greedy"));

    presets.push_back(make("C", "Supplier failures",
        "How well do the strategies recover when the k largest regular suppliers shut down?",
        "number of failed suppliers (of 6)", {1, 2, 3, 4, 5}, ScenarioKind::SupplierFailures, 30, allStrategies(), "greedy"));

    presets.push_back(make("D", "Route bottlenecks",
        "How do the strategies behave when the busiest quarter of routes lose capacity?",
        "capacity lost on the busiest quarter of routes (%)", {0, 20, 40, 60, 80, 100}, ScenarioKind::RouteCapacityLoss, 30, allStrategies(), "greedy"));

    presets.push_back(make("E", "Demand spikes",
        "How do the strategies cope when every customer's demand rises suddenly?",
        "demand increase (%)", {0, 25, 50, 100, 150, 200}, ScenarioKind::DemandSpike, 30, allStrategies(), "greedy"));

    ExperimentPreset f = make("F", "Heuristic ablation (bottleneck-risk term)",
        "Does the bottleneck-risk term change the outcome? Same scoring with and without it, and at several weights, on identical scenarios.",
        "severity (fixed)", {0.5}, ScenarioKind::RandomDisruption, 100, {}, "bottleneck_aware(lambda=0)");
    StrategySpec flow{"min_cost_flow", "min_cost_flow", {}};
    StrategySpec greedy{"greedy", "greedy", {}};
    f.config.strategies = {flow, greedy};
    for (double lambda : {0.0, 0.25, 0.5, 1.0, 2.0, 5.0}) {
        StrategySpec spec;
        spec.strategy = "bottleneck_aware";
        spec.config.lambda = lambda;
        char label[64];
        std::snprintf(label, sizeof(label), "bottleneck_aware(lambda=%g)", lambda);
        spec.label = label;
        f.config.strategies.push_back(spec);
    }
    presets.push_back(f);

    presets.push_back(make("G", "Repeated random scenarios",
        "Averages, spread and extremes over many independently seeded random scenarios at one disruption level.",
        "severity (fixed)", {0.4}, ScenarioKind::RandomDisruption, 200, allStrategies(), "greedy"));
    return presets;
}

std::vector<SweepPoint> runPreset(const ExperimentPreset& preset, double runsScale) {
    std::vector<SweepPoint> points;
    for (double x : preset.xValues) {
        ExperimentConfig config = preset.config;
        config.x = x;
        config.runs = std::max(1, static_cast<int>(std::lround(preset.config.runs * runsScale)));
        points.push_back({x, runExperiment(config)});
    }
    return points;
}

}  // namespace scx
