#include "algorithms/RecoveryHeuristics.h"

#include <chrono>
#include <functional>
#include <stdexcept>

#include "algorithms/SupplyChainFlow.h"

namespace scx {

namespace {

long long totalUnmet(const FlowSolution& plan) {
    long long total = 0;
    for (long long u : plan.unmetDemand) total += u;
    return total;
}

// Everything a scoring rule may look at for one trial action.
struct TrialOutcome {
    RecoveryAction action;
    const SupplyChainGraph* repaired;
    const FlowSolution* flowAfter;
    long long shortageReduction;
    long long costGain;
    long long penaltyWeightedReduction;  // L(a)
};

struct Score {
    double value = 0.0;
    double risk = 0.0;
};

using ScoreFunction = std::function<Score(const TrialOutcome&)>;

RecoveryPlan runGreedyLoop(const SupplyChainGraph& disrupted, const std::string& name, const ScoreFunction& score) {
    const auto started = std::chrono::steady_clock::now();
    SupplyChainGraph state = disrupted;
    MinCostFlowResult current = solveMinCostFlow(state);
    std::vector<RecoveryAction> chosen;
    std::vector<RecoveryStep> steps;
    long long evaluations = 1;

    while (true) {
        bool haveBest = false;
        RecoveryStep bestStep;
        for (const RecoveryAction& candidate : enumerateCandidateActions(state)) {
            SupplyChainGraph trial = state;  // never touch the real state while evaluating
            applyAction(trial, candidate);
            const MinCostFlowResult after = solveMinCostFlow(trial);
            ++evaluations;

            TrialOutcome outcome{candidate, &trial, &after.plan, 0, 0, 0};
            outcome.shortageReduction = totalUnmet(current.plan) - totalUnmet(after.plan);
            outcome.costGain = current.cost.total() - after.cost.total();
            for (const Node& node : trial.nodes()) {
                outcome.penaltyWeightedReduction +=
                    (current.plan.unmetDemand[node.id] - after.plan.unmetDemand[node.id]) * node.shortagePenaltyPerUnit;
            }
            // Eligibility: must reduce shortage AND pay for itself.
            if (outcome.shortageReduction <= 0 || outcome.costGain <= candidate.cost) continue;

            const Score s = score(outcome);
            const bool better = !haveBest || s.value > bestStep.score ||
                                (s.value == bestStep.score && outcome.costGain > bestStep.costGain);
            if (better) {
                haveBest = true;
                bestStep = {candidate, s.value, outcome.shortageReduction, outcome.costGain, s.risk};
            }
        }
        if (!haveBest) break;
        applyAction(state, bestStep.action);
        current = solveMinCostFlow(state);
        ++evaluations;
        chosen.push_back(bestStep.action);
        steps.push_back(bestStep);
    }

    RecoveryPlan plan = buildRecoveryPlan(disrupted, name, chosen);
    plan.steps = steps;
    plan.candidateEvaluations = evaluations;
    plan.runtimeMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return plan;
}

}  // namespace

double saturatedFlowShare(const SupplyChainGraph& graph, const FlowSolution& plan, double threshold) {
    long long shipped = 0, onSaturated = 0;
    for (const Edge& e : graph.edges()) {
        const long long flow = plan.edgeFlow[e.id];
        if (flow <= 0) continue;
        shipped += flow;
        if (e.capacity > 0 && static_cast<double>(flow) >= threshold * e.capacity) onSaturated += flow;
    }
    return shipped == 0 ? 0.0 : static_cast<double>(onSaturated) / shipped;
}

RecoveryPlan greedyRecovery(const SupplyChainGraph& disrupted) {
    const double epsilon = 1e-9;
    return runGreedyLoop(disrupted, "greedy", [epsilon](const TrialOutcome& t) {
        return Score{static_cast<double>(t.shortageReduction) / (static_cast<double>(t.action.cost) + epsilon), 0.0};
    });
}

RecoveryPlan bottleneckAwareRecovery(const SupplyChainGraph& disrupted, const BottleneckConfig& config) {
    if (config.lambda < 0 || config.epsilon <= 0 || config.saturationThreshold <= 0 || config.saturationThreshold > 1) {
        throw std::invalid_argument("invalid bottleneck-aware configuration");
    }
    const std::string name = config.lambda == 0.0 ? "bottleneck_aware_lambda0" : "bottleneck_aware";
    return runGreedyLoop(disrupted, name, [config](const TrialOutcome& t) {
        const double avoidedLoss = static_cast<double>(t.penaltyWeightedReduction);
        const double share = saturatedFlowShare(*t.repaired, *t.flowAfter, config.saturationThreshold);
        const double risk = share * avoidedLoss;
        return Score{avoidedLoss / (static_cast<double>(t.action.cost) + config.lambda * risk + config.epsilon), share};
    });
}

}  // namespace scx
