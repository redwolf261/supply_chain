#pragma once
// BACKTRACKING: exact supplier selection for a small instance, compared with a greedy heuristic.
//
// Problem: a deficit of D units must be covered by activating a subset of backup suppliers.
//   supplier i: capacity c_i, one-off activation cost a_i, cost per unit u_i
//   every unit left unserved costs `penaltyPerUnit`
// For a chosen subset S the cheapest way to serve is to buy units from the cheapest suppliers first, and only
// units that cost less than the penalty (anything dearer is cheaper to leave unserved). So
//   cost(S) = sum of a_i over S  +  cost of the units bought  +  penaltyPerUnit * unserved.
// We want the subset with the least cost(S).
//
// Backtracking: decide suppliers one at a time (include / exclude), a binary tree of 2^n leaves. Pruning:
//   lower bound at a node = activation cost already committed
//                           + cost of covering the deficit if ALL still-undecided suppliers were free to use.
// Any completion of the node only adds activation costs and can only use a subset of those suppliers, so no
// completion beats this bound; if the bound is not better than the best known solution the whole subtree is cut.
// Worst case is still exponential (O(2^n) nodes, each O(n)); pruning helps in practice, not in theory.
// See docs/NP_COMPLETENESS.md for why no polynomial algorithm is expected.

#include <string>
#include <vector>

namespace scx {

struct BackupSupplier {
    std::string name;
    int capacity = 0;
    int activationCost = 0;
    int unitCost = 0;
};

struct SelectionProblem {
    std::vector<BackupSupplier> suppliers;
    int deficit = 0;                    // units that must be covered
    long long penaltyPerUnit = 0;       // cost of each unit left unserved
};

struct SelectionResult {
    std::vector<int> chosen;            // indices into problem.suppliers, ascending
    long long totalCost = 0;
    int unserved = 0;
    long long nodesVisited = 0;         // backtracking tree nodes explored (shows what pruning saved)
    long long fullTreeNodes = 0;        // 2^(n+1) - 1, the size of the unpruned tree
};

// Throws std::invalid_argument for negative values or more than 24 suppliers (exponential search).
void validateSelectionProblem(const SelectionProblem& problem);

// Cost of activating exactly `chosen` (indices); the reference definition used by every solver and test.
long long selectionCost(const SelectionProblem& problem, const std::vector<int>& chosen, int* unservedOut = nullptr);

SelectionResult solveSelectionBacktracking(const SelectionProblem& problem, bool usePruning = true);

// Best-improvement greedy: repeatedly add the single supplier that lowers cost(S) the most; stop when none does.
// Fast, but myopic (it can stop in a local optimum): NOT guaranteed optimal.
SelectionResult solveSelectionGreedy(const SelectionProblem& problem);

// Decision form used in the NP-completeness argument: can the deficit be covered completely
// (no unserved units) with total activation cost <= budget? Unit costs are taken as zero.
bool canCoverWithinBudget(const std::vector<int>& capacities, const std::vector<int>& activationCosts, int deficit, long long budget);

}  // namespace scx
