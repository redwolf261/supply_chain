#include <cmath>
#include <vector>

#include "experiments/ExperimentIO.h"
#include "experiments/ExperimentPresets.h"
#include "experiments/ExperimentRunner.h"
#include "experiments/Statistics.h"
#include "simulation/ScenarioIO.h"
#include "test_framework.h"

using namespace scx;

namespace {
bool near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) <= tolerance; }

ExperimentConfig smallConfig(ScenarioKind kind, double x, int runs, const std::vector<std::string>& strategies) {
    ExperimentConfig c;
    c.kind = kind;
    c.x = x;
    c.runs = runs;
    c.baseSeed = 11;
    c.network.suppliers = 4; c.network.backupSuppliers = 3; c.network.warehouses = 3;
    c.network.distributionCenters = 3; c.network.customers = 6;
    c.strategies = defaultStrategies(strategies);
    return c;
}

const RunRecord& find(const ExperimentResult& r, int run, const std::string& label) {
    for (const RunRecord& rec : r.records) if (rec.run == run && rec.label == label) return rec;
    throw std::runtime_error("record not found: " + label);
}

std::string fingerprint(const SupplyChainGraph& g) { return networkToJson(g).dump(); }
}  // namespace

void testSummaryByHand() {
    // Classic example: mean 5, sum of squared deviations 32, sample variance 32/7.
    const Summary s = summarize({2, 4, 4, 4, 5, 5, 7, 9});
    SCX_CHECK_EQ(s.n, 8);
    SCX_CHECK(near(s.mean, 5.0));
    SCX_CHECK(near(s.stddev, std::sqrt(32.0 / 7.0)));
    SCX_CHECK(near(s.min, 2.0) && near(s.max, 9.0));
    SCX_CHECK(near(s.ci95HalfWidth, 2.365 * std::sqrt(32.0 / 7.0) / std::sqrt(8.0)));     // t(7) = 2.365

    const Summary one = summarize({3.5});
    SCX_CHECK(one.n == 1 && near(one.mean, 3.5) && near(one.stddev, 0.0) && near(one.ci95HalfWidth, 0.0));
    SCX_CHECK_EQ(summarize({}).n, 0);

    SCX_CHECK(near(tCritical95(1), 12.706) && near(tCritical95(10), 2.228) && near(tCritical95(30), 2.042));
    SCX_CHECK(near(tCritical95(31), 1.96) && near(tCritical95(1000), 1.96));
    for (int df = 1; df < 30; ++df) SCX_CHECK(tCritical95(df) > tCritical95(df + 1));      // more data, tighter interval
    SCX_CHECK_THROWS(tCritical95(0), std::invalid_argument);
}

void testPairedComparisonByHand() {
    const PairedSummary p = pairedCompare({1, 2, 3}, {2, 2, 2});      // differences -1, 0, 1
    SCX_CHECK_EQ(p.n, 3);
    SCX_CHECK(near(p.meanDifference, 0.0));
    SCX_CHECK(near(p.stddev, 1.0));
    SCX_CHECK_EQ(p.firstBetter, 1);
    SCX_CHECK_EQ(p.secondBetter, 1);
    SCX_CHECK_EQ(p.ties, 1);
    SCX_CHECK(near(p.ci95HalfWidth, 4.303 * 1.0 / std::sqrt(3.0)));

    const PairedSummary lower = pairedCompare({10, 12, 11, 13}, {12, 14, 13, 15});   // always exactly 2 cheaper
    SCX_CHECK(near(lower.meanDifference, -2.0) && lower.firstBetter == 4 && near(lower.ci95HalfWidth, 0.0));
    SCX_CHECK_THROWS(pairedCompare({1, 2}, {1}), std::invalid_argument);
}

void testScenarioKindsDoWhatTheySay() {
    // Supplier failures: exactly x regular suppliers are down, the largest first.
    ExperimentConfig c = smallConfig(ScenarioKind::SupplierFailures, 2, 1, {"min_cost_flow"});
    ExperimentScenario s = buildExperimentScenario(c, 0);
    int down = 0, downBackups = 0;
    for (const Node& n : s.disrupted.nodes()) {
        if (n.type == NodeType::Supplier && !n.isBackup && !n.active) ++down;
        if (n.isBackup && s.initial.node(n.id).active != n.active) ++downBackups;
    }
    SCX_CHECK_EQ(down, 2);
    SCX_CHECK_EQ(downBackups, 0);
    int largestRegularSupply = 0, largestId = -1;
    for (const Node& n : s.initial.nodes()) if (n.type == NodeType::Supplier && !n.isBackup && n.supply > largestRegularSupply) { largestRegularSupply = n.supply; largestId = n.id; }
    SCX_CHECK(!s.disrupted.node(largestId).active);                    // the biggest supplier was among the failures

    // Demand spike: every customer's demand grows by x percent (rounded half up), nothing else changes.
    c = smallConfig(ScenarioKind::DemandSpike, 50, 1, {"min_cost_flow"});
    s = buildExperimentScenario(c, 0);
    for (const Node& n : s.initial.nodes()) {
        if (n.type == NodeType::Customer) SCX_CHECK_EQ(s.disrupted.node(n.id).demand, static_cast<int>((n.demand * 150LL + 50) / 100));
        else SCX_CHECK_EQ(s.disrupted.node(n.id).demand, n.demand);
    }

    // Route capacity loss: 100% fails the busiest quarter of routes; 40% cuts them to 60% of design capacity; 0 changes nothing.
    c = smallConfig(ScenarioKind::RouteCapacityLoss, 100, 1, {"min_cost_flow"});
    s = buildExperimentScenario(c, 0);
    int failed = 0;
    for (const Edge& e : s.disrupted.edges()) if (!e.active) ++failed;
    SCX_CHECK(failed >= 1 && failed < s.initial.edgeCount() / 2);
    c.x = 40;
    s = buildExperimentScenario(c, 0);
    int reduced = 0;
    for (const Edge& e : s.disrupted.edges()) if (e.capacity < e.baseCapacity) { ++reduced; SCX_CHECK_EQ(e.capacity, e.baseCapacity * 60 / 100); }
    SCX_CHECK_EQ(reduced, failed);
    c.x = 0;
    s = buildExperimentScenario(c, 0);
    SCX_CHECK_EQ(fingerprint(s.disrupted), fingerprint(s.initial));

    // Severity 0 disrupts nothing; higher severity disrupts more.
    c = smallConfig(ScenarioKind::RandomDisruption, 0.0, 1, {"min_cost_flow"});
    SCX_CHECK_EQ(buildExperimentScenario(c, 0).disruptions, 0);
    c.x = 0.6;
    SCX_CHECK(buildExperimentScenario(c, 0).disruptions > 0);

    // Network size: a larger multiplier gives a larger network.
    c = smallConfig(ScenarioKind::NetworkSize, 1, 1, {"min_cost_flow"});
    const int base = buildExperimentScenario(c, 0).initial.nodeCount();
    c.x = 2;
    const int doubled = buildExperimentScenario(c, 0).initial.nodeCount();
    SCX_CHECK(doubled >= 2 * base - 5 && doubled <= 2 * base + 5);
}

// For every scenario of every kind: all strategies see the same network, results are valid, and the orderings
// that the mathematics guarantees hold in every single run.
void testPerRunInvariants() {
    const std::vector<std::string> all = {"dijkstra_dispatch", "min_cost_flow", "greedy", "bottleneck_aware", "bottleneck_aware_lambda0", "exact"};
    struct Case { ScenarioKind kind; double x; };
    for (const Case& test : {Case{ScenarioKind::RandomDisruption, 0.5}, Case{ScenarioKind::SupplierFailures, 2}, Case{ScenarioKind::DemandSpike, 100},
                             Case{ScenarioKind::RouteCapacityLoss, 70}, Case{ScenarioKind::RandomDisruption, 0.0}}) {
        const ExperimentResult r = runExperiment(smallConfig(test.kind, test.x, 6, all));
        SCX_CHECK_EQ(r.records.size(), size_t{6 * 6});
        for (int run = 0; run < 6; ++run) {
            const RunRecord& dispatch = find(r, run, "dijkstra_dispatch");
            const RunRecord& flow = find(r, run, "min_cost_flow");
            const RunRecord& greedy = find(r, run, "greedy");
            const RunRecord& aware = find(r, run, "bottleneck_aware");
            const RunRecord& ablated = find(r, run, "bottleneck_aware_lambda0");
            const RunRecord& exact = find(r, run, "exact");
            for (const RunRecord* rec : {&dispatch, &flow, &greedy, &aware, &ablated}) {
                SCX_CHECK(!rec->skipped);
                SCX_CHECK(rec->feasible);
                SCX_CHECK(rec->fulfilment >= 0.0 && rec->fulfilment <= 1.0);
                SCX_CHECK(rec->unmetDemand >= 0 && rec->unmetDemand <= rec->totalDemand);
                SCX_CHECK(rec->runtimeMs >= 0.0);
                SCX_CHECK_EQ(rec->nodes, flow.nodes);                                    // identical network for every strategy
                SCX_CHECK_EQ(rec->totalDemand, flow.totalDemand);
            }
            SCX_CHECK(flow.totalCost <= dispatch.totalCost);            // min-cost flow is optimal for the fixed active set
            SCX_CHECK(greedy.totalCost <= flow.totalCost);              // recovery actions are only taken when they pay off
            SCX_CHECK(aware.totalCost <= flow.totalCost);
            SCX_CHECK(ablated.totalCost <= flow.totalCost);
            if (!exact.skipped) {
                SCX_CHECK(exact.totalCost <= greedy.totalCost && exact.totalCost <= aware.totalCost && exact.totalCost <= ablated.totalCost);
            } else {
                SCX_CHECK(exact.candidateActions > 10 && !exact.skipReason.empty());
            }
            if (test.x == 0.0) SCX_CHECK_EQ(greedy.totalCost, flow.totalCost);          // healthy network: nothing to recover
        }
    }
}

void testAggregatesAreConsistentWithRawRecords() {
    const ExperimentResult r = runExperiment(smallConfig(ScenarioKind::RandomDisruption, 0.5, 12, {"min_cost_flow", "greedy", "bottleneck_aware"}));
    for (const StrategyAggregate& agg : r.aggregates) {
        std::vector<double> costs;
        int feasible = 0;
        for (const RunRecord& rec : r.records) if (rec.label == agg.spec.label && !rec.skipped) { costs.push_back(static_cast<double>(rec.totalCost)); feasible += rec.feasible; }
        const Summary expected = summarize(costs);
        SCX_CHECK_EQ(agg.completed, 12);
        SCX_CHECK(near(agg.cost.mean, expected.mean) && near(agg.cost.stddev, expected.stddev));
        SCX_CHECK(agg.cost.min <= agg.cost.mean && agg.cost.mean <= agg.cost.max);
        SCX_CHECK(near(agg.feasibilityRate, static_cast<double>(feasible) / 12.0));
        SCX_CHECK(agg.meanRegretPercent >= 0.0);
        SCX_CHECK(agg.wins >= 0 && agg.wins <= 12);
        SCX_CHECK(!agg.memoryMeasured && agg.peakBytes.n == 0);                           // no allocation hooks in this binary: "not measured"
    }
    int winnersPerRun = 0;                                                                   // someone has the lowest cost in each run
    for (const StrategyAggregate& agg : r.aggregates) winnersPerRun += agg.wins;
    SCX_CHECK(winnersPerRun >= 12);

    // Regret and wins recomputed from the raw records with a separate, deliberately naive implementation.
    for (const StrategyAggregate& agg : r.aggregates) {
        double regretSum = 0.0;
        int wins = 0;
        for (int run = 0; run < 12; ++run) {
            long long best = -1;
            for (const RunRecord& rec : r.records) if (rec.run == run && !rec.skipped && (best < 0 || rec.totalCost < best)) best = rec.totalCost;
            const long long mine = find(r, run, agg.spec.label).totalCost;
            regretSum += 100.0 * static_cast<double>(mine - best) / static_cast<double>(std::max<long long>(best, 1));
            wins += mine == best;
        }
        SCX_CHECK(near(agg.meanRegretPercent, regretSum / 12.0, 1e-9));
        SCX_CHECK_EQ(agg.wins, wins);
    }

    const PairedSummary self = pairedCost(r, "greedy", "greedy");
    SCX_CHECK(self.n == 12 && self.ties == 12 && near(self.meanDifference, 0.0));
    const PairedSummary vsFlow = pairedCost(r, "greedy", "min_cost_flow");
    SCX_CHECK(vsFlow.meanDifference <= 0.0 && vsFlow.secondBetter == 0);                     // greedy never costs more than re-routing alone
}

void testExperimentsAreReproducible() {
    const ExperimentConfig config = smallConfig(ScenarioKind::SupplierFailures, 1, 8, {"min_cost_flow", "greedy", "bottleneck_aware", "dijkstra_dispatch"});
    const ExperimentResult a = runExperiment(config), b = runExperiment(config);
    SCX_CHECK_EQ(a.records.size(), b.records.size());
    for (size_t i = 0; i < a.records.size(); ++i) {                                          // everything except wall-clock time
        SCX_CHECK_EQ(a.records[i].totalCost, b.records[i].totalCost);
        SCX_CHECK_EQ(a.records[i].unmetDemand, b.records[i].unmetDemand);
        SCX_CHECK_EQ(a.records[i].actions, b.records[i].actions);
    }
    ExperimentConfig other = config;
    other.baseSeed = 500;
    const ExperimentResult c = runExperiment(other);
    bool differs = false;
    for (size_t i = 0; i < a.records.size(); ++i) differs = differs || a.records[i].totalCost != c.records[i].totalCost;
    SCX_CHECK(differs);                                                                       // a different seed gives different scenarios
}

void testTimingRepeatsAndRuntimeAreReported() {
    ExperimentConfig config = smallConfig(ScenarioKind::RandomDisruption, 0.4, 3, {"greedy"});
    config.timingRepeats = 3;
    const ExperimentResult r = runExperiment(config);
    for (const RunRecord& rec : r.records) SCX_CHECK(rec.runtimeMs > 0.0);
    SCX_CHECK(r.aggregates[0].runtimeMs.mean > 0.0);
}

void testConfigurationValidation() {
    const std::vector<std::string> one = {"greedy"};
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::RandomDisruption, 0.3, 0, one)), std::invalid_argument);       // no runs
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::RandomDisruption, 1.5, 3, one)), std::invalid_argument);       // severity > 1
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::RandomDisruption, -0.1, 3, one)), std::invalid_argument);
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::SupplierFailures, 1.5, 3, one)), std::invalid_argument);       // not a whole number
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::RouteCapacityLoss, 120, 3, one)), std::invalid_argument);
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::DemandSpike, -5, 3, one)), std::invalid_argument);
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::NetworkSize, 0, 3, one)), std::invalid_argument);
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::NetworkSize, 500, 3, one)), std::invalid_argument);            // far too large
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::RandomDisruption, 0.3, 3, {"nonsense"})), std::invalid_argument);
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::RandomDisruption, 0.3, 3, {})), std::invalid_argument);
    SCX_CHECK_THROWS(runExperiment(smallConfig(ScenarioKind::RandomDisruption, 0.3, 3, {"greedy", "greedy"})), std::invalid_argument);   // duplicate label
    ExperimentConfig badNetwork = smallConfig(ScenarioKind::RandomDisruption, 0.3, 3, one);
    badNetwork.network.customers = 0;
    SCX_CHECK_THROWS(runExperiment(badNetwork), std::invalid_argument);
    ExperimentConfig badLambda = smallConfig(ScenarioKind::RandomDisruption, 0.3, 3, one);
    badLambda.strategies[0].config.lambda = -1;
    SCX_CHECK_THROWS(runExperiment(badLambda), std::invalid_argument);
    SCX_CHECK_THROWS(scenarioKindFromString("meteor"), std::invalid_argument);
    SCX_CHECK(scenarioKindFromString("demand_spike") == ScenarioKind::DemandSpike);
}

void testLambdaVariantsAreSeparateLabels() {
    ExperimentConfig config = smallConfig(ScenarioKind::RandomDisruption, 0.5, 5, {});
    for (double lambda : {0.0, 1.0, 4.0}) {
        StrategySpec spec{"ba_lambda_" + std::to_string(static_cast<int>(lambda)), "bottleneck_aware", {}};
        spec.config.lambda = lambda;
        config.strategies.push_back(spec);
    }
    const ExperimentResult r = runExperiment(config);
    SCX_CHECK_EQ(r.aggregates.size(), size_t{3});
    for (const StrategyAggregate& agg : r.aggregates) SCX_CHECK_EQ(agg.completed, 5);
    // lambda = 0 through the generic name must equal the dedicated ablation strategy.
    ExperimentConfig ablation = smallConfig(ScenarioKind::RandomDisruption, 0.5, 5, {"bottleneck_aware_lambda0"});
    const ExperimentResult a = runExperiment(ablation);
    for (int run = 0; run < 5; ++run) SCX_CHECK_EQ(find(r, run, "ba_lambda_0").totalCost, find(a, run, "bottleneck_aware_lambda0").totalCost);
}

size_t columns(const std::string& line) {
    size_t count = 1;
    bool quoted = false;
    for (char c : line) { if (c == '"') quoted = !quoted; else if (c == ',' && !quoted) ++count; }
    return count;
}

void testConfigParsingFromJson() {
    const ExperimentConfig defaults = experimentConfigFromJson(nlohmann::json::object());
    SCX_CHECK(defaults.kind == ScenarioKind::RandomDisruption);
    SCX_CHECK_EQ(defaults.strategies.size(), strategyNames().size());          // every strategy by default
    SCX_CHECK_EQ(defaults.runs, 10);

    const ExperimentConfig c = experimentConfigFromJson(nlohmann::json::parse(
        R"({"kind":"supplier_failures","x":2,"runs":7,"seed":9,"lambda":3.5,"strategies":["greedy","bottleneck_aware"],
            "network":{"customers":12,"capacityFactor":1.5}})"));
    SCX_CHECK(c.kind == ScenarioKind::SupplierFailures);
    SCX_CHECK(near(c.x, 2.0));
    SCX_CHECK_EQ(c.runs, 7);
    SCX_CHECK_EQ(static_cast<int>(c.baseSeed), 9);
    SCX_CHECK_EQ(c.network.customers, 12);
    SCX_CHECK(near(c.network.capacityFactor, 1.5));
    SCX_CHECK_EQ(c.network.suppliers, GeneratorParams{}.suppliers);              // untouched fields keep their defaults
    SCX_CHECK(near(c.strategies[1].config.lambda, 3.5));

    SCX_CHECK_THROWS(experimentConfigFromJson(nlohmann::json::array()), std::invalid_argument);
    SCX_CHECK_THROWS(experimentConfigFromJson(nlohmann::json{{"runs", 2.5}}), std::invalid_argument);            // must be an integer
    SCX_CHECK_THROWS(experimentConfigFromJson(nlohmann::json{{"runs", -1}}), std::invalid_argument);
    SCX_CHECK_THROWS(experimentConfigFromJson(nlohmann::json{{"severity", 2}}), std::invalid_argument);
    SCX_CHECK_THROWS(experimentConfigFromJson(nlohmann::json{{"strategies", {1, 2}}}), std::invalid_argument);
    SCX_CHECK_THROWS(experimentConfigFromJson(nlohmann::json{{"network", {{"customers", "x"}}}}), std::invalid_argument);
    SCX_CHECK_THROWS(experimentConfigFromJson(nlohmann::json{{"lambda", "big"}}), std::invalid_argument);
}

void testCsvAndJsonOutputAreWellFormed() {
    // exact is skipped in some runs on purpose, so skipped rows are exercised too.
    ExperimentConfig config = smallConfig(ScenarioKind::RandomDisruption, 0.8, 8, {"min_cost_flow", "greedy", "exact"});
    config.exactMaxCandidates = 3;
    const ExperimentResult r = runExperiment(config);
    bool sawSkipped = false;
    for (const RunRecord& rec : r.records) sawSkipped = sawSkipped || rec.skipped;
    SCX_CHECK(sawSkipped);

    const std::string runsCsv = runsCsvHeader() + runsCsvRows("T", 0.8, r);
    const size_t headerColumns = columns(runsCsv.substr(0, runsCsv.find('\n')));
    size_t lines = 0, pos = 0;
    while (pos < runsCsv.size()) {
        const size_t end = runsCsv.find('\n', pos);
        SCX_CHECK_EQ(columns(runsCsv.substr(pos, end - pos)), headerColumns);      // every row, skipped or not, has every column
        ++lines;
        pos = end + 1;
    }
    SCX_CHECK_EQ(lines, r.records.size() + 1);

    const std::string summaryCsv = summaryCsvHeader() + summaryCsvRows("T", 0.8, r);
    const size_t summaryColumns = columns(summaryCsv.substr(0, summaryCsv.find('\n')));
    pos = 0;
    lines = 0;
    while (pos < summaryCsv.size()) {
        const size_t end = summaryCsv.find('\n', pos);
        SCX_CHECK_EQ(columns(summaryCsv.substr(pos, end - pos)), summaryColumns);
        ++lines;
        pos = end + 1;
    }
    SCX_CHECK_EQ(lines, r.aggregates.size() + 1);

    const nlohmann::json j = experimentResultToJson(r, true, "greedy");
    SCX_CHECK_EQ(j["strategies"].size(), size_t{3});
    SCX_CHECK_EQ(j["records"].size(), r.records.size());
    SCX_CHECK(j["strategies"][0].contains("pairedVs") && !j["strategies"][1].contains("pairedVs"));
    SCX_CHECK(j["strategies"][0]["peakMemoryBytes"].is_null());                      // hooks are not linked in this test binary: null, not 0
    SCX_CHECK(!experimentResultToJson(r, false).contains("records"));
    // Numbers in the summary are the numbers of the aggregates.
    SCX_CHECK(near(j["strategies"][1]["cost"]["mean"].get<double>(), r.aggregates[1].cost.mean));
}

void testStandardPresetsAreValidAndRunnable() {
    const std::vector<ExperimentPreset> presets = standardExperiments();
    SCX_CHECK_EQ(presets.size(), size_t{7});
    const char* expectedIds[] = {"A", "B", "C", "D", "E", "F", "G"};
    for (size_t i = 0; i < presets.size(); ++i) {
        const ExperimentPreset& p = presets[i];
        SCX_CHECK_EQ(p.id, std::string(expectedIds[i]));
        SCX_CHECK(!p.xValues.empty() && !p.question.empty() && !p.title.empty());
        for (double x : p.xValues) {                                                     // every setting of every preset is a valid experiment
            ExperimentConfig c = p.config;
            c.x = x;
            validateExperimentConfig(c);
        }
        if (!p.pairedReference.empty()) {
            bool found = false;
            for (const StrategySpec& s : p.config.strategies) found = found || s.label == p.pairedReference;
            SCX_CHECK(found);                                                            // the reference label exists
        }
    }
    // F is the ablation: it must contain lambda = 0 and lambda > 0 versions of the same strategy.
    const ExperimentPreset& f = presets[5];
    bool zero = false, positive = false;
    for (const StrategySpec& s : f.config.strategies) {
        if (s.strategy == "bottleneck_aware" && s.config.lambda == 0.0) zero = true;
        if (s.strategy == "bottleneck_aware" && s.config.lambda > 0.0) positive = true;
    }
    SCX_CHECK(zero && positive);

    const std::vector<SweepPoint> points = runPreset(presets[2], 0.04);                  // C at 4%: 1 run per setting
    SCX_CHECK_EQ(points.size(), presets[2].xValues.size());
    for (const SweepPoint& point : points) {
        SCX_CHECK_EQ(point.result.config.runs, 1);
        for (const StrategyAggregate& a : point.result.aggregates) SCX_CHECK(a.completed + a.skipped == 1);
    }
}

int main() {
    SCX_RUN(testConfigParsingFromJson);
    SCX_RUN(testCsvAndJsonOutputAreWellFormed);
    SCX_RUN(testStandardPresetsAreValidAndRunnable);
    SCX_RUN(testSummaryByHand);
    SCX_RUN(testPairedComparisonByHand);
    SCX_RUN(testScenarioKindsDoWhatTheySay);
    SCX_RUN(testPerRunInvariants);
    SCX_RUN(testAggregatesAreConsistentWithRawRecords);
    SCX_RUN(testExperimentsAreReproducible);
    SCX_RUN(testTimingRepeatsAndRuntimeAreReported);
    SCX_RUN(testConfigurationValidation);
    SCX_RUN(testLambdaVariantsAreSeparateLabels);
    return scxTestExitCode();
}
