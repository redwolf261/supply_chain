#include "optimization/RecoveryPlan.h"

#include <chrono>
#include <set>
#include <stdexcept>
#include <utility>

#include "algorithms/SupplyChainFlow.h"

namespace scx {

std::vector<RecoveryAction> enumerateCandidateActions(const SupplyChainGraph& graph) {
    std::vector<RecoveryAction> candidates;
    for (const Node& node : graph.nodes()) {
        if (node.isBackup && !node.active) {
            candidates.push_back({RecoveryActionType::ActivateSupplier, node.id, node.activationCost});
        }
    }
    for (const Edge& e : graph.edges()) {
        if (!e.active) candidates.push_back({RecoveryActionType::ReopenRoute, e.id, e.reopenCost});
    }
    return candidates;
}

bool isActionLegal(const SupplyChainGraph& graph, const RecoveryAction& action) {
    switch (action.type) {
        case RecoveryActionType::ActivateSupplier:
            return action.targetId >= 0 && action.targetId < graph.nodeCount() &&
                   graph.node(action.targetId).isBackup && !graph.node(action.targetId).active &&
                   action.cost == graph.node(action.targetId).activationCost;
        case RecoveryActionType::ReopenRoute:
            return action.targetId >= 0 && action.targetId < graph.edgeCount() &&
                   !graph.edge(action.targetId).active &&
                   action.cost == graph.edge(action.targetId).reopenCost;
    }
    return false;
}

void applyAction(SupplyChainGraph& graph, const RecoveryAction& action) {
    if (!isActionLegal(graph, action)) throw std::invalid_argument("illegal recovery action in this state");
    if (action.type == RecoveryActionType::ActivateSupplier) graph.setNodeActive(action.targetId, true);
    else graph.setEdgeActive(action.targetId, true);
}

RecoveryPlan buildRecoveryPlan(const SupplyChainGraph& disrupted, const std::string& algorithm,
                               const std::vector<RecoveryAction>& actions) {
    SupplyChainGraph repaired = disrupted;
    RecoveryPlan plan;
    plan.algorithm = algorithm;
    for (const RecoveryAction& action : actions) {
        applyAction(repaired, action);
        plan.actionCost += action.cost;
    }
    plan.actions = actions;
    MinCostFlowResult solved = solveMinCostFlow(repaired);
    plan.flow = std::move(solved.plan);
    plan.flowCost = solved.cost;
    for (const Node& node : repaired.nodes()) plan.unmetDemand += plan.flow.unmetDemand[node.id];
    plan.candidateEvaluations = 1;
    return plan;
}

std::vector<std::string> validateRecoveryPlan(const SupplyChainGraph& disrupted, const RecoveryPlan& plan) {
    std::vector<std::string> problems;
    SupplyChainGraph repaired = disrupted;
    std::set<std::pair<int, int>> seen;
    long long actionCost = 0;
    for (const RecoveryAction& action : plan.actions) {
        if (!seen.insert({static_cast<int>(action.type), action.targetId}).second) {
            problems.push_back("action repeated (its cost would be counted twice)");
            continue;
        }
        if (!isActionLegal(repaired, action)) {
            problems.push_back("illegal action on target " + std::to_string(action.targetId));
            continue;
        }
        applyAction(repaired, action);
        actionCost += action.cost;
    }
    if (actionCost != plan.actionCost) problems.push_back("reported action cost differs from recomputation");

    for (const std::string& violation : validateFlowSolution(repaired, plan.flow)) problems.push_back(violation);
    const CostBreakdown recomputed = computeCost(repaired, plan.flow);
    if (recomputed.total() != plan.flowCost.total()) problems.push_back("reported flow cost differs from recomputation");

    long long unmet = 0;
    for (long long u : plan.flow.unmetDemand) unmet += u;
    if (unmet != plan.unmetDemand) problems.push_back("reported unmet demand differs from recomputation");
    return problems;
}

RecoveryPlan solveRecoveryByExhaustiveSearch(const SupplyChainGraph& disrupted, int maxActions) {
    const auto started = std::chrono::steady_clock::now();
    const std::vector<RecoveryAction> candidates = enumerateCandidateActions(disrupted);
    if (static_cast<int>(candidates.size()) > maxActions) {
        throw std::invalid_argument("too many candidate actions for exhaustive search");
    }
    const unsigned long long subsetCount = 1ULL << candidates.size();
    RecoveryPlan best;
    bool haveBest = false;
    for (unsigned long long mask = 0; mask < subsetCount; ++mask) {
        std::vector<RecoveryAction> subset;
        for (size_t k = 0; k < candidates.size(); ++k) {
            if (mask & (1ULL << k)) subset.push_back(candidates[k]);
        }
        RecoveryPlan plan = buildRecoveryPlan(disrupted, "exhaustive", subset);
        if (!haveBest || plan.totalCost() < best.totalCost()) {
            best = std::move(plan);
            haveBest = true;
        }
    }
    best.candidateEvaluations = static_cast<long long>(subsetCount);
    best.runtimeMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return best;
}

}  // namespace scx
