#include "algorithms/GraphTraversal.h"

#include <queue>
#include <utility>

namespace scx {

namespace {
TraversalResult makeEmptyResult(const SupplyChainGraph& graph, int source) {
    graph.node(source);  // validates the id (throws std::out_of_range)
    TraversalResult result;
    result.source = source;
    result.reached.assign(graph.nodeCount(), false);
    result.parentNode.assign(graph.nodeCount(), -1);
    result.parentEdge.assign(graph.nodeCount(), -1);
    result.distanceInHops.assign(graph.nodeCount(), -1);
    return result;
}
}  // namespace

TraversalResult breadthFirstSearch(const SupplyChainGraph& graph, int source) {
    TraversalResult result = makeEmptyResult(graph, source);
    if (!graph.node(source).active) return result;

    std::queue<int> frontier;
    result.reached[source] = true;  // mark when ENQUEUED so a node is never queued twice
    result.distanceInHops[source] = 0;
    frontier.push(source);

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        result.visitOrder.push_back(current);
        for (int edgeId : graph.outgoingEdgeIds(current)) {
            if (!graph.isEdgeUsable(edgeId)) continue;
            const int next = graph.edge(edgeId).to;
            if (result.reached[next]) continue;
            result.reached[next] = true;
            result.parentNode[next] = current;
            result.parentEdge[next] = edgeId;
            result.distanceInHops[next] = result.distanceInHops[current] + 1;
            frontier.push(next);
        }
    }
    return result;
}

TraversalResult depthFirstSearch(const SupplyChainGraph& graph, int source) {
    TraversalResult result = makeEmptyResult(graph, source);
    if (!graph.node(source).active) return result;

    // Each stack frame remembers how far we got through that node's edge list,
    // which is exactly the state a recursive call would keep on the call stack.
    std::vector<std::pair<int, size_t>> callStack;
    result.reached[source] = true;
    result.visitOrder.push_back(source);
    callStack.emplace_back(source, 0);

    while (!callStack.empty()) {
        const int current = callStack.back().first;
        size_t& nextEdgeIndex = callStack.back().second;
        const std::vector<int>& edgeIds = graph.outgoingEdgeIds(current);

        if (nextEdgeIndex == edgeIds.size()) {  // all neighbours explored: node is "finished"
            result.finishOrder.push_back(current);
            callStack.pop_back();
            continue;
        }
        const int edgeId = edgeIds[nextEdgeIndex++];
        if (!graph.isEdgeUsable(edgeId)) continue;
        const int next = graph.edge(edgeId).to;
        if (result.reached[next]) continue;
        result.reached[next] = true;
        result.parentNode[next] = current;
        result.parentEdge[next] = edgeId;
        result.visitOrder.push_back(next);
        callStack.emplace_back(next, 0);
    }
    return result;
}

std::vector<int> nodesLostBetween(const TraversalResult& before, const TraversalResult& after) {
    std::vector<int> lost;
    for (size_t v = 0; v < before.reached.size(); ++v) {
        if (before.reached[v] && !(v < after.reached.size() && after.reached[v])) {
            lost.push_back(static_cast<int>(v));
        }
    }
    return lost;
}

}  // namespace scx
