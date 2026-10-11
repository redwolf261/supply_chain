#include "simulation/Strategy.h"

#include <chrono>
#include <stdexcept>

#include "algorithms/DispatchBaseline.h"

namespace scx {

const std::vector<std::string>& strategyNames() {
    static const std::vector<std::string> names = {"dijkstra_dispatch", "min_cost_flow", "greedy",
                                                   "bottleneck_aware", "bottleneck_aware_lambda0", "exact"};
    return names;
}

namespace {
template <typename Callable>
RecoveryPlan timed(Callable&& produce) {
    const auto started = std::chrono::steady_clock::now();
    RecoveryPlan plan = produce();
    plan.runtimeMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return plan;
}
}  // namespace

RecoveryPlan runStrategy(const std::string& name, const SupplyChainGraph& disrupted, const BottleneckConfig& config) {
    if (name == "dijkstra_dispatch") {
        return timed([&] {
            RecoveryPlan plan;
            plan.algorithm = name;
            plan.flow = dispatchByShortestPaths(disrupted);
            plan.flowCost = computeCost(disrupted, plan.flow);
            for (long long u : plan.flow.unmetDemand) plan.unmetDemand += u;
            plan.candidateEvaluations = 0;
            return plan;
        });
    }
    if (name == "min_cost_flow") return timed([&] { return buildRecoveryPlan(disrupted, name, {}); });
    if (name == "greedy") return greedyRecovery(disrupted);
    if (name == "bottleneck_aware") return bottleneckAwareRecovery(disrupted, config);
    if (name == "bottleneck_aware_lambda0") {
        BottleneckConfig ablated = config;
        ablated.lambda = 0.0;
        return bottleneckAwareRecovery(disrupted, ablated);
    }
    if (name == "exact") return solveRecoveryByExhaustiveSearch(disrupted);
    throw std::invalid_argument("unknown strategy '" + name + "'");
}

}  // namespace scx
