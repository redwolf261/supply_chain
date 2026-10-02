#include "algorithms/Dijkstra.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

namespace scx {

std::vector<int> ShortestPathResult::nodePathTo(int target) const {
    std::vector<int> path;
    if (target < 0 || target >= static_cast<int>(distance.size()) || !isReachable(target)) return path;
    for (int v = target; v != -1; v = parentNode[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<int> ShortestPathResult::edgePathTo(int target) const {
    std::vector<int> path;
    if (target < 0 || target >= static_cast<int>(distance.size()) || !isReachable(target)) return path;
    for (int v = target; parentEdge[v] != -1; v = parentNode[v]) path.push_back(parentEdge[v]);
    std::reverse(path.begin(), path.end());
    return path;
}

ShortestPathResult dijkstra(const SupplyChainGraph& graph, int source) {
    graph.node(source);  // validates the id
    ShortestPathResult result;
    result.source = source;
    result.distance.assign(graph.nodeCount(), ShortestPathResult::kUnreachable);
    result.parentNode.assign(graph.nodeCount(), -1);
    result.parentEdge.assign(graph.nodeCount(), -1);
    if (!graph.node(source).active) return result;

    // Min-heap of (distance, node). std::priority_queue is a max-heap by default,
    // so std::greater flips it. Stale entries are skipped instead of decreased in place.
    using QueueEntry = std::pair<long long, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> frontier;
    result.distance[source] = 0;
    frontier.emplace(0, source);

    while (!frontier.empty()) {
        const auto [costSoFar, current] = frontier.top();
        frontier.pop();
        if (costSoFar > result.distance[current]) continue;  // outdated entry

        for (int edgeId : graph.outgoingEdgeIds(current)) {
            if (!graph.isEdgeUsable(edgeId)) continue;
            const Edge& e = graph.edge(edgeId);
            // Relaxation: is going through `current` cheaper than the best known way to e.to?
            const long long candidate = costSoFar + e.costPerUnit;
            if (candidate < result.distance[e.to]) {
                result.distance[e.to] = candidate;
                result.parentNode[e.to] = current;
                result.parentEdge[e.to] = edgeId;
                frontier.emplace(candidate, e.to);
            }
        }
    }
    return result;
}

}  // namespace scx
