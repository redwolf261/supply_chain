# NP-Completeness of Supplier Activation

This document states one restricted decision problem, proves it NP-complete with a polynomial-time reduction from a known NP-complete problem, explains exactly what that does and does not imply for SupplyChainX, and says which parts are tested rather than proved.

## 1. The problem

**SUPPLIER-ACTIVATION** (decision version)

* *Instance:* positive integers `c_1..c_n` (capacities of backup suppliers), `a_1..a_n` (their activation costs), a demand `D` and a budget `B`.
* *Question:* is there a set `S ⊆ {1..n}` with `Σ_{i∈S} c_i ≥ D` and `Σ_{i∈S} a_i ≤ B`?

It is the restricted case of recovery planning with one customer, one direct route per backup supplier (capacity `c_i`), zero unit and transport costs, and no shortage allowed.

## 2. Theorem

SUPPLIER-ACTIVATION is NP-complete (in the weak sense).

### Membership in NP
A certificate is the set `S`. Checking the two sums takes `O(n)` additions of the given numbers, which is polynomial in the input size.

### NP-hardness: reduction from SUBSET-SUM
SUBSET-SUM: given positive integers `v_1..v_n` and a target `T`, is there a subset whose sum is exactly `T`? This is NP-complete (Karp 1972; Garey and Johnson, problem SP13).

*Construction (linear time):* set `c_i = a_i = v_i` for every `i`, `D = T`, `B = T`.

*Correctness:*
* (⇒) If a subset `S` has `Σ v_i = T`, then `Σ c_i = T ≥ D` and `Σ a_i = T ≤ B`, so the constructed instance is a yes-instance.
* (⇐) If some `S` satisfies `Σ c_i ≥ D = T` and `Σ a_i ≤ B = T`, then since `a_i = c_i`: `T ≤ Σ c_i = Σ a_i ≤ T`. Hence `Σ v_i = T` exactly, so `S` solves SUBSET-SUM.

The map is computable in polynomial time and preserves yes/no answers, so SUBSET-SUM ≤ₚ SUPPLIER-ACTIVATION. Together with membership, SUPPLIER-ACTIVATION is NP-complete. ∎

## 3. What this does and does not mean

* **Weak NP-completeness only.** Numbers are written in binary, and SUBSET-SUM is solvable in pseudo-polynomial time `O(n·T)` by dynamic programming; the same holds for this problem (a knapsack-style table over the amount covered). That algorithm is *not implemented* in SupplyChainX. So the hardness is about large capacities and costs, not about the number of suppliers alone. We make no claim of strong NP-hardness.
* **Consequence for the full recovery problem.** Define RECOVERY-BUDGET: given a network, a set of recovery actions with costs, and a budget `B`, is there a set of actions with total cost ≤ `B` after which min-cost/max flow serves all demand? It is in NP (certificate: the action set; checking cost and running max flow are polynomial). SUPPLIER-ACTIVATION is the special case above (customer with demand `D`; one backup per item with supply `c_i`, activation cost `a_i`, and a route of capacity `c_i` to the customer; all unit and route costs zero; max flow reaches `D` exactly when `Σ c_i ≥ D` over the activated set). Therefore RECOVERY-BUDGET is NP-complete too, and its optimisation version (minimum total cost) is NP-hard.
* **Why the project is built the way it is.** Min-cost flow solves the flow part exactly *for a fixed set of activations* in polynomial time. Choosing the activations is the hard part, so the greedy and bottleneck-aware strategies are heuristics (not optimal, see `test_recovery`), and the exact reference solver (`exact`, and backtracking for supplier selection) is exponential in the number of candidate actions.
* **No hardness claim for the experimental heuristic.** Nothing here says the bottleneck-aware heuristic is good or bad; that is for the experiments.

## 4. What is tested (evidence, not proof)

The proof above is the argument. The tests check that the implementation agrees with it:

* `testSubsetSumReductionAgreesWithBruteForce`: for 150 seeded random SUBSET-SUM instances, the backtracking solver's yes/no answer to the constructed instance equals a brute-force subset-sum answer (both outcomes occur often in the sample).
* `testSubsetSumReductionThroughTheRecoveryEngine`: for 60 seeded random instances the reduction is built as a *real* supply-chain network (customer demand `D`, shortage penalty `D + 1`, one backup supplier per item, free routes) and solved by the exhaustive recovery solver. Its optimum total cost is ≤ `D` exactly when a subset sums to `D`, and every "yes" plan serves all demand.

The shortage penalty `D + 1` makes the optimisation version encode the decision version: any plan with unserved units costs at least `D + 1 > B`.
