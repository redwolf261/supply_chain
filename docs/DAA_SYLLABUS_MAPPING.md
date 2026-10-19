# DAA Syllabus Mapping

Where each topic of the course appears in SupplyChainX, what it is used for, and how it is verified. Only implemented items are listed. Complexities are the ones documented in `ALGORITHMS.md`.

| Topic | Implementation | Used for | Verified by |
|---|---|---|---|
| **Graph representation** (adjacency lists) | `graph/SupplyChainGraph` | the supply-chain network, O(V + E) space | `test_graph` |
| **BFS** | `algorithms/GraphTraversal` | reachability, failure impact | `test_traversal_dijkstra`, API and UI |
| **DFS** (iterative) | `algorithms/GraphTraversal` | downstream region, finish order | `test_traversal_dijkstra` |
| **Greedy: Dijkstra** | `algorithms/Dijkstra` | cheapest per-unit route; shortest-path dispatch baseline | hand-checked paths, disabled routes, unreachable nodes |
| **Greedy: heuristics** | `algorithms/RecoveryHeuristics` | recovery action selection (greedy, bottleneck-aware) | proven suboptimal on a constructed case; invariants on 40 random networks |
| **Network flow: Edmonds-Karp, max-flow min-cut** | `algorithms/FlowNetwork`, `SupplyChainFlow` | deliverable capacity, bottleneck routes | textbook network (23), reverse-arc case, cut capacity equals flow |
| **Minimum-cost flow** (potentials, Bellman-Ford start) | `algorithms/FlowNetwork`, `SupplyChainFlow` | cheapest complete shipment plan | hand-computed optima; exhaustive enumeration on 25 random networks |
| **Dynamic programming** | `advanced/InventoryDP` | multi-period ordering | worked example by hand; equals brute force on 80 random problems |
| **Backtracking** (with bound-based pruning) | `advanced/SupplierSelection` | exact supplier subset selection | equals all-subsets oracle; pruning never changes the optimum |
| **Exhaustive search / brute force** | `optimization/RecoveryPlan` (`exact`), inventory brute force | exact reference for tiny instances | used as the oracle for the heuristics |
| **Divide and conquer: merge sort** (Master Theorem) | `advanced/MergeSort.h` | ordering shipment records | equals `std::stable_sort`; comparison and depth bounds |
| **Topological sort, cycle detection** | `advanced/TopologicalSort` | manufacturing dependencies; rejects cyclic networks | Floyd-Warshall oracle on 400 random graphs |
| **Network flow over time** (time-expanded network, rolling horizon) | `multiday/DispatchPlanner`, `multiday/MultiDaySimulator` | daily dispatch with goods in transit and backorders over 5-30 days | hand-calculated 5-day chain (every day's numbers); conservation of goods every day |
| **Greedy vs look-ahead vs exhaustive** (online decisions) | `multiday/MultiDayPolicies` | which recovery actions to buy on each day | clairvoyant lower bound never beaten; experiments M1-M10 |
| **NP-completeness** | `docs/NP_COMPLETENESS.md` | proof that supplier activation is (weakly) NP-complete | proof; reduction checked on 150 + 60 random instances |
| **Complexity analysis** | per algorithm in `ALGORITHMS.md`; measured runtime and heap in the API | | measured values come from the engine |
| **Algorithm design paradigms compared** | Comparison page: exact vs greedy vs baseline on identical scenarios | | strategy matrix test over 8 scenarios |

## Paradigm summary

* **Greedy** (locally best choice): Dijkstra is provably optimal for non-negative costs; the recovery heuristics are not (see the constructed counterexample).
* **Dynamic programming** (optimal substructure + overlapping subproblems): inventory planning; min-cost flow's successive shortest paths is a related incremental scheme.
* **Divide and conquer** (split, solve, combine): merge sort.
* **Backtracking / branch and bound** (systematic search with pruning): supplier selection; the bound is the branch-and-bound idea.
* **Network flow** (reductions to a standard problem): shipments and shortage reduced to min-cost flow with delivery arcs.
* **Hardness**: why choosing which suppliers to activate is not expected to have a polynomial exact algorithm.
