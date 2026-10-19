#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "advanced/MergeSort.h"
#include "advanced/Shipments.h"
#include "advanced/TopologicalSort.h"
#include "algorithms/SupplyChainFlow.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace scx;
using namespace scx_test;

namespace {
int ceilLog2(size_t n) { int bits = 0; while ((static_cast<size_t>(1) << bits) < n) ++bits; return bits; }
}  // namespace

void testMergeSortMatchesStableSortOnRandomInput() {
    std::mt19937 rng(1);
    for (size_t n : {0u, 1u, 2u, 3u, 7u, 8u, 9u, 50u, 200u, 1000u}) {
        std::vector<std::pair<int, int>> items;                        // (key, original position): many equal keys
        for (size_t i = 0; i < n; ++i) items.emplace_back(static_cast<int>(rng() % 10), static_cast<int>(i));
        std::vector<std::pair<int, int>> expected = items;
        const auto byKey = [](const auto& a, const auto& b) { return a.first < b.first; };
        std::stable_sort(expected.begin(), expected.end(), byKey);

        const SortStats stats = mergeSort(items, byKey);
        SCX_CHECK(items == expected);                                    // identical to std::stable_sort, including tie order
        if (n >= 2) {
            SCX_CHECK(stats.comparisons <= static_cast<long long>(n) * ceilLog2(n));   // the O(n log n) bound, concretely
            SCX_CHECK(stats.maxDepth <= ceilLog2(n));
            SCX_CHECK_EQ(stats.merges, static_cast<long long>(n) - 1);   // a binary merge tree over n leaves has n - 1 merges
        } else {
            SCX_CHECK_EQ(stats.comparisons, 0LL);
        }
    }
}

void testMergeSortSpecialInputs() {
    std::vector<int> ascending(100), descending(100), equal(100, 5);
    for (int i = 0; i < 100; ++i) { ascending[i] = i; descending[i] = 99 - i; }
    SortStats a = mergeSort(ascending, std::less<int>());
    SortStats d = mergeSort(descending, std::less<int>());
    mergeSort(equal, std::less<int>());
    SCX_CHECK(std::is_sorted(ascending.begin(), ascending.end()));
    SCX_CHECK(std::is_sorted(descending.begin(), descending.end()));
    SCX_CHECK(a.comparisons < d.comparisons);                           // already-sorted input needs fewer comparisons
    SCX_CHECK_EQ(equal[0], 5);

    std::vector<std::string> words = {"pear", "apple", "fig", "banana", "apple"};
    mergeSort(words, std::less<std::string>());
    SCX_CHECK((words == std::vector<std::string>{"apple", "apple", "banana", "fig", "pear"}));
}

void testShipmentRecordsAreExtractedAndSorted() {
    SupplyChainGraph g;
    const int s = addSupplier(g, "S", 100, 1);
    const int w1 = addWarehouse(g, "W1");
    const int w2 = addWarehouse(g, "W2");
    const int c = addCustomer(g, "C", 60, 99);
    addRoute(g, s, w1, 40, 1);
    addRoute(g, s, w2, 40, 2);
    addRoute(g, w1, c, 40, 3);
    addRoute(g, w2, c, 40, 3);
    const MinCostFlowResult solved = solveMinCostFlow(g);

    std::vector<ShipmentRecord> records = extractShipments(g, solved.plan);
    SCX_CHECK_EQ(records.size(), size_t{4});
    long long totalCost = 0;
    for (const ShipmentRecord& r : records) totalCost += r.cost;
    SCX_CHECK_EQ(totalCost, solved.cost.transport);                    // records add up to the plan's transport cost

    std::vector<ShipmentRecord> byCost = records;
    sortShipments(byCost, ShipmentOrder::ByCostDescending);
    for (size_t i = 1; i < byCost.size(); ++i) SCX_CHECK(byCost[i - 1].cost >= byCost[i].cost);
    std::vector<ShipmentRecord> byQuantity = records;
    sortShipments(byQuantity, ShipmentOrder::ByQuantityDescending);
    for (size_t i = 1; i < byQuantity.size(); ++i) SCX_CHECK(byQuantity[i - 1].quantity >= byQuantity[i].quantity);
    std::vector<ShipmentRecord> byRoute = records;
    sortShipments(byRoute, ShipmentOrder::ByRoute);
    SCX_CHECK_EQ(byRoute.front().from, std::string("S"));              // "S" < "W1" < "W2"
    SCX_CHECK_EQ(byRoute.back().from, std::string("W2"));
}

void testTopologicalSortOnDependencyGraphs() {
    // Diamond: 0 before 1 and 2, both before 3. Smallest id first among free nodes gives 0,1,2,3.
    const std::vector<std::pair<int, int>> diamond = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
    TopologicalResult r = topologicalSort(4, diamond);
    SCX_CHECK(r.acyclic);
    SCX_CHECK((r.order == std::vector<int>{0, 1, 2, 3}));
    SCX_CHECK(isValidTopologicalOrder(4, diamond, r.order));

    // Reverse-numbered chain 3 -> 2 -> 1 -> 0 must come out as 3,2,1,0, not sorted by id.
    r = topologicalSort(4, {{3, 2}, {2, 1}, {1, 0}});
    SCX_CHECK((r.order == std::vector<int>{3, 2, 1, 0}));

    r = topologicalSort(3, {});                                         // no dependencies: any order is valid, ids ascending here
    SCX_CHECK((r.order == std::vector<int>{0, 1, 2}));
    SCX_CHECK(topologicalSort(0, {}).acyclic);
    SCX_CHECK(topologicalSort(0, {}).order.empty());
    SCX_CHECK_THROWS(topologicalSort(2, {{0, 5}}), std::invalid_argument);
    SCX_CHECK_THROWS(topologicalSort(-1, {}), std::invalid_argument);

    SCX_CHECK(!isValidTopologicalOrder(4, diamond, {3, 2, 1, 0}));      // violates every edge
    SCX_CHECK(!isValidTopologicalOrder(4, diamond, {0, 1, 1, 3}));      // repeats a node
    SCX_CHECK(!isValidTopologicalOrder(4, diamond, {0, 1, 2}));         // misses a node
}

void testCyclesAreDetectedAndReported() {
    // 0 -> 1 -> 2 -> 1 is a cycle (1,2) with a tail; node 3 depends on the cycle.
    const std::vector<std::pair<int, int>> edges = {{0, 1}, {1, 2}, {2, 1}, {2, 3}};
    TopologicalResult r = topologicalSort(4, edges);
    SCX_CHECK(!r.acyclic);
    SCX_CHECK_EQ(r.order.size(), size_t{1});                            // only node 0 could be ordered
    SCX_CHECK_EQ(r.cycle.size(), size_t{2});
    std::vector<int> sorted = r.cycle;
    std::sort(sorted.begin(), sorted.end());
    SCX_CHECK((sorted == std::vector<int>{1, 2}));

    r = topologicalSort(1, {{0, 0}});                                   // self-dependency
    SCX_CHECK(!r.acyclic);
    SCX_CHECK((r.cycle == std::vector<int>{0}));

    r = topologicalSort(6, {{0, 1}, {1, 0}, {2, 3}, {3, 4}, {4, 2}});   // two separate cycles, one free node (5)
    SCX_CHECK(!r.acyclic);
    SCX_CHECK_EQ(r.order.size(), size_t{1});
    SCX_CHECK_EQ(r.order[0], 5);
}

// Independent oracle: a graph is cyclic iff some node reaches itself through at least one edge (Floyd-Warshall closure).
void testAgainstTransitiveClosureOnRandomGraphs() {
    std::mt19937 rng(31337);
    int cyclic = 0, acyclic = 0;
    for (int trial = 0; trial < 400; ++trial) {
        const int n = 1 + static_cast<int>(rng() % 8);
        std::vector<std::pair<int, int>> edges;
        const int edgeCount = static_cast<int>(rng() % (2 * n + 1));
        for (int e = 0; e < edgeCount; ++e) edges.emplace_back(static_cast<int>(rng() % n), static_cast<int>(rng() % n));

        std::vector<std::vector<bool>> reach(n, std::vector<bool>(n, false));
        for (const auto& [a, b] : edges) reach[a][b] = true;
        for (int k = 0; k < n; ++k) for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) if (reach[i][k] && reach[k][j]) reach[i][j] = true;
        bool expectCyclic = false;
        for (int i = 0; i < n; ++i) expectCyclic = expectCyclic || reach[i][i];

        const TopologicalResult r = topologicalSort(n, edges);
        SCX_CHECK_EQ(!r.acyclic, expectCyclic);
        if (r.acyclic) {
            SCX_CHECK(isValidTopologicalOrder(n, edges, r.order));
            ++acyclic;
        } else {
            // The reported cycle must be a real cycle of the input: consecutive nodes joined by edges, closing back on itself.
            SCX_CHECK(!r.cycle.empty());
            for (size_t i = 0; i < r.cycle.size(); ++i) {
                const std::pair<int, int> link = {r.cycle[i], r.cycle[(i + 1) % r.cycle.size()]};
                SCX_CHECK(std::find(edges.begin(), edges.end(), link) != edges.end());
            }
            std::vector<int> distinct = r.cycle;
            std::sort(distinct.begin(), distinct.end());
            SCX_CHECK(std::unique(distinct.begin(), distinct.end()) == distinct.end());
            ++cyclic;
        }
    }
    SCX_CHECK(cyclic >= 50 && acyclic >= 50);                           // the oracle comparison covers both outcomes
}

// The transportation network may contain loops of routes, so ordering it must report a cycle, not pretend.
void testTransportNetworkIsNotAssumedAcyclic() {
    SupplyChainGraph g;
    const int s = addSupplier(g, "S", 10, 1);
    const int w = addWarehouse(g, "W");
    const int c = addCustomer(g, "C", 5, 9);
    addRoute(g, s, w, 10, 1);
    addRoute(g, w, c, 10, 1);
    TopologicalResult r = topologicalSortOfSupplyChain(g);
    SCX_CHECK(r.acyclic);
    SCX_CHECK((r.order == std::vector<int>{0, 1, 2}));

    const int loopback = addRoute(g, w, s, 10, 1);                      // goods may be sent back upstream
    r = topologicalSortOfSupplyChain(g);
    SCX_CHECK(!r.acyclic);
    SCX_CHECK_EQ(r.cycle.size(), size_t{2});

    g.setEdgeActive(loopback, false);                                   // a closed route is not part of the network
    SCX_CHECK(topologicalSortOfSupplyChain(g).acyclic);
}

int main() {
    SCX_RUN(testMergeSortMatchesStableSortOnRandomInput);
    SCX_RUN(testMergeSortSpecialInputs);
    SCX_RUN(testShipmentRecordsAreExtractedAndSorted);
    SCX_RUN(testTopologicalSortOnDependencyGraphs);
    SCX_RUN(testCyclesAreDetectedAndReported);
    SCX_RUN(testAgainstTransitiveClosureOnRandomGraphs);
    SCX_RUN(testTransportNetworkIsNotAssumedAcyclic);
    return scxTestExitCode();
}
