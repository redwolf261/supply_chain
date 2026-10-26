// Multi-day experiment runner: input validation, paired design (every policy gets the same scenario), the
// clairvoyant bound, aggregates, CSV shape, sweeps and determinism.
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "experiments/MultiDayExperiments.h"
#include "test_framework.h"

using namespace scx;

namespace {
MultiDayExperimentConfig smallConfig() {
    MultiDayExperimentConfig c;
    MultiDayScenarioParams& s = c.scenario;
    s.network.suppliers = 3; s.network.backupSuppliers = 2; s.network.warehouses = 2;
    s.network.distributionCenters = 2; s.network.customers = 3;
    s.horizon = 8;
    s.supplierFailures = 1; s.supplierFailureDay = 1; s.supplierFailureDuration = 4;
    c.runs = 3;
    c.baseSeed = 40;
    c.policies = multiDayPolicySpecs({"none", "greedy", "bottleneck_aware", "lookahead"}, 1.0, 3);
    c.clairvoyantMaxCandidates = 6;
    return c;
}

int countLines(const std::string& text) { return static_cast<int>(std::count(text.begin(), text.end(), '\n')); }
}  // namespace

void testValidation() {
    MultiDayExperimentConfig c = smallConfig();
    c.runs = 0;
    SCX_CHECK_THROWS(validateMultiDayExperiment(c), std::invalid_argument);
    c = smallConfig();
    c.policies.clear();
    SCX_CHECK_THROWS(validateMultiDayExperiment(c), std::invalid_argument);
    c = smallConfig();
    c.policies.push_back(c.policies.front());                              // duplicate label
    SCX_CHECK_THROWS(validateMultiDayExperiment(c), std::invalid_argument);
    c = smallConfig();
    c.policies[0].policy = "magic";
    SCX_CHECK_THROWS(validateMultiDayExperiment(c), std::invalid_argument);
    c = smallConfig();
    c.scenario.horizon = 0;
    SCX_CHECK_THROWS(validateMultiDayExperiment(c), std::invalid_argument);
    c = smallConfig();
    c.clairvoyantMaxCandidates = 17;
    SCX_CHECK_THROWS(validateMultiDayExperiment(c), std::invalid_argument);
    validateMultiDayExperiment(smallConfig());                              // a valid config does not throw
    SCX_CHECK(true);
}

// Each row must equal an independent run of that policy on the scenario built from the same seed: the runner adds
// nothing and shares no state between policies.
void testPairedDesignMatchesIndependentRuns() {
    const MultiDayExperimentConfig c = smallConfig();
    const MultiDayExperimentResult r = runMultiDayExperiment(c);
    SCX_CHECK_EQ(static_cast<int>(r.runs.size()), c.runs * static_cast<int>(c.policies.size()));
    for (const MultiDayRunRow& row : r.runs) {
        MultiDayScenarioParams p = c.scenario;
        p.seed = c.baseSeed + static_cast<std::uint64_t>(row.run);
        SCX_CHECK_EQ(row.seed, p.seed);
        const MultiDaySimulator sim(buildMultiDayScenario(p), c.simulation);
        const MultiDayPolicySpec& spec = *std::find_if(c.policies.begin(), c.policies.end(), [&](const MultiDayPolicySpec& s) { return s.label == row.label; });
        auto policy = makeMultiDayPolicy(spec.policy, spec.lambda, spec.lookaheadDays, spec.maxExactCandidates);
        const MultiDayRunResult alone = sim.run(*policy, false);
        SCX_CHECK_EQ(row.totalCost, alone.totalCost);
        SCX_CHECK_EQ(row.totalServed, alone.totalServed);
        SCX_CHECK_EQ(row.actions, alone.actionCount);
        SCX_CHECK(row.feasible);
        if (row.boundComputed) SCX_CHECK(row.bound <= row.totalCost);
    }
    // Same scenario for every label in a run: identical total demand.
    for (int run = 0; run < c.runs; ++run) {
        std::set<long long> demand;
        for (const MultiDayRunRow& row : r.runs) if (row.run == run) demand.insert(row.totalDemand);
        SCX_CHECK_EQ(static_cast<int>(demand.size()), 1);
    }
    SCX_CHECK(r.boundsComputed >= 1);
}

void testAggregatesAndDailyRows() {
    const MultiDayExperimentConfig c = smallConfig();
    const MultiDayExperimentResult r = runMultiDayExperiment(c);
    SCX_CHECK_EQ(static_cast<int>(r.aggregates.size()), static_cast<int>(c.policies.size()));
    SCX_CHECK_EQ(static_cast<int>(r.daily.size()), c.runs * static_cast<int>(c.policies.size()) * c.scenario.horizon);
    int winsTotal = 0;
    for (const MultiDayAggregate& a : r.aggregates) {
        SCX_CHECK_EQ(a.completed + a.skipped, c.runs);
        double sum = 0;
        for (const MultiDayRunRow& row : r.runs) if (row.label == a.spec.label) sum += static_cast<double>(row.totalCost);
        SCX_CHECK(std::abs(a.cost.mean - sum / c.runs) < 1e-6);
        SCX_CHECK(a.gapToBoundPercent.n == 0 || a.gapToBoundPercent.min >= 0.0);   // nobody beats the bound
        SCX_CHECK(a.recoveredRuns <= a.recoveryApplicableRuns);
        winsTotal += a.wins;
    }
    SCX_CHECK(winsTotal >= c.runs);                                         // every run has at least one winner
    // Daily rows add up to the run totals.
    for (const MultiDayRunRow& row : r.runs) {
        long long demand = 0, served = 0, lastCumulative = -1;
        for (const MultiDayDailyRow& d : r.daily) {
            if (d.run != row.run || d.label != row.label) continue;
            demand += d.demand; served += d.served; lastCumulative = d.cumulativeCost;
            SCX_CHECK(d.normalService >= 0.0);                              // the scenario has a disruption
        }
        SCX_CHECK_EQ(demand, row.totalDemand);
        SCX_CHECK_EQ(served, row.totalServed);
        SCX_CHECK_EQ(lastCumulative + row.settlement, row.totalCost);
    }
    // Paired comparison of a policy with itself: zero difference, all ties.
    const PairedSummary self = pairedMultiDayCost(r, "greedy", "greedy");
    SCX_CHECK_EQ(self.n, c.runs);
    SCX_CHECK_EQ(self.ties, c.runs);
    SCX_CHECK(self.meanDifference == 0.0);
}

void testCsvShape() {
    const MultiDayExperimentConfig c = smallConfig();
    const MultiDayExperimentResult r = runMultiDayExperiment(c);
    const std::string runs = multiDayRunsCsvRows("T", 1.5, r);
    const std::string daily = multiDayDailyCsvRows("T", 1.5, r);
    const std::string summary = multiDaySummaryCsvRows("T", 1.5, r);
    SCX_CHECK_EQ(countLines(runs), static_cast<int>(r.runs.size()));
    SCX_CHECK_EQ(countLines(daily), static_cast<int>(r.daily.size()));
    SCX_CHECK_EQ(countLines(summary), static_cast<int>(r.aggregates.size()));
    auto columns = [](const std::string& line) {
        int n = 1;
        bool inQuotes = false;
        for (char ch : line) { if (ch == '"') inQuotes = !inQuotes; else if (ch == ',' && !inQuotes) ++n; }
        return n;
    };
    auto firstLine = [](const std::string& text) { return text.substr(0, text.find('\n')); };
    SCX_CHECK_EQ(columns(firstLine(runs)), columns(firstLine(multiDayRunsCsvHeader())));
    SCX_CHECK_EQ(columns(firstLine(daily)), columns(firstLine(multiDayDailyCsvHeader())));
    SCX_CHECK_EQ(columns(firstLine(summary)), columns(firstLine(multiDaySummaryCsvHeader())));
    SCX_CHECK(runs.rfind("\"T\",1.5000,", 0) == 0);
}

void testPresetsAndSweeps() {
    const std::vector<MultiDayPreset> presets = standardMultiDayExperiments();
    SCX_CHECK_EQ(static_cast<int>(presets.size()), 10);
    std::set<std::string> ids;
    for (const MultiDayPreset& p : presets) {
        ids.insert(p.id);
        SCX_CHECK(!p.xValues.empty());
        for (double x : p.xValues) validateMultiDayExperiment(applySweep(p, x));   // every setting is valid
        const bool hasReference = std::any_of(p.config.policies.begin(), p.config.policies.end(), [&](const MultiDayPolicySpec& s) { return s.label == p.reference; });
        SCX_CHECK(hasReference);
    }
    SCX_CHECK_EQ(static_cast<int>(ids.size()), 10);
    const MultiDayPreset& m5 = presets[4];
    SCX_CHECK_EQ(applySweep(m5, 3).scenario.transitMin, 3);
    SCX_CHECK_EQ(applySweep(m5, 3).scenario.transitMax, 3);
    const MultiDayPreset& m9 = presets[8];
    for (const MultiDayPolicySpec& s : applySweep(m9, 2.0).policies) {
        if (s.label == "bottleneck_aware") SCX_CHECK(s.lambda == 2.0);
        if (s.label == "bottleneck_aware_lambda0") SCX_CHECK(s.lambda == 1.0);   // unchanged; its policy ignores lambda
    }
    SCX_CHECK(applySweep(presets[9], 1).simulation.forecast == ForecastMode::Perfect);
    SCX_CHECK(applySweep(presets[9], 0).simulation.forecast == ForecastMode::Persistence);
    MultiDayPreset broken = presets[0];
    broken.sweep = "nonsense";
    SCX_CHECK_THROWS(applySweep(broken, 1), std::invalid_argument);
}

void testDeterministic() {
    const MultiDayExperimentResult a = runMultiDayExperiment(smallConfig());
    const MultiDayExperimentResult b = runMultiDayExperiment(smallConfig());
    SCX_CHECK_EQ(a.runs.size(), b.runs.size());
    for (std::size_t i = 0; i < a.runs.size(); ++i) {
        SCX_CHECK_EQ(a.runs[i].totalCost, b.runs[i].totalCost);
        SCX_CHECK_EQ(a.runs[i].bound, b.runs[i].bound);
    }
}

int main() {
    SCX_RUN(testValidation);
    SCX_RUN(testPairedDesignMatchesIndependentRuns);
    SCX_RUN(testAggregatesAndDailyRows);
    SCX_RUN(testCsvShape);
    SCX_RUN(testPresetsAndSweeps);
    SCX_RUN(testDeterministic);
    return scxTestExitCode();
}
