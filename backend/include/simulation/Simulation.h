#pragma once
// Simulation state manager.
//
// Keeps three things apart:
//   initial_      the pristine network, never modified after construction
//   current_      the live network that disruptions and executed recovery actions change
//   checkpoint_   a copy of current_ taken just before a recovery plan is executed, so the user can
//                 return to "the same disrupted state" and try a different strategy
//
// An optional event script (a list of disruptions) can be stepped through one event at a time;
// start/pause/resume in the UI are timers around stepForward().

#include <optional>
#include <string>
#include <vector>

#include "simulation/Disruption.h"
#include "simulation/Strategy.h"

namespace scx {

struct ScenarioMetrics {
    CostBreakdown flowCost;
    long long actionCost = 0;
    long long totalCost = 0;          // flowCost.total() + actionCost, no cost counted twice
    long long totalDemand = 0;
    long long unmetDemand = 0;
    long long deliveredDemand = 0;
    double fulfilmentRatio = 1.0;
    double networkUtilisation = 0.0;
    int totalSuppliers = 0, activeSuppliers = 0, factories = 0, warehouses = 0,
        distributionCenters = 0, customers = 0;
    int failedNodes = 0, failedRoutes = 0, reducedRoutes = 0, demandSpikes = 0;
    int activeDisruptions = 0;        // failedNodes + failedRoutes + reducedRoutes + demandSpikes
    bool feasible = true;             // plan passed the independent validator
};

// Evaluates `plan` as applied to `disrupted`, measuring disruption counts against `initial`.
ScenarioMetrics evaluatePlan(const SupplyChainGraph& initial, const SupplyChainGraph& disrupted, const RecoveryPlan& plan);

class Simulation {
public:
    // Throws std::invalid_argument if the network fails structural validation.
    explicit Simulation(SupplyChainGraph initial);

    const SupplyChainGraph& initial() const { return initial_; }
    const SupplyChainGraph& current() const { return current_; }
    const std::vector<Disruption>& appliedEvents() const { return applied_; }

    // Throws std::invalid_argument (or std::out_of_range for a bad id) if the event does not apply;
    // state unchanged in that case.
    void applyDisruption(const Disruption& event);

    // Event script: stepForward applies the next scripted event.
    void loadScript(std::vector<Disruption> events);
    bool hasPendingEvents() const { return nextScriptIndex_ < script_.size(); }
    size_t pendingEventCount() const { return script_.size() - nextScriptIndex_; }
    const std::vector<Disruption>& scriptEvents() const { return script_; }
    size_t nextScriptIndex() const { return nextScriptIndex_; }
    // Applies the next event and returns its description; nullopt when the script is finished.
    std::optional<std::string> stepForward();

    // Computes a plan for the current state WITHOUT changing it.
    RecoveryPlan planRecovery(const std::string& strategy, const BottleneckConfig& config = {}) const;

    // Validates then executes: pays for the actions and repairs current_. Throws std::invalid_argument
    // (state unchanged) if the plan is invalid for the current state.
    void executeRecovery(const RecoveryPlan& plan);
    bool recoveryExecuted() const { return checkpoint_.has_value(); }
    // The disrupted state saved just before recovery was executed; nullptr if none was.
    const SupplyChainGraph* stateBeforeRecovery() const { return checkpoint_ ? &*checkpoint_ : nullptr; }

    ScenarioMetrics metricsFor(const RecoveryPlan& plan) const { return evaluatePlan(initial_, current_, plan); }

    void resetToInitial();             // pristine network, no events applied, script rewound
    void resetToDisruptedState();      // undo executed recovery only; throws std::logic_error if none was executed

private:
    SupplyChainGraph initial_;
    SupplyChainGraph current_;
    std::optional<SupplyChainGraph> checkpoint_;
    std::vector<Disruption> applied_;
    std::vector<Disruption> script_;
    size_t nextScriptIndex_ = 0;
    size_t appliedAtCheckpoint_ = 0;
};

struct ScenarioRunResult {
    std::string strategy;
    std::vector<std::string> eventDescriptions;
    SupplyChainGraph disruptedNetwork;      // state after the events, before any recovery action
    ScenarioMetrics normalOperations;       // optimal plan on the untouched network
    ScenarioMetrics disruptedNoRecovery;    // optimal re-routing only, no paid actions
    RecoveryPlan plan;                      // the strategy's plan
    ScenarioMetrics recovered;              // after executing the plan
};

// Full workflow for one scenario: keep the initial state, apply the events, plan with `strategy`,
// validate, execute, and measure. Throws std::invalid_argument on a bad event or invalid plan.
ScenarioRunResult runScenario(const SupplyChainGraph& initial, const std::vector<Disruption>& events,
                              const std::string& strategy, const BottleneckConfig& config = {});

}  // namespace scx
