#include "advanced/SupplierSelection.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace scx {

namespace {

// Cost of buying `deficit` units from the available suppliers (indices in `byUnitCost` order), cheapest first,
// skipping units that cost at least the penalty. Returns variable cost + penalty on unserved units.
long long fillCost(const SelectionProblem& p, const std::vector<int>& byUnitCost, const std::vector<bool>& available, int* unservedOut) {
    long long cost = 0;
    int remaining = p.deficit;
    for (int index : byUnitCost) {
        if (remaining == 0) break;
        if (!available[index]) continue;
        const BackupSupplier& s = p.suppliers[index];
        if (s.unitCost >= p.penaltyPerUnit) break;                    // sorted ascending: everything after is dearer too
        const int units = std::min(s.capacity, remaining);
        cost += static_cast<long long>(units) * s.unitCost;
        remaining -= units;
    }
    cost += p.penaltyPerUnit * remaining;
    if (unservedOut) *unservedOut = remaining;
    return cost;
}

std::vector<int> orderByUnitCost(const SelectionProblem& p) {
    std::vector<int> order(p.suppliers.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return p.suppliers[a].unitCost < p.suppliers[b].unitCost; });
    return order;
}

}  // namespace

void validateSelectionProblem(const SelectionProblem& p) {
    if (p.suppliers.size() > 24) throw std::invalid_argument("backtracking is exponential: at most 24 suppliers");
    if (p.deficit < 0 || p.penaltyPerUnit < 0) throw std::invalid_argument("deficit and penalty must be non-negative");
    for (const BackupSupplier& s : p.suppliers) {
        if (s.capacity < 0 || s.activationCost < 0 || s.unitCost < 0) throw std::invalid_argument("supplier '" + s.name + "' has a negative value");
    }
}

long long selectionCost(const SelectionProblem& p, const std::vector<int>& chosen, int* unservedOut) {
    validateSelectionProblem(p);
    std::vector<bool> available(p.suppliers.size(), false);
    long long activation = 0;
    for (int index : chosen) {
        if (index < 0 || index >= static_cast<int>(p.suppliers.size()) || available[index]) throw std::invalid_argument("invalid or repeated supplier index");
        available[index] = true;
        activation += p.suppliers[index].activationCost;
    }
    return activation + fillCost(p, orderByUnitCost(p), available, unservedOut);
}

SelectionResult solveSelectionBacktracking(const SelectionProblem& p, bool usePruning) {
    validateSelectionProblem(p);
    const int n = static_cast<int>(p.suppliers.size());
    const std::vector<int> byUnitCost = orderByUnitCost(p);

    // Branch on suppliers with the cheapest activation per unit of capacity first: good solutions are found
    // early, so the bound prunes more.
    std::vector<int> branchOrder(n);
    std::iota(branchOrder.begin(), branchOrder.end(), 0);
    std::stable_sort(branchOrder.begin(), branchOrder.end(), [&](int a, int b) {
        const BackupSupplier &x = p.suppliers[a], &y = p.suppliers[b];
        return static_cast<long long>(x.activationCost) * std::max(1, y.capacity) < static_cast<long long>(y.activationCost) * std::max(1, x.capacity);
    });

    SelectionResult result;
    result.fullTreeNodes = (1LL << (n + 1)) - 1;
    result.totalCost = -1;                                              // -1 = no incumbent yet
    std::vector<bool> chosen(n, false);
    std::vector<bool> usable(n, true);                                  // chosen or still undecided

    auto recurse = [&](auto&& self, int depth, long long activation) -> void {
        ++result.nodesVisited;
        if (depth == n) {
            const long long total = activation + fillCost(p, byUnitCost, chosen, nullptr);
            if (result.totalCost < 0 || total < result.totalCost) {
                result.totalCost = total;
                result.chosen.clear();
                for (int i = 0; i < n; ++i) if (chosen[i]) result.chosen.push_back(i);
            }
            return;
        }
        if (usePruning && result.totalCost >= 0) {
            // usable[] currently marks chosen + undecided suppliers, i.e. the best case for covering the deficit.
            const long long bound = activation + fillCost(p, byUnitCost, usable, nullptr);
            if (bound >= result.totalCost) return;
        }
        const int index = branchOrder[depth];
        chosen[index] = true;                                           // branch 1: activate this supplier
        self(self, depth + 1, activation + p.suppliers[index].activationCost);
        chosen[index] = false;                                          // branch 2: leave it out
        usable[index] = false;
        self(self, depth + 1, activation);
        usable[index] = true;                                           // undo before returning to the caller
    };
    recurse(recurse, 0, 0);

    std::sort(result.chosen.begin(), result.chosen.end());
    int unserved = 0;
    selectionCost(p, result.chosen, &unserved);
    result.unserved = unserved;
    return result;
}

SelectionResult solveSelectionGreedy(const SelectionProblem& p) {
    validateSelectionProblem(p);
    const int n = static_cast<int>(p.suppliers.size());
    SelectionResult result;
    result.fullTreeNodes = (1LL << (n + 1)) - 1;
    std::vector<int> chosen;
    long long current = selectionCost(p, chosen);
    while (true) {
        int bestIndex = -1;
        long long bestCost = current;
        for (int i = 0; i < n; ++i) {
            if (std::find(chosen.begin(), chosen.end(), i) != chosen.end()) continue;
            std::vector<int> trial = chosen;
            trial.push_back(i);
            ++result.nodesVisited;
            const long long cost = selectionCost(p, trial);
            if (cost < bestCost) { bestCost = cost; bestIndex = i; }
        }
        if (bestIndex < 0) break;
        chosen.push_back(bestIndex);
        current = bestCost;
    }
    std::sort(chosen.begin(), chosen.end());
    result.chosen = chosen;
    result.totalCost = current;
    selectionCost(p, chosen, &result.unserved);
    return result;
}

bool canCoverWithinBudget(const std::vector<int>& capacities, const std::vector<int>& activationCosts, int deficit, long long budget) {
    if (capacities.size() != activationCosts.size()) throw std::invalid_argument("capacities and costs must have the same length");
    SelectionProblem problem;
    for (size_t i = 0; i < capacities.size(); ++i) problem.suppliers.push_back({"s" + std::to_string(i), capacities[i], activationCosts[i], 0});
    problem.deficit = deficit;
    problem.penaltyPerUnit = 1000000000LL;              // so large that leaving any unit unserved never wins
    const SelectionResult best = solveSelectionBacktracking(problem);
    return best.unserved == 0 && best.totalCost <= budget;
}

}  // namespace scx
