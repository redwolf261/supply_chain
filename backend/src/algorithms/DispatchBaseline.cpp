#include "algorithms/DispatchBaseline.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>

namespace scx {

FlowSolution dispatchByShortestPaths(const SupplyChainGraph& graph) {
    constexpr long long kInf = std::numeric_limits<long long>::max() / 4;
    const int n = graph.nodeCount();
    FlowSolution plan = FlowSolution::empty(graph);

    std::vector<long long> remainingSupply(n, 0), remainingStock(n, 0), remainingCapacity(graph.edgeCount(), 0);
    for (const Node& node : graph.nodes()) {
        if (!node.active) continue;
        remainingSupply[node.id] = node.supply;
        remainingStock[node.id] = node.inventory;
    }
    for (const Edge& e : graph.edges()) remainingCapacity[e.id] = graph.isEdgeUsable(e.id) ? e.capacity : 0;

    std::vector<int> customers;
    for (const Node& node : graph.nodes()) {
        if (node.type == NodeType::Customer && node.active && node.demand > 0) customers.push_back(node.id);
    }
    std::stable_sort(customers.begin(), customers.end(), [&](int a, int b) {
        return graph.node(a).shortagePenaltyPerUnit > graph.node(b).shortagePenaltyPerUnit;
    });

    using Entry = std::pair<long long, int>;
    for (int customer : customers) {
        while (plan.unmetDemand[customer] > 0) {
            // Multi-source Dijkstra: every node with stock starts at the price of its cheapest unit.
            std::vector<long long> distance(n, kInf);
            std::vector<int> parentEdge(n, -1);
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
            for (int i = 0; i < n; ++i) {
                long long start = kInf;
                if (remainingStock[i] > 0) start = 0;
                else if (remainingSupply[i] > 0) start = graph.node(i).unitPurchaseCost;
                if (start < kInf) { distance[i] = start; frontier.emplace(start, i); }
            }
            while (!frontier.empty()) {
                const auto [d, u] = frontier.top();
                frontier.pop();
                if (d > distance[u]) continue;
                for (int edgeId : graph.outgoingEdgeIds(u)) {
                    if (remainingCapacity[edgeId] <= 0) continue;
                    const Edge& e = graph.edge(edgeId);
                    if (d + e.costPerUnit < distance[e.to]) {
                        distance[e.to] = d + e.costPerUnit;
                        parentEdge[e.to] = edgeId;
                        frontier.emplace(distance[e.to], e.to);
                    }
                }
            }
            if (distance[customer] >= kInf || distance[customer] >= graph.node(customer).shortagePenaltyPerUnit) break;

            // Walk back to the path's start (a node with parentEdge == -1) collecting the bottleneck.
            long long amount = plan.unmetDemand[customer];
            int node = customer;
            while (parentEdge[node] != -1) {
                amount = std::min(amount, remainingCapacity[parentEdge[node]]);
                node = graph.edge(parentEdge[node]).from;
            }
            const int start = node;
            const bool useStock = remainingStock[start] > 0;
            amount = std::min(amount, useStock ? remainingStock[start] : remainingSupply[start]);
            if (amount <= 0) break;  // defensive: cannot happen when distance is finite

            for (int v = customer; parentEdge[v] != -1; v = graph.edge(parentEdge[v]).from) {
                remainingCapacity[parentEdge[v]] -= amount;
                plan.edgeFlow[parentEdge[v]] += amount;
            }
            if (useStock) { remainingStock[start] -= amount; plan.inventoryDraw[start] += amount; }
            else { remainingSupply[start] -= amount; plan.supplyDraw[start] += amount; }
            plan.unmetDemand[customer] -= amount;
        }
    }
    return plan;
}

}  // namespace scx
