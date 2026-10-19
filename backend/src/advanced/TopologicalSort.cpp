#include "advanced/TopologicalSort.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>

namespace scx {

namespace {

// Finds one directed cycle among the nodes where `inPlay` is true using a DFS with white/grey/black colouring:
// reaching a GREY node means we returned to a node still on the current path, i.e. closed a cycle.
std::vector<int> findCycle(int n, const std::vector<std::vector<int>>& next, const std::vector<bool>& inPlay) {
    enum Colour { White, Grey, Black };
    std::vector<Colour> colour(n, White);
    std::vector<int> path;
    std::vector<std::pair<int, size_t>> stack;       // (node, next neighbour index): an explicit call stack

    for (int start = 0; start < n; ++start) {
        if (!inPlay[start] || colour[start] != White) continue;
        colour[start] = Grey;
        path.push_back(start);
        stack.emplace_back(start, 0);
        while (!stack.empty()) {
            const int node = stack.back().first;
            size_t& index = stack.back().second;
            if (index == next[node].size()) {
                colour[node] = Black;
                path.pop_back();
                stack.pop_back();
                continue;
            }
            const int neighbour = next[node][index++];
            if (!inPlay[neighbour]) continue;
            if (colour[neighbour] == Grey) {
                auto from = std::find(path.begin(), path.end(), neighbour);
                return std::vector<int>(from, path.end());
            }
            if (colour[neighbour] == White) {
                colour[neighbour] = Grey;
                path.push_back(neighbour);
                stack.emplace_back(neighbour, 0);
            }
        }
    }
    return {};
}

}  // namespace

TopologicalResult topologicalSort(int nodeCount, const std::vector<std::pair<int, int>>& edges) {
    if (nodeCount < 0) throw std::invalid_argument("negative node count");
    std::vector<std::vector<int>> next(nodeCount);
    std::vector<int> prerequisitesLeft(nodeCount, 0);
    for (const auto& [from, to] : edges) {
        if (from < 0 || from >= nodeCount || to < 0 || to >= nodeCount) throw std::invalid_argument("edge endpoint out of range");
        next[from].push_back(to);
        ++prerequisitesLeft[to];
    }

    // Smallest id first among free nodes => the same input always gives the same order.
    std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
    for (int v = 0; v < nodeCount; ++v) if (prerequisitesLeft[v] == 0) ready.push(v);

    TopologicalResult result;
    while (!ready.empty()) {
        const int v = ready.top();
        ready.pop();
        result.order.push_back(v);
        for (int w : next[v]) if (--prerequisitesLeft[w] == 0) ready.push(w);
    }

    if (static_cast<int>(result.order.size()) == nodeCount) return result;
    result.acyclic = false;
    std::vector<bool> blocked(nodeCount, false);                    // nodes never released: on a cycle or behind one
    for (int v = 0; v < nodeCount; ++v) blocked[v] = prerequisitesLeft[v] > 0;
    result.cycle = findCycle(nodeCount, next, blocked);
    return result;
}

bool isValidTopologicalOrder(int nodeCount, const std::vector<std::pair<int, int>>& edges, const std::vector<int>& order) {
    if (static_cast<int>(order.size()) != nodeCount) return false;
    std::vector<int> position(nodeCount, -1);
    for (int i = 0; i < nodeCount; ++i) {
        const int v = order[i];
        if (v < 0 || v >= nodeCount || position[v] != -1) return false;       // out of range or repeated
        position[v] = i;
    }
    for (const auto& [from, to] : edges) if (position[from] >= position[to]) return false;
    return true;
}

TopologicalResult topologicalSortOfSupplyChain(const SupplyChainGraph& graph) {
    std::vector<std::pair<int, int>> edges;
    for (const Edge& e : graph.edges()) if (graph.isEdgeUsable(e.id)) edges.emplace_back(e.from, e.to);
    return topologicalSort(graph.nodeCount(), edges);
}

}  // namespace scx
