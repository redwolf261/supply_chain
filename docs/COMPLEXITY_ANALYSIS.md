# Complexity Analysis

All bounds refer to the implementations in this repository. Notation: V nodes and E edges of the supply-chain graph (the flow network adds 2 nodes and at most 2V + C arcs, which does not change any bound); C customers; D total demand (units); F units of flow sent; k candidate recovery actions (inactive backup suppliers plus inactive routes); n items in a backtracking or sorting problem; T days, C_inv warehouse capacity, Q maximum order in the inventory DP.

## 1. Summary table

| Algorithm | Function | Time | Space | Exact? |
|---|---|---|---|---|
| BFS | `breadthFirstSearch` | O(V + E) | O(V) | yes |
| DFS (iterative) | `depthFirstSearch` | O(V + E) | O(V) | yes |
| Dijkstra (binary heap, lazy deletion) | `dijkstra` | O((V + E) log V) | O(V + E) heap entries worst case | yes, costs ≥ 0 |
| Edmonds-Karp max flow + min cut | `FlowNetwork::edmondsKarp`, `computeMaxFlow` | O(V E²) | O(V + E) | yes |
| Min-cost flow (successive shortest paths, potentials) | `FlowNetwork::minCostFlow`, `solveMinCostFlow` | O(V E + F (V + E) log V) | O(V + E) | yes, for a fixed set of active elements |
| Shortest-path dispatch | `dispatchByShortestPaths` | O((D + C)(V + E) log V) | O(V + E) | no (baseline) |
| Greedy recovery | `greedyRecovery` | O(k²) min-cost-flow solves | O(V + E) per trial copy | no |
| Bottleneck-aware recovery | `bottleneckAwareRecovery` | O(k²) solves + O(E) per candidate | O(V + E) per trial copy | no |
| Exhaustive recovery search | `solveRecoveryByExhaustiveSearch` | 2^k min-cost-flow solves | O(V + E) | yes |
| Plan validation / cost recomputation | `validateFlowSolution`, `computeCost` | O(V + E) | O(V) | (checker) |
| Inventory DP | `solveInventoryDP` | O(T · C_inv · Q) | O(T · C_inv) | yes, for its model |
| Backtracking supplier selection | `solveSelectionBacktracking` | O(2^n · n) worst case | O(n) | yes |
| Greedy supplier selection | `solveSelectionGreedy` | O(n³ log n) (n rounds × n candidates × O(n log n) per cost evaluation, which re-sorts suppliers) | O(n) | no |
| Merge sort | `mergeSort` | Θ(n log n) | O(n) buffer, O(log n) depth | yes |
| Multi-day dispatch plan (one day) | `planDispatch` | one min-cost flow on about V·W nodes and (E + 2V)·W arcs | O((V + E) W) | yes, for the window and the information given |
| Multi-day run, `none` | `MultiDaySimulator::run` | H plans + O(H · pipeline) bookkeeping | O((V + E) W + pipeline) | (simulation) |
| Multi-day greedy / bottleneck-aware decision (one day) | `ImmediateBenefitPolicy::decide` | O(k²) short plans + one Dijkstra per candidate | as a plan | no |
| Multi-day look-ahead decision (one day) | `LookaheadPolicy::decide` | O(k²) simulations of h days, i.e. O(k² · h) plans | a copy of the state | no |
| Multi-day rolling exact decision (one day) | `RollingExactPolicy::decide` | 2^k plans | as a plan | exact over one day's choices only |
| Clairvoyant lower bound | `computeClairvoyantBound` | 2^k full-horizon plans (W = H) | O((V + E) H) | a lower bound |
| Supply lead (minimum planning window) | `supplyLeadDays` | one Dijkstra per supplier, O(S (V + E) log V) | O(V + E) | yes |
| Topological sort (Kahn) + cycle extraction | `topologicalSort` | O(V log V + E) (a min-heap of free nodes for a deterministic order; O(V + E) with a plain queue) | O(V + E) | yes |

## 2. Derivations

**BFS / DFS.** Each node is marked once and each outgoing edge is examined once when its tail is processed, so the work is Θ(V + E). The DFS keeps an explicit stack of (node, next-edge index) pairs, at most V deep.

**Dijkstra.** Every successful relaxation pushes one heap entry, and an edge relaxes at most once per time its tail is settled, so at most E + 1 entries are pushed; each push and pop costs O(log E) = O(log V). Stale entries are skipped on pop. Total O((V + E) log V).

**Edmonds-Karp.** Each augmentation is a BFS, O(E). Because paths are shortest in number of edges, the distance from the source to any node never decreases, and every augmentation saturates at least one *critical* edge whose distance must grow by 2 before it can be critical again; distances are below V, so each edge is critical O(V) times and there are O(V E) augmentations. Total O(V E²). The minimum cut is one more BFS on the final residual graph.

**Minimum-cost flow.** Bellman-Ford for the initial potentials runs at most V passes over the arcs, O(V E), and exits early when nothing changes. Each later round is one Dijkstra on reduced costs, O((V + E) log V), and pushes at least one unit, so there are at most F rounds. F is bounded by the total demand D, so the bound is pseudo-polynomial (it depends on the size of the numbers, not only on V and E). The stopping rule (stop at the first path of non-negative cost) can only reduce the number of rounds.

**Dispatch.** Every successful Dijkstra ships at least one unit (D in total), and each customer has at most one unsuccessful Dijkstra that ends its loop (C in total).

**Greedy and bottleneck-aware recovery.** Each round trial-applies each of at most k candidates to a copy of the graph (O(V + E)) and solves min-cost flow on it, then applies the best one. Every accepted action removes a candidate, so there are at most k rounds: at most k(k + 1)/2 + k solves, i.e. O(k²) min-cost-flow solves. The bottleneck score adds an O(E) pass over the plan per candidate. The actual work depends on how many rounds happen, which explains the irregular runtimes in section 3.

**Exhaustive recovery search.** Every subset of the k candidates (2^k) is applied to a copy and solved once. It is therefore limited to k ≤ 16 in the API and k ≤ 10 in the experiments.

**Multi-day planner and policies.** The time-expanded network has one copy of each node per day of the window (V·W + 2 nodes) and, per day, one arc per route plus a hold and a source arc per node, about (E + 2V)·W arcs, so one plan costs one min-cost flow of that size. A simulated day costs one plan for every policy plus the policy's decision: greedy and bottleneck-aware use the same O(k²) round structure as in the single-period model, but with short windows (lead + delivery delay + 1 days); look-ahead simulates h days for each candidate in each round (O(k² · h) plans), and since the window inside each simulated day also grows with h, its measured time grows about as h^1.9 (M8: 3 ms at h = 1 to 319 ms at h = 12 per 30-day run); rolling exact and the clairvoyant bound are 2^k plans, contained by the limits in section 4. Measured decision time per 30-day run of the base multi-day scenario (19 nodes): greedy about 50 ms, rolling exact about 72 ms, look-ahead(5) about 97 ms (`experiments/results/multiday/analysis.txt`).

**Inventory DP.** (T + 1)(C_inv + 1) states; each tries at most Q + 1 orders with O(1) work per order. The table and the stored decisions take O(T · C_inv). This is pseudo-polynomial in the capacity.

**Backtracking.** The decision tree has 2^(n+1) − 1 nodes; each node computes a bound in O(n) (one pass over suppliers sorted once by unit cost). Pruning never worsens the worst case and helps on average (measured in `test_supplier_selection`).

**Merge sort.** T(n) = 2T(n/2) + Θ(n). With a = 2, b = 2 and f(n) = Θ(n) = Θ(n^(log_b a)), Master Theorem case 2 gives T(n) = Θ(n log n). Tested bound: at most n·⌈log₂ n⌉ comparisons, recursion depth at most ⌈log₂ n⌉, exactly n − 1 merges.

**Topological sort.** Each node enters the free set once and each edge decrements a counter once: O(V + E) work, plus O(log V) per heap operation (one push and one pop per node) for the deterministic smallest-id-first order, so O(V log V + E) overall. The cycle extraction is one DFS over the blocked nodes, O(V + E).

## 3. Measured growth (experiment A)

Mean time per scenario on networks of 27 to 216 nodes (5 runs per size, median of 3 timings, disruption severity 0.3), from `experiments/results/A_summary.csv`:

| Strategy | 27 nodes | 216 nodes | empirical exponent (log-log slope) |
|---|---|---|---|
| Dijkstra dispatch | 0.06 ms | 3.26 ms | n^1.95 |
| Min-cost flow | 0.11 ms | 8.98 ms | n^2.10 |
| Greedy | 1.49 ms | 911 ms | n^3.05 |
| Bottleneck-aware | 1.24 ms | 924 ms | n^3.13 |

Peak heap grows about linearly (greedy 24 KB → 217 KB, n^1.03; min-cost flow 13 KB → 159 KB, n^1.18).

**Reading the numbers against the bounds.** In these generated networks E grows roughly in proportion to V, and so do F (demand grows with the number of customers) and k (candidate actions grew from 5 to 44 on average). Then min-cost flow's O(V E + F (V + E) log V) behaves like about n² (measured n^2.10), and greedy's O(k²) solves of that cost would suggest up to about n⁴; the measured n^3.05 is lower because greedy takes only 0.4 to 2.6 actions on average, so the number of rounds stays small and the cost is closer to k × (one solve), about n³. The exponents come from six sizes on one machine and are indicative, not a precise fit (`EXPERIMENTAL_EVALUATION.md`, section 3.4).

## 4. Where exponential cost is accepted, and how it is contained

| Exponential part | Why it is acceptable | Guard |
|---|---|---|
| `exact` recovery (2^k) | reference solution for small instances only | `/api/algorithm/run` refuses k > 16 (HTTP 422); `/api/algorithms/compare` skips it with a reason; experiments skip k > 10 and report the skip |
| Backtracking supplier selection (2^n) | teaching module, exact answer | refuses n > 24 |
| Inventory brute force ((Q + 1)^T) | test oracle only | refuses more than about 2 million sequences |
| Multi-day `rolling_exact` (2^k plans per day) | optimization-based reference | above `maxExactCandidates` (default 8, at most 10 in the API) the run is reported as skipped |
| Clairvoyant bound (2^k full-horizon plans) | a lower bound for evaluation | not computed above 10 possible actions (reported with the reason) |

The NP-completeness result (`NP_COMPLETENESS.md`) explains why no polynomial exact algorithm is expected for choosing recovery actions; the result is weak NP-completeness, consistent with the pseudo-polynomial nature of the inputs.
