#include "simulation/Simulation.h"

#include <stdexcept>

namespace scx {

ScenarioMetrics evaluatePlan(const SupplyChainGraph& initial, const SupplyChainGraph& disrupted, const RecoveryPlan& plan) {
    SupplyChainGraph repaired = disrupted;
    for (const RecoveryAction& action : plan.actions) applyAction(repaired, action);

    ScenarioMetrics m;
    m.feasible = validateRecoveryPlan(disrupted, plan).empty();
    m.flowCost = plan.flowCost;
    m.actionCost = plan.actionCost;
    m.totalCost = plan.totalCost();

    const PlanMetrics flow = computeMetrics(repaired, plan.flow);
    m.totalDemand = flow.totalDemand;
    m.unmetDemand = flow.unmetDemand;
    m.deliveredDemand = flow.deliveredDemand;
    m.fulfilmentRatio = flow.fulfilmentRatio;
    m.networkUtilisation = flow.networkUtilisation;

    for (const Node& node : repaired.nodes()) {
        switch (node.type) {
            case NodeType::Supplier: ++m.totalSuppliers; if (node.active) ++m.activeSuppliers; break;
            case NodeType::Factory: ++m.factories; break;
            case NodeType::Warehouse: ++m.warehouses; break;
            case NodeType::DistributionCenter: ++m.distributionCenters; break;
            case NodeType::Customer: ++m.customers; break;
        }
    }
    // Disruptions are read off the state itself (compared with the pristine network), so they stay
    // correct however the state was reached. An activated backup or a recovery-reopened route is not
    // a disruption: reopened routes count as healthy because recovery repaired them.
    for (const Node& node : repaired.nodes()) {
        const Node& original = initial.node(node.id);
        if (original.active && !node.active) ++m.failedNodes;
        if (node.demand > original.demand) ++m.demandSpikes;
    }
    for (const Edge& edge : repaired.edges()) {
        const Edge& original = initial.edge(edge.id);
        if (original.active && !edge.active) ++m.failedRoutes;
        else if (edge.active && edge.capacity < original.capacity) ++m.reducedRoutes;
    }
    m.activeDisruptions = m.failedNodes + m.failedRoutes + m.reducedRoutes + m.demandSpikes;
    return m;
}

Simulation::Simulation(SupplyChainGraph initial) : initial_(std::move(initial)), current_(initial_) {
    const std::vector<std::string> problems = initial_.validate();
    if (!problems.empty()) throw std::invalid_argument("invalid network: " + problems.front());
}

void Simulation::applyDisruption(const Disruption& event) {
    SupplyChainGraph trial = current_;      // apply to a copy so a failing event leaves no partial change
    scx::applyDisruption(trial, event);
    current_ = std::move(trial);
    applied_.push_back(event);
    checkpoint_.reset();                    // the world moved on: an old checkpoint no longer describes it
}

void Simulation::loadScript(std::vector<Disruption> events) {
    script_ = std::move(events);
    nextScriptIndex_ = 0;
}

std::optional<std::string> Simulation::stepForward() {
    if (!hasPendingEvents()) return std::nullopt;
    const Disruption event = script_[nextScriptIndex_];
    const std::string text = describe(current_, event);
    applyDisruption(event);                 // throws before advancing if the scripted event is invalid
    ++nextScriptIndex_;
    return text;
}

RecoveryPlan Simulation::planRecovery(const std::string& strategy, const BottleneckConfig& config) const {
    return runStrategy(strategy, current_, config);
}

void Simulation::executeRecovery(const RecoveryPlan& plan) {
    const std::vector<std::string> problems = validateRecoveryPlan(current_, plan);
    if (!problems.empty()) throw std::invalid_argument("recovery plan rejected: " + problems.front());
    SupplyChainGraph before = current_;
    for (const RecoveryAction& action : plan.actions) scx::applyAction(current_, action);
    checkpoint_ = std::move(before);
    appliedAtCheckpoint_ = applied_.size();
}

void Simulation::resetToInitial() {
    current_ = initial_;
    checkpoint_.reset();
    applied_.clear();
    nextScriptIndex_ = 0;
}

void Simulation::resetToDisruptedState() {
    if (!checkpoint_) throw std::logic_error("no recovery has been executed, nothing to undo");
    current_ = *checkpoint_;
    applied_.resize(appliedAtCheckpoint_);
    checkpoint_.reset();
}

ScenarioRunResult runScenario(const SupplyChainGraph& initial, const std::vector<Disruption>& events,
                              const std::string& strategy, const BottleneckConfig& config) {
    Simulation simulation(initial);         // copies: the caller's network is never modified
    ScenarioRunResult result;
    result.strategy = strategy;

    const RecoveryPlan normal = runStrategy("min_cost_flow", simulation.initial());
    result.normalOperations = evaluatePlan(simulation.initial(), simulation.initial(), normal);

    for (const Disruption& event : events) {
        result.eventDescriptions.push_back(describe(simulation.current(), event));
        simulation.applyDisruption(event);
    }
    result.disruptedNetwork = simulation.current();
    const RecoveryPlan reroutingOnly = runStrategy("min_cost_flow", simulation.current());
    result.disruptedNoRecovery = simulation.metricsFor(reroutingOnly);

    result.plan = simulation.planRecovery(strategy, config);
    simulation.executeRecovery(result.plan);
    // After execution the repairs are already in current_, so measure against the pre-recovery view.
    simulation.resetToDisruptedState();
    result.recovered = simulation.metricsFor(result.plan);
    return result;
}

}  // namespace scx
