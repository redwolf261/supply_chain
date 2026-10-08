#pragma once
// Recovery = choosing which discrete actions to take on a disrupted network, then shipping
// optimally on the repaired network.
//
// Actions (the binary variables w_i, v_e of docs/MATHEMATICAL_MODEL.md):
//   ActivateSupplier(i): bring a BACKUP supplier online      - one-off cost node.activationCost
//   ReopenRoute(e):      reopen an edge that was switched off - one-off cost edge.reopenCost
// Each action may be taken at most once and is charged once, regardless of how much flow uses it.
//
// Given a set of actions the flow part is solved exactly by min-cost flow, so every strategy
// differs ONLY in which actions it chooses. That makes strategies directly comparable:
// total cost = (min-cost flow on the repaired network) + (sum of action costs).

#include <string>
#include <vector>

#include "graph/SupplyChainGraph.h"
#include "optimization/FlowSolution.h"

namespace scx {

enum class RecoveryActionType { ActivateSupplier, ReopenRoute };

struct RecoveryAction {
    RecoveryActionType type = RecoveryActionType::ActivateSupplier;
    int targetId = -1;      // node id or edge id, depending on type
    long long cost = 0;
};

// One accepted greedy-style decision, kept so the UI can replay the reasoning.
struct RecoveryStep {
    RecoveryAction action;
    double score = 0.0;
    long long shortageReduction = 0;   // units of unmet demand removed by this action
    long long costGain = 0;            // reduction of the flow objective (incl. penalties), before paying for the action
    double bottleneckRisk = 0.0;       // f(a): saturated-flow share; only set by the bottleneck-aware heuristic
};

struct RecoveryPlan {
    std::string algorithm;
    std::vector<RecoveryAction> actions;
    std::vector<RecoveryStep> steps;     // empty for strategies that are not step-wise
    FlowSolution flow;                   // optimal shipments on the repaired network
    CostBreakdown flowCost;
    long long actionCost = 0;
    long long unmetDemand = 0;
    long long candidateEvaluations = 0;  // number of min-cost-flow solves the strategy needed
    double runtimeMilliseconds = 0.0;

    long long totalCost() const { return flowCost.total() + actionCost; }
};

// Legal actions in the current state: inactive backup suppliers and inactive edges.
std::vector<RecoveryAction> enumerateCandidateActions(const SupplyChainGraph& graph);

bool isActionLegal(const SupplyChainGraph& graph, const RecoveryAction& action);

// Throws std::invalid_argument if the action is not legal in this state.
void applyAction(SupplyChainGraph& graph, const RecoveryAction& action);

// Repairs a copy of `disrupted` with `actions`, solves min-cost flow, and fills a RecoveryPlan.
RecoveryPlan buildRecoveryPlan(const SupplyChainGraph& disrupted, const std::string& algorithm,
                               const std::vector<RecoveryAction>& actions);

// Independent audit: actions legal and distinct, costs match the graph, flow valid on the repaired
// network, reported cost equals a fresh recomputation. Empty result = valid.
std::vector<std::string> validateRecoveryPlan(const SupplyChainGraph& disrupted, const RecoveryPlan& plan);

// Exact reference: tries every subset of candidate actions (2^k solves). Only for small k;
// throws std::invalid_argument if there are more than `maxActions` candidates.
RecoveryPlan solveRecoveryByExhaustiveSearch(const SupplyChainGraph& disrupted, int maxActions = 16);

}  // namespace scx
