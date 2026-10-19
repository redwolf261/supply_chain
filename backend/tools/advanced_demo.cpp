// Prints worked examples of the four syllabus modules (DP, backtracking, merge sort, topological sort) using the
// real implementations. Run:  build\backend\scx_advanced_demo.exe
#include <iomanip>
#include <iostream>

#include "advanced/InventoryDP.h"
#include "advanced/MergeSort.h"
#include "advanced/Shipments.h"
#include "advanced/SupplierSelection.h"
#include "advanced/TopologicalSort.h"
#include "algorithms/SupplyChainFlow.h"
#include "simulation/NetworkGenerator.h"

using namespace scx;

static void heading(const char* text) { std::cout << "\n=== " << text << " ===\n"; }

int main() {
    heading("1. Dynamic programming: inventory over 3 days");
    InventoryProblem inventory;
    inventory.demand = {2, 3, 2};
    inventory.unitCost = {2, 2, 2};
    inventory.setupCost = 5; inventory.holdingCost = 1; inventory.shortagePenalty = 10;
    inventory.maxInventory = 5; inventory.maxOrder = 5;
    const InventoryPlan dp = solveInventoryDP(inventory);
    std::cout << "costToGo[day][starting stock]  (row 3 is the base case)\n   stock:";
    for (int s = 0; s <= inventory.maxInventory; ++s) std::cout << std::setw(5) << s;
    std::cout << "\n";
    for (size_t t = 0; t < dp.costToGo.size(); ++t) {
        std::cout << "   day " << t << ":";
        for (long long v : dp.costToGo[t]) std::cout << std::setw(5) << v;
        std::cout << "\n";
    }
    std::cout << "optimal orders:";
    for (int q : dp.order) std::cout << ' ' << q;
    std::cout << "   total cost " << dp.totalCost << "   (order exactly the daily need: "
              << evaluateInventoryPolicy(inventory, lotForLotOrders(inventory)) << ")\n";

    heading("2. Backtracking: which backup suppliers to activate");
    SelectionProblem selection;
    selection.suppliers = {{"A", 6, 6, 0}, {"B", 6, 6, 0}, {"C", 10, 15, 0}};
    selection.deficit = 10;
    selection.penaltyPerUnit = 100;
    const SelectionResult exact = solveSelectionBacktracking(selection);
    const SelectionResult greedy = solveSelectionGreedy(selection);
    auto names = [&](const SelectionResult& r) { std::string out; for (int i : r.chosen) out += selection.suppliers[i].name + " "; return out; };
    std::cout << "deficit 10 units. Exact (backtracking): activate " << names(exact) << "-> cost " << exact.totalCost
              << ", visited " << exact.nodesVisited << " of " << exact.fullTreeNodes << " tree nodes\n";
    std::cout << "Greedy heuristic:                      activate " << names(greedy) << "-> cost " << greedy.totalCost << "  (not optimal)\n";

    heading("3. Divide and conquer: merge sort of shipment records");
    const Scenario scenario = buildStandardScenario("normal_operations");
    const MinCostFlowResult plan = solveMinCostFlow(scenario.graph);
    std::vector<ShipmentRecord> shipments = extractShipments(scenario.graph, plan.plan);
    sortShipments(shipments, ShipmentOrder::ByCostDescending);
    std::cout << shipments.size() << " shipments, most expensive first:\n";
    for (size_t i = 0; i < shipments.size() && i < 5; ++i)
        std::cout << "   " << shipments[i].from << " -> " << shipments[i].to << "  " << shipments[i].quantity << " units, cost " << shipments[i].cost << "\n";
    std::vector<int> numbers = {5, 2, 9, 1, 5, 6, 3, 8};
    const SortStats stats = mergeSort(numbers, std::less<int>());
    std::cout << "sorting 8 numbers took " << stats.comparisons << " comparisons in " << stats.merges << " merges (bound n*ceil(log2 n) = 24)\n";

    heading("4. Topological sort: manufacturing dependencies");
    const std::vector<std::pair<int, int>> bill = {{0, 2}, {1, 2}, {2, 3}, {2, 4}};   // parts 0,1 -> 2 -> 3,4
    const TopologicalResult order = topologicalSort(5, bill);
    std::cout << "valid build order:";
    for (int v : order.order) std::cout << ' ' << v;
    const TopologicalResult broken = topologicalSort(4, {{0, 1}, {1, 2}, {2, 1}, {2, 3}});
    std::cout << "\nwith a circular dependency 1 -> 2 -> 1: acyclic=" << std::boolalpha << broken.acyclic << ", cycle:";
    for (int v : broken.cycle) std::cout << ' ' << v;
    std::cout << "\n";
    return 0;
}
