#pragma once
// Recovery policies of the multi-day simulation. All implement MultiDayPolicy::decide (step 6 of the day); the
// shipments are always planned by the common dispatch planner, so policies differ only in the actions they buy.
// See docs/MULTI_DAY_SIMULATION.md, section 8.
//
// Every policy works ONLY through PolicyContext: the observed state, the forecast provider (persistence or perfect,
// fixed by the experiment) and the legal candidates. None of them can read the scenario's event schedule.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "multiday/MultiDaySimulator.h"

namespace scx {

// Never buys anything: rerouting and daily replanning only (the baseline).
class NoActionPolicy : public MultiDayPolicy {
public:
    std::string name() const override { return "none"; }
    std::vector<RecoveryAction> decide(const PolicyContext&) override { return {}; }
};

// Buys fixed actions on fixed days (used by tests and to replay a decision sequence).
class ScriptedPolicy : public MultiDayPolicy {
public:
    explicit ScriptedPolicy(std::map<int, std::vector<RecoveryAction>> script, std::string label = "scripted")
        : script_(std::move(script)), label_(std::move(label)) {}
    std::string name() const override { return label_; }
    std::vector<RecoveryAction> decide(const PolicyContext& context) override;
private:
    std::map<int, std::vector<RecoveryAction>> script_;
    std::string label_;
};

// Days until an action can first change a delivery: its lead time plus the shortest transit time (in days) from the
// activated supplier, or from the reopened route, to any customer. -1 if no customer can be reached.
// Computed with Dijkstra on transit days over routes that are usable (or will be, for the reopened route).
int daysUntilFirstDelivery(const PolicyContext& context, const RecoveryAction& action);

// IMMEDIATE-BENEFIT GREEDY (and its bottleneck-aware variant).
// The benefit of an action is judged only up to the first day it can change a delivery (window = that delay + 1).
// Each round: for every remaining candidate, plan that window with and without it; it is eligible only if it reduces
// the planned unmet units and the planned saving exceeds its cost. Take the best score; repeat until none is eligible.
//   greedy:            score = units of planned shortage removed / (cost + eps)
//   bottleneck-aware:  score = L / (cost + lambda * f * L + eps), L = penalty-weighted planned shortage removed,
//                      f = share of planned shipments on route-days at >= the saturation threshold
// lambda = 0 is the ablation (risk term off).
class ImmediateBenefitPolicy : public MultiDayPolicy {
public:
    enum class Score { UnitsPerCost, BottleneckAware };
    ImmediateBenefitPolicy(Score score, double lambda, std::string label);
    std::string name() const override { return label_; }
    std::vector<RecoveryAction> decide(const PolicyContext& context) override;
private:
    Score score_;
    double lambda_;
    std::string label_;
};

// LIMITED LOOK-AHEAD.
// For each candidate: copy the state, apply the action, and simulate `lookaheadDays` days forward with the common
// planner, inside the world the FORECAST describes (so no information leakage), with no further actions. The value of
// a choice is the simulated cost plus its action cost plus the lost-sale penalty on what is still owed at the end of
// the look-ahead. Actions are added one at a time while the best one lowers that value.
// Optional risk term: value + lambda * f * (simulated shortage penalty), f measured on today's plan.
class LookaheadPolicy : public MultiDayPolicy {
public:
    LookaheadPolicy(int lookaheadDays, double lambda = 0.0, std::string label = "");
    std::string name() const override { return label_; }
    std::vector<RecoveryAction> decide(const PolicyContext& context) override;
    int lookaheadDays() const { return lookaheadDays_; }
private:
    long long evaluate(const PolicyContext& context, const std::vector<RecoveryAction>& actions) const;
    int lookaheadDays_;
    double lambda_;
    std::string label_;
};

// OPTIMIZATION-BASED REFERENCE: every day, every subset of the legal candidates is evaluated by the planner over the
// full planning window (planned cost + action cost) and the cheapest subset is bought. Exact for the planner's model
// and the forecast, but exponential: throws PolicyNotApplicable above `maxCandidates`.
class RollingExactPolicy : public MultiDayPolicy {
public:
    explicit RollingExactPolicy(int maxCandidates = 8) : maxCandidates_(maxCandidates) {}
    std::string name() const override { return "rolling_exact"; }
    std::vector<RecoveryAction> decide(const PolicyContext& context) override;
private:
    int maxCandidates_;
};

// The policies by name, as used by the API and the experiments:
//   none, greedy, bottleneck_aware, bottleneck_aware_lambda0, lookahead, rolling_exact
// `lambda` applies to bottleneck_aware; `lookaheadDays` to lookahead.
std::unique_ptr<MultiDayPolicy> makeMultiDayPolicy(const std::string& name, double lambda = 1.0, int lookaheadDays = 5, int maxExactCandidates = 8);
const std::vector<std::string>& multiDayPolicyNames();

// CLAIRVOYANT LOWER BOUND (not a policy). With perfect knowledge of the whole horizon, solves one time-expanded
// min-cost flow over all H days for every subset of the actions that ever become possible, each effective at the
// earliest day it can be. Every policy's executed run is feasible in that model at the same cost (one-off action costs
// never make buying earlier more expensive), so the result is a lower bound on any policy's total cost.
struct ClairvoyantBound {
    bool computed = false;
    std::string reason;                 // why it was not computed
    long long cost = 0;
    std::vector<PendingAction> actions; // the best subset, with effective days
    int candidates = 0;
    double runtimeMs = 0.0;
};
ClairvoyantBound computeClairvoyantBound(const MultiDaySimulator& simulator, int maxCandidates = 10);

}  // namespace scx
