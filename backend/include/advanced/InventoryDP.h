#pragma once
// DYNAMIC PROGRAMMING: restricted multi-period inventory planning for ONE product at ONE location.
//
// This is deliberately a small, exactly solvable model; it does not claim to solve the full multi-product
// supply chain. Each day t = 0..T-1:
//   1. we may order q units (0 <= q <= maxOrder); they arrive immediately,
//      cost = setupCost (only if q > 0) + unitCost[t] * q,
//      and stock after ordering may not exceed maxInventory (warehouse capacity);
//   2. demand[t] is served from stock: sold = min(stock, demand[t]);
//      the rest is LOST (no backlog), costing shortagePenalty per unit;
//   3. leftover stock is carried to the next day and costs holdingCost per unit.
//
// State  : (day t, stock at the start of the day i)
// Value  : costToGo[t][i] = cheapest total cost of days t..T-1 starting with stock i
// Base   : costToGo[T][i] = 0                                   (nothing left to pay for)
// Recur. : costToGo[t][i] = min over q of
//            order(t,q) + holdingCost * end + shortagePenalty * lost + costToGo[t+1][end]
//          where stock = i + q, sold = min(stock, demand[t]), lost = demand[t] - sold, end = stock - sold.
//
// Why DP applies: the future depends on the past only through the stock level (optimal substructure),
// and many different histories lead to the same (t, i) (overlapping subproblems), so each state is solved once.
// Time O(T * C * Q) for capacity C and maximum order Q; space O(T * C) for the table and the decisions.

#include <vector>

namespace scx {

struct InventoryProblem {
    std::vector<int> demand;           // units demanded each day (size T)
    std::vector<int> unitCost;         // purchase cost per unit each day (size T)
    int setupCost = 0;                 // paid on every day with an order > 0
    int holdingCost = 0;               // per unit of stock carried overnight
    int shortagePenalty = 0;           // per unit of lost demand
    int maxInventory = 0;              // warehouse capacity C
    int maxOrder = 0;                  // largest single order Q
    int initialInventory = 0;
};

struct InventoryPlan {
    long long totalCost = 0;
    std::vector<int> order;            // units ordered each day
    std::vector<int> endInventory;     // stock after serving demand each day
    std::vector<int> lost;             // unmet demand each day
    std::vector<std::vector<long long>> costToGo;   // (T+1) x (C+1) DP table, kept for teaching and tests
};

// Throws std::invalid_argument for inconsistent sizes, negative values, or initialInventory > maxInventory.
void validateInventoryProblem(const InventoryProblem& problem);

// Backward tabulation. Optimal for the model above.
InventoryPlan solveInventoryDP(const InventoryProblem& problem);

// Cost of following a given order sequence (throws std::invalid_argument if it breaks maxOrder / maxInventory).
// Used to check plans independently of the solver.
long long evaluateInventoryPolicy(const InventoryProblem& problem, const std::vector<int>& orders);

// Reference solver: tries every order sequence ((maxOrder+1)^T of them). Throws std::invalid_argument
// if that is more than about two million sequences.
InventoryPlan solveInventoryBruteForce(const InventoryProblem& problem);

// Naive policy for comparison: each day order exactly what is needed to serve that day's demand
// (as far as the limits allow). No planning ahead, so it pays the setup cost every day.
std::vector<int> lotForLotOrders(const InventoryProblem& problem);

}  // namespace scx
