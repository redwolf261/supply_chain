#pragma once
// Discrete-time multi-day simulation (design: docs/MULTI_DAY_SIMULATION.md).
//
// STATE TRANSITIONS. One day is split in two so that a policy can decide in the middle:
//   beginDay   1 arrivals  2 scheduled events  3 completed actions  4 capacities  5 observe demand
//   (policy)   6 recovery decision
//   finishDay  6 pay for and schedule the actions  7 plan dispatch  8 validate  9 dispatch  10 fulfil
//              11 backlog  12 daily cost  13 record, day += 1
// After the last day, settlement() adds the end-of-horizon lost-sale penalty.
//
// NO INFORMATION LEAKAGE. A policy never receives the scenario. It gets a ForecastProvider, which hides the event
// schedule and exposes only what the configured forecast mode allows (persistence: today's network and demand
// persist; perfect: the true future within the window).

#include <stdexcept>
#include <string>
#include <vector>

#include "multiday/DispatchPlanner.h"
#include "multiday/MultiDayModel.h"

namespace scx {

class ForecastProvider {
public:
    ForecastProvider(const MultiDayScenario& scenario, ForecastMode mode, const MultiDayConfig& config);

    ForecastMode mode() const { return mode_; }
    int horizon() const { return scenario_.horizon; }
    const std::vector<int>& transitDays() const { return scenario_.transitDays; }

    // Planning input for the state's current day (after beginDay) and the next windowDays - 1 days, as the forecast
    // sees them. `boughtToday` are hypothetical actions decided today (effective after their lead time); the state's
    // own pending actions are always included, since a policy knows what it has bought.
    PlanningInput planningInput(const MultiDayState& state, int windowDays, const std::vector<RecoveryAction>& boughtToday = {}) const;
    // The same with hypothetical actions that take effect on explicit days (used by the clairvoyant bound).
    PlanningInput planningInputWith(const MultiDayState& state, int windowDays, const std::vector<PendingAction>& hypothetical) const;

    // A scenario describing the next `days` days exactly as the forecast believes them (used to simulate forward in a
    // look-ahead). Day numbering is kept, so it ends at day min(H, today + days).
    MultiDayScenario forecastScenario(const MultiDayState& state, int days) const;

private:
    const MultiDayScenario& scenario_;
    ForecastMode mode_;
    MultiDayConfig config_;
    std::vector<long long> backlogCost_;
};

// Thrown by a policy that cannot decide on this instance (for example exhaustive search with too many candidates).
// The run is then reported as skipped, never as a result.
class PolicyNotApplicable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// What a policy is given at step 6. Note what is NOT here: the scenario and its event schedule.
struct PolicyContext {
    const MultiDayState& state;
    const ForecastProvider& forecast;
    const MultiDayConfig& config;
    std::vector<RecoveryAction> candidates;    // legal actions today
    int horizon;
    int leadDays(const RecoveryAction& action) const;
};

class MultiDayPolicy {
public:
    virtual ~MultiDayPolicy() = default;
    virtual std::string name() const = 0;
    virtual std::vector<RecoveryAction> decide(const PolicyContext& context) = 0;
};

// Supply lead time: the largest, over all suppliers (backups included), of the shortest transit time in days to any
// customer, ignoring which nodes and routes are currently up. -1 when no supplier reaches a customer.
int supplyLeadDays(const MultiDayScenario& scenario);
// Shortest planning window that can see a purchase pay off: the longest action lead + supplyLeadDays + 1. A shorter
// window never values goods that are bought or dispatched today, so the planner would let the pipeline run dry.
int minimumPlanningWindow(const MultiDayScenario& scenario, const MultiDayConfig& config);

class MultiDaySimulator {
public:
    // Throws std::invalid_argument for an invalid scenario or configuration. The planning window is raised to
    // minimumPlanningWindow() when the configured one is shorter; config() returns the window actually used.
    MultiDaySimulator(MultiDayScenario scenario, MultiDayConfig config);

    const MultiDayConfig& config() const { return config_; }
    int horizon() const { return scenario_.horizon; }

    MultiDayState initialState() const;
    void beginDay(MultiDayState& state) const;
    DayRecord finishDay(MultiDayState& state, const std::vector<RecoveryAction>& actions, const ForecastProvider& provider, bool snapshots) const;
    // Lost-sale penalty for everything still owed (after the last day).
    long long settlement(const MultiDayState& state) const;

    // Legal recovery actions now: inactive backup suppliers and inactive routes that are not already pending.
    std::vector<RecoveryAction> candidateActions(const MultiDayState& state) const;

    // A complete run of one policy from day 0. `snapshots` keeps per-node and per-route data for the dashboard.
    MultiDayRunResult run(MultiDayPolicy& policy, bool snapshots = true) const;
    // The same network, initial state and demand noise without any disruption: no events and no demand spike.
    MultiDayScenario baselineScenario() const;
    // Daily service level of the baseline scenario with no recovery actions: "normal service".
    std::vector<double> normalService() const;

    // Read access for the experiment layer and the clairvoyant bound (NOT given to policies).
    const MultiDayScenario& scenario() const { return scenario_; }

private:
    MultiDayScenario scenario_;
    MultiDayConfig config_;
    std::vector<long long> backlogCost_;
};

// Recovery time as defined in the design (section 9). `firstDisruptionDay` < 0 means not applicable.
// Day d counts as 'served normally' when serviceLevels[d] >= min(threshold, normal[d]); with an empty `normal`
// the requirement is the threshold alone. Definition: docs/MULTI_DAY_SIMULATION.md, section 9.
RecoveryTime computeRecoveryTime(const std::vector<double>& serviceLevels, int firstDisruptionDay, double threshold, int stabilityDays,
                                 const std::vector<double>& normal = {});

}  // namespace scx
