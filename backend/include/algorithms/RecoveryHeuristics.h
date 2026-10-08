#pragma once
// Algorithm 4 (greedy recovery) and Algorithm 7 (experimental bottleneck-aware recovery).
//
// Both are step-wise greedy strategies over the same candidate actions. In every round, each
// legal candidate is trial-applied to a COPY of the current planning state, min-cost flow is
// re-solved, and the candidate is scored. Candidates are only eligible if
//     costGain > action cost          (the action pays for itself on the flow objective)
//     shortageReduction > 0           (it actually reduces unmet demand)
// so a plan's total cost never exceeds the do-nothing cost. The best-scoring candidate is applied
// and everything is re-scored on the new state, until no eligible candidate remains.
//
// They differ only in the score:
//   greedy:            S(a) = shortageReduction(a) / (cost(a) + eps)                 [units per currency]
//   bottleneck-aware:  S(a) = L(a) / (cost(a) + lambda * R(a) + eps)                 [dimensionless ratio]
//     L(a) = sum_j penalty_j * unmetReduction_j   (avoided shortage loss, in currency)
//     R(a) = f(a) * L(a), where f(a) in [0,1] is the share of shipped units that travel on edges whose
//            utilisation is >= saturationThreshold in the repaired plan. f measures how fragile the plan is:
//            if most flow squeezes through near-saturated edges, the benefit L(a) is exposed to those
//            bottlenecks, so R(a) is the part of the avoided loss "at risk".
//   lambda = 0 removes the risk term (used for the ablation experiment).
//
// NEITHER heuristic is claimed to be optimal. The bottleneck-aware score is an experimental idea:
// whether it helps is decided by the experiments, not assumed.

#include "optimization/RecoveryPlan.h"

namespace scx {

struct BottleneckConfig {
    double lambda = 1.0;                // weight of the bottleneck-risk term, >= 0
    double saturationThreshold = 0.9;   // an edge counts as a bottleneck at utilisation >= this
    double epsilon = 1e-9;              // avoids division by zero for free actions
};

RecoveryPlan greedyRecovery(const SupplyChainGraph& disrupted);
RecoveryPlan bottleneckAwareRecovery(const SupplyChainGraph& disrupted, const BottleneckConfig& config = {});

// f(a) above: fraction of shipped units on edges with utilisation >= threshold. 0 if nothing ships.
double saturatedFlowShare(const SupplyChainGraph& graph, const FlowSolution& plan, double threshold);

}  // namespace scx
