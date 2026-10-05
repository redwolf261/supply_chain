#include "algorithms/FlowNetwork.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace scx {

FlowNetwork::FlowNetwork(int nodeCount) : adjacentArcIds_(nodeCount) {
    if (nodeCount < 0) throw std::invalid_argument("negative node count");
}

void FlowNetwork::checkNode(int nodeId) const {
    if (nodeId < 0 || nodeId >= nodeCount()) throw std::invalid_argument("flow network node out of range");
}

int FlowNetwork::addArc(int from, int to, long long capacity, long long costPerUnit) {
    checkNode(from);
    checkNode(to);
    if (from == to) throw std::invalid_argument("self-loop arc");
    if (capacity < 0) throw std::invalid_argument("negative arc capacity");
    const int forwardId = static_cast<int>(arcs_.size());
    arcs_.push_back({to, capacity, capacity, costPerUnit});
    arcs_.push_back({from, 0, 0, -costPerUnit});  // reverse arc starts empty, undoes cost
    adjacentArcIds_[from].push_back(forwardId);
    adjacentArcIds_[to].push_back(forwardId + 1);
    return forwardId;
}

long long FlowNetwork::flowOnArc(int forwardArcId) const {
    return arcs_[forwardArcId].originalCapacity - arcs_[forwardArcId].residualCapacity;
}

long long FlowNetwork::edmondsKarp(int source, int sink) {
    checkNode(source);
    checkNode(sink);
    if (source == sink) throw std::invalid_argument("source and sink must differ");

    long long totalFlow = 0;
    while (true) {
        // BFS over arcs that still have residual capacity; remember the arc used to reach each node.
        std::vector<int> arcUsedToReach(nodeCount(), -1);
        std::vector<bool> visited(nodeCount(), false);
        std::queue<int> frontier;
        visited[source] = true;
        frontier.push(source);
        while (!frontier.empty() && !visited[sink]) {
            const int current = frontier.front();
            frontier.pop();
            for (int arcId : adjacentArcIds_[current]) {
                const Arc& arc = arcs_[arcId];
                if (arc.residualCapacity > 0 && !visited[arc.to]) {
                    visited[arc.to] = true;
                    arcUsedToReach[arc.to] = arcId;
                    frontier.push(arc.to);
                }
            }
        }
        if (!visited[sink]) break;  // no augmenting path left => flow is maximum

        long long bottleneck = kInfinity;
        for (int node = sink; node != source; node = arcs_[arcUsedToReach[node] ^ 1].to) {
            bottleneck = std::min(bottleneck, arcs_[arcUsedToReach[node]].residualCapacity);
        }
        for (int node = sink; node != source; node = arcs_[arcUsedToReach[node] ^ 1].to) {
            arcs_[arcUsedToReach[node]].residualCapacity -= bottleneck;
            arcs_[arcUsedToReach[node] ^ 1].residualCapacity += bottleneck;
        }
        totalFlow += bottleneck;
    }
    return totalFlow;
}

std::vector<bool> FlowNetwork::reachableInResidual(int source) const {
    checkNode(source);
    std::vector<bool> visited(nodeCount(), false);
    std::queue<int> frontier;
    visited[source] = true;
    frontier.push(source);
    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        for (int arcId : adjacentArcIds_[current]) {
            const Arc& arc = arcs_[arcId];
            if (arc.residualCapacity > 0 && !visited[arc.to]) {
                visited[arc.to] = true;
                frontier.push(arc.to);
            }
        }
    }
    return visited;
}

FlowNetwork::MinCostResult FlowNetwork::minCostFlow(int source, int sink, long long maxFlow,
                                                     bool stopAtNonNegativePath) {
    checkNode(source);
    checkNode(sink);
    if (source == sink) throw std::invalid_argument("source and sink must differ");
    const int n = nodeCount();

    // Initial potentials = shortest distances from source using Bellman-Ford, because
    // arcs may have negative cost and plain Dijkstra would be wrong on them.
    // After this, every residual arc has non-negative REDUCED cost
    //   reducedCost(u,v) = cost(u,v) + potential[u] - potential[v] >= 0
    // so Dijkstra is valid in every later iteration.
    std::vector<long long> potential(n, kInfinity);
    potential[source] = 0;
    for (int pass = 0; pass < n; ++pass) {
        bool changed = false;
        for (int u = 0; u < n; ++u) {
            if (potential[u] == kInfinity) continue;
            for (int arcId : adjacentArcIds_[u]) {
                const Arc& arc = arcs_[arcId];
                if (arc.residualCapacity > 0 && potential[u] + arc.cost < potential[arc.to]) {
                    potential[arc.to] = potential[u] + arc.cost;
                    changed = true;
                }
            }
        }
        if (!changed) break;
        if (pass == n - 1) throw std::runtime_error("negative-cost cycle in flow network");
    }
    // Nodes unreachable from the source never become reachable later (new residual arcs only
    // appear along augmenting paths), so their potential is irrelevant; use 0.
    for (long long& p : potential) if (p == kInfinity) p = 0;

    MinCostResult result;
    using QueueEntry = std::pair<long long, int>;
    while (result.flow < maxFlow) {
        std::vector<long long> distance(n, kInfinity);
        std::vector<int> arcUsedToReach(n, -1);
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> frontier;
        distance[source] = 0;
        frontier.emplace(0, source);
        while (!frontier.empty()) {
            const auto [distSoFar, u] = frontier.top();
            frontier.pop();
            if (distSoFar > distance[u]) continue;
            for (int arcId : adjacentArcIds_[u]) {
                const Arc& arc = arcs_[arcId];
                if (arc.residualCapacity <= 0) continue;
                const long long reduced = arc.cost + potential[u] - potential[arc.to];
                if (distSoFar + reduced < distance[arc.to]) {
                    distance[arc.to] = distSoFar + reduced;
                    arcUsedToReach[arc.to] = arcId;
                    frontier.emplace(distance[arc.to], arc.to);
                }
            }
        }
        if (distance[sink] == kInfinity) break;  // sink unreachable: nothing more can be sent
        for (int v = 0; v < n; ++v) if (distance[v] != kInfinity) potential[v] += distance[v];

        const long long pathCost = potential[sink] - potential[source];  // true cost per unit
        if (stopAtNonNegativePath && pathCost >= 0) break;

        long long bottleneck = maxFlow - result.flow;
        for (int v = sink; v != source; v = arcs_[arcUsedToReach[v] ^ 1].to) {
            bottleneck = std::min(bottleneck, arcs_[arcUsedToReach[v]].residualCapacity);
        }
        for (int v = sink; v != source; v = arcs_[arcUsedToReach[v] ^ 1].to) {
            arcs_[arcUsedToReach[v]].residualCapacity -= bottleneck;
            arcs_[arcUsedToReach[v] ^ 1].residualCapacity += bottleneck;
        }
        result.flow += bottleneck;
        result.cost += bottleneck * pathCost;
    }
    return result;
}

}  // namespace scx
