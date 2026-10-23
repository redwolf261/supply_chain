# Algorithms

Only implemented algorithms are documented here. Each section is added when the code and its tests exist.

## 1. Breadth-First Search — `breadthFirstSearch` (`algorithms/GraphTraversal.cpp`)

* **Purpose:** which facilities can goods from a source still reach? Used for failure-impact analysis (`nodesLostBetween` compares reachability before/after a disruption).
* **Input / output:** graph + source id → visit order, `reached[]`, parent node/edge, fewest-hops distance.
* **Pseudocode**
  ```
  BFS(G, s):  if s inactive: return nothing
      mark s reached; enqueue s
      while queue not empty:
          u = dequeue; record u
          for each outgoing edge e=(u,v) that is usable:
              if v not reached: mark v reached; parent[v]=u; hops[v]=hops[u]+1; enqueue v
  ```
* **Why it is correct:** nodes leave the queue in non-decreasing hop distance (queue is FIFO and each node enqueues neighbours at distance +1), so the first time v is discovered is along a fewest-edge path. A node is marked when enqueued, so it is enqueued at most once.
* **Complexity:** O(V + E) time (each node dequeued once, each out-edge examined once), O(V) space.
* **Dry run** (test graph `Diamond`, edges 0→1, 0→2, 1→2, 1→3, 2→3, node 4 isolated, source 0): queue [0] → visit 0, enqueue 1,2 → visit 1, enqueue 3 → visit 2 (3 already marked) → visit 3. Order 0,1,2,3; hops 0,1,1,2; node 4 unreached.
* **Limitations:** ignores costs and capacities; "reachable" does not mean "can carry the demand".

## 2. Depth-First Search — `depthFirstSearch`

* **Purpose:** explore everything downstream of a node; the finish (post-)order is reused later for topological sorting.
* **Implementation:** iterative, with an explicit stack of (node, next-edge-index) frames, equivalent to recursion but safe on deep networks.
* **Semantics:** follows edge direction only. Starting from a customer with no outgoing edges reaches only that customer (tested), i.e. it is *directed reachability*, not undirected connectivity.
* **Dry run** (same graph): visit 0 → 0→1 visit 1 → 1→2 visit 2 → 2→3 visit 3 → 3 has no out-edges, finish 3 → back to 2 finish → 1 (edge 1→3 already visited) finish → 0 finish. Visit 0,1,2,3; finish 3,2,1,0.
* **Complexity:** O(V + E) time, O(V) space (stack depth ≤ V).

## 3. Dijkstra — `dijkstra` (`algorithms/Dijkstra.cpp`)

* **Purpose:** cheapest per-unit route from a source to every facility.
* **Relaxation:** if `dist[u] + cost(u,v) < dist[v]`, we found a cheaper way to v through u; update `dist[v]`, `parent[v]` and push (dist, v) on a min-heap. Stale heap entries (larger than the current `dist`) are skipped.
* **Correctness (sketch):** with non-negative costs, the node popped with the smallest tentative distance cannot be improved later, because any other route to it must pass through a node with distance at least as large and then add a non-negative cost. Costs are validated non-negative in `addEdge`.
* **Complexity:** O((V + E) log V) time with a binary heap, O(V) space.
* **Dry run** (`Diamond`, cost 0→1=2, 0→2=5, 1→2=1, 1→3=7, 2→3=2): dist[1]=2, dist[2]=5 → pop 1: dist[2] improves to 3, dist[3]=9 → pop 2 (3): dist[3] improves to 5 → pop 3. Path 0-1-2-3, cost 5.
* **Limitation (important for the project):** Dijkstra routes one unit and ignores capacity and other shipments, so it cannot split flow or respect shared bottlenecks. That is why network-wide allocation needs min-cost flow.

## 5. Edmonds-Karp maximum flow — `FlowNetwork::edmondsKarp`, `computeMaxFlow`

* **Purpose:** the most goods the (disrupted) network can possibly deliver, ignoring cost, and which routes are the bottleneck.
* **Ford-Fulkerson idea:** repeatedly find a source-to-sink path with spare capacity (an *augmenting path*), push the bottleneck amount along it, repeat until none exists. **Edmonds-Karp** is Ford-Fulkerson where each path is found by BFS (fewest edges). That bounds the number of augmentations by O(VE), giving O(V·E²) total; Ford-Fulkerson with arbitrary path choice can be pseudo-polynomial.
* **Residual graph:** each arc u→v (capacity c) has a paired reverse arc v→u. Pushing f units lowers u→v's residual by f and raises v→u's by f, so later paths can "undo" earlier flow. Arcs are stored in pairs (`arc ^ 1` is the reverse).
* **Supply-chain mapping:** super-source → every active supplier (cap = supply) and stocked node (cap = inventory); each customer → super-sink (cap = demand); real edges keep their *available* capacity (failed or zero-capacity edges are omitted).
* **Min cut:** after the last augmentation, BFS in the residual graph. Edges from reachable to unreachable nodes are saturated and form a minimum cut (max-flow min-cut theorem). `minCutEdgeIds` are the route bottlenecks; `supplyLimitedNodeIds` / `demandLimitedNodeIds` say when supply or demand, not routes, is the limit. Tests check cut capacity == flow value.
* **Dry run:** S(supply 10) → W1 (cap 4) and W2 (cap 3), both → C (cap 5 each). Path S-W1-C pushes 4, S-W2-C pushes 3, no path remains: flow 7. Reachable set stops at S, so the cut is S→W1, S→W2 (4+3 = 7).
* **Reverse-arc dry run** (`testReverseResidualArcIsUsed`): BFS first picks s-a-b-t, using a→b. The second path s-x-b-(reverse b→a)-a-y-t cancels a→b. Total 2; without reverse arcs only 1.
* **Complexity:** O(V·E²) time, O(V + E) space. **Limitation:** ignores cost, so max flow is not the best plan.

## 6. Minimum-cost flow — `FlowNetwork::minCostFlow`, `solveMinCostFlow`

* **Purpose:** the cheapest complete shipment plan (purchase + transport + holding + shortage penalty), per `MATHEMATICAL_MODEL.md` §5 and §7.
* **Method:** successive shortest augmenting paths. Each round finds the cheapest residual path σ→τ and saturates it. Residual reverse arcs have NEGATIVE cost, so plain Dijkstra would be wrong. We keep node **potentials** π and run Dijkstra on *reduced costs* c(u,v)+π(u)−π(v) ≥ 0, updating π with the distances found. Initial π comes from Bellman-Ford (needed because the −h and −p arcs are negative).
* **Stopping rule:** path costs are non-decreasing from round to round (convexity of min-cost flow), so we stop at the first path of cost ≥ 0: another delivered unit would cost more than the penalty it avoids (`testLowPenaltyMeansNoShipment`).
* **Correctness:** a flow is cost-optimal iff its residual graph has no negative-cost cycle, and augmenting along shortest paths never creates one. Empirically, tests (a) match hand-computed optima, (b) match exhaustive enumeration of every integer plan on 25 seeded random networks, and (c) pass every plan through the independent `validateFlowSolution` / `computeCost`. Removing the stop rule (a deliberate mutation) made the tests fail, then it was reverted.
* **Complexity:** one-time O(VE) Bellman-Ford, then each round O(E log V); rounds ≤ total flow F, so O(VE + F·E log V). Space O(V + E).
* **Limitations:** optimal only for the *given* active set of suppliers and routes. Choosing which backups to activate (fixed charges) is not plain min-cost flow and is handled by the recovery heuristics (Phase 4). Single commodity, integer quantities.

## 4. Greedy recovery — `greedyRecovery` (`algorithms/RecoveryHeuristics.cpp`)

* **Purpose:** a fast baseline that decides which recovery actions to take (activate a backup supplier, reopen a failed route) after a disruption.
* **Candidates:** every inactive backup supplier and every inactive edge in the current state. Each action has a one-off cost and may be taken once.
* **Selection criterion:** `S(a) = shortageReduction(a) / (cost(a) + ε)`, units of unmet demand removed per currency spent. `shortageReduction` is measured exactly: copy the current state, apply the action, re-solve min-cost flow, compare unmet demand.
* **Eligibility (shared with Algorithm 7):** the action must reduce unmet demand AND its flow-objective saving must exceed its own cost. So no accepted step can raise total cost, and a plan is never worse than doing nothing (tested).
* **Loop:** score all candidates, apply the best to the planning state, re-score everything, stop when nothing is eligible. Never invents inventory or exceeds capacity because all shipments come from min-cost flow on the repaired network.
* **Pseudocode**
  ```
  state = disrupted network
  repeat:
      best = none
      for each legal action a:  trial = copy(state) + a;  solve min-cost flow on trial
          if unmet decreases and costGain > cost(a): score a;  keep the best
      if best == none: stop
      apply best to state
  ```
* **Is it optimal? No.** Test `testGreedyIsNotAlwaysOptimal`: demand 10, penalty 10/unit; backup B1 (6 units, activation 6) has ratio 1.0, backup B2 (10 units, activation 30) ratio 0.33. Greedy takes B1 then still needs B2: cost 56. The optimum is B2 alone: 50. The exact reference solver (`solveRecoveryByExhaustiveSearch`, all 2^k subsets) finds 50.
* **Complexity:** each round solves one min-cost flow per candidate: O(k²) flow solves for k candidate actions worst case (k rounds × k candidates), each costing the min-cost-flow bound of Algorithm 6. Space: a graph copy per trial.
* **Limitations:** myopic. It cannot see that two individually useless actions (e.g. a backup whose only route is also closed) are valuable together. The pay-off test uses flow savings at the current state only.

## 7. Bottleneck-aware recovery (EXPERIMENTAL) — `bottleneckAwareRecovery`

* **Hypothesis under test:** accounting for downstream capacity bottlenecks improves recovery compared with immediate-benefit greedy. Not assumed true.
* **Score:** `S(a) = L(a) / (cost(a) + λ·R(a) + ε)`.
  * `L(a) = Σ_j penalty_j · (unmet_j before − unmet_j after)`, the avoided shortage loss in currency.
  * `f(a)` = share of shipped units that ride on edges with utilisation ≥ θ (default 0.9) in the repaired optimal plan; in [0, 1].
  * `R(a) = f(a)·L(a)`, the part of the avoided loss exposed to saturated edges. λ ≥ 0 is configurable; ε avoids division by zero.
* **Ablation:** λ = 0 removes the risk term (algorithm name `bottleneck_aware_lambda0`). Note that λ = 0 still differs from plain greedy in the numerator (penalty-weighted instead of unit count), so greedy vs λ=0 isolates the numerator and λ=0 vs λ>0 isolates the bottleneck term.
* **Demonstrated behaviour** (`testBottleneckTermChangesTheChoice`): X activates for 20 but its route is exactly saturated; Y costs 22 with a half-empty route. λ=0 picks X (total 40), λ=1 picks Y (total 42). On this instance the risk-aware choice is 2 more expensive; it buys slack, which only pays off if a later disruption hits. The single-period model cannot measure that benefit, so the real evaluation is Phase 9's experiments.
* **Early evidence (unit test, 40 seeded random networks with ≤ 10 candidate actions):** greedy, bottleneck-aware (λ=1) and λ=0 each matched the exact optimum in 37/40 cases. Nothing here shows the bottleneck term helps; no claim of superiority is made.
* **Measured evidence (Phase 9, `EXPERIMENTAL_EVALUATION.md`):** over 26 sweep settings (30 paired runs each), a 200-run repeated experiment and a 100-run ablation over λ ∈ {0, 0.25, 0.5, 1, 2, 5}, **no paired cost difference to greedy or to λ = 0 had a 95% interval excluding zero**, and 88% of the λ > 0 plans cost exactly the same as the λ = 0 plan. The hypothesis that the bottleneck-risk term improves recovery is **not supported** on this scenario family (it is not refuted in general: the single-period model cannot reward the slack the term buys).
* **Complexity:** same as greedy plus O(E) per candidate to compute f(a).

## Baseline: sequential Dijkstra dispatch — `dispatchByShortestPaths` (`algorithms/DispatchBaseline.cpp`)

* **Purpose:** what you get if you use Dijkstra alone for allocation. Customers are served in decreasing shortage-penalty order; for each, repeat: multi-source Dijkstra from every node with stock (start distance = unit purchase cost; inventory = 0), ship along the cheapest path as much as path capacity, source stock and unmet demand allow, stop when the cheapest unit costs at least the penalty.
* **Why it is not optimal:** earlier shipments are never undone (no reverse arcs). `testDispatchBaselineIsFeasibleButNotOptimal`: S1 can serve X or Y, S2 can only serve X. Dispatch gives S1 to X (the penalty-100 customer served first) and leaves Y short: cost 91; min-cost flow sends S2→X and S1→Y: cost 6.
* **Guarantee:** always a valid plan (all constraints hold, tested by the independent validator), and never cheaper than min-cost flow on the same network (min-cost flow is optimal for a fixed active set; the matrix test checks `dispatch >= min-cost flow` on all eight scenarios).
* **Complexity:** O(D (V + E) log V) for total demand D.
* On the generated scenarios it cost about 2.6% to 5.3% more than min-cost flow across the eight standard scenarios, including normal operations (seed 1, one run each: an illustration, not a benchmark).

## Recovery plan validation — `validateRecoveryPlan`

Independent of the strategy that built the plan: actions must be legal in sequence and distinct (cost counted once), reported action cost matches recomputation, the flow is valid on the repaired network (capacities, conservation, supply, inventory, unmet demand bounds), and reported flow cost and unmet demand match fresh recomputation. Tests tamper with plans (duplicated action, understated cost, fake fulfilment) and confirm rejection.

## 8. Dynamic programming: multi-period inventory — `solveInventoryDP` (`advanced/InventoryDP.cpp`)

* **Purpose:** plan how much to order each day for ONE product at ONE location so that purchase (with a setup cost per order), holding and lost-sales costs are minimal over a short horizon. A deliberately small model: it does not claim to solve the multi-product supply chain.
* **Model per day:** order `q` (0..maxOrder, stock after ordering at most `maxInventory`; cost `setup·[q>0] + unitCost·q`), then demand is served from stock, unserved demand is lost (`shortagePenalty` each), leftover stock is carried and costs `holdingCost` per unit.
* **State and value:** `costToGo[t][i]` = cheapest cost of days `t..T-1` when day `t` starts with stock `i`.
* **Base case:** `costToGo[T][i] = 0`.
* **Recurrence:** with `stock = i + q`, `sold = min(stock, demand[t])`, `lost = demand[t] - sold`, `end = stock - sold`:
  `costToGo[t][i] = min over q of ( order(t,q) + holding·end + penalty·lost + costToGo[t+1][end] )`.
* **Pseudocode**
  ```
  for t = T-1 down to 0:
    for i = 0..C:
      best = infinity
      for q = 0..min(Q, C - i):
        total = dayCost(t, i, q) + costToGo[t+1][endStock(t, i, q)]
        if total < best: best = total; choice[t][i] = q
      costToGo[t][i] = best
  follow choice[][] forward from the initial stock to read off the orders
  ```
* **Why it is correct (optimal substructure):** the cost of days `t..T-1` depends on the past only through the stock `i` at the start of day `t`, so an optimal plan for the whole horizon, restricted to days `t..T-1`, must itself be optimal for the state it starts in; otherwise replacing it with a better tail would improve the whole plan. The recurrence tries every legal order, so it considers every possible first decision. Induction on `T - t` completes the argument. Empirically it equals exhaustive search of all order sequences on 80 seeded random problems, and the reconstructed orders re-cost to the same total through an independent evaluator.
* **Worked example** (tested, and printed by `scx_advanced_demo`): demand 2, 3, 2; unit cost 2; setup 5; holding 1; lost-sale penalty 10; capacity 5; max order 5; start stock 0.
  ```
  costToGo      stock: 0    1    2    3    4    5
        day 0:        26   24   17   16   15   12
        day 1:        17   15   13    9    8    2
        day 2:         9    7    0    1    2    3
        day 3 (base):  0    0    0    0    0    0
  ```
  Day 2 by hand: with 0 on hand, order 2 (5 + 4 = 9); with 1 on hand, order 1 (5 + 2 = 7); with 2 on hand nothing is needed; with 3 on hand, 1 extra unit is held overnight (1). `costToGo[0][0] = 26`: order 2 on day 0 (9), order 5 on day 1 (5 + 10 = 15, serve 3, carry 2 for holding 2), serve day 2 from stock. Ordering exactly the daily need costs 29 (three setup costs). The cheaper plan orders ahead.
* **Complexity:** time `O(T · C · Q)`, space `O(T · C)` for the table and decisions (a value-only version needs `O(C)` per row).
* **Limitations:** one product, one location, integer units, lost sales (no backlog), deterministic demand. The state space grows with the stock capacity, so it is pseudo-polynomial.

## 9. Backtracking: exact supplier selection — `solveSelectionBacktracking` (`advanced/SupplierSelection.cpp`)

* **Purpose:** choose which backup suppliers to activate to cover a deficit `D` at minimum total cost: activation costs, plus the cheapest units bought (cheapest first, and only units cheaper than the penalty), plus the penalty on units left unserved.
* **Search:** a binary tree, one level per supplier (activate / leave out); suppliers with the cheapest activation per unit of capacity are decided first so good solutions appear early.
* **Pruning (bound):** at a node, `lower bound = activation cost already committed + cost of covering D if every still-undecided supplier were free to use`. Every completion adds activation costs and can only use a subset of those suppliers, so no completion beats the bound; if the bound is not below the best known cost the subtree is cut.
* **Pseudocode**
  ```
  search(level, committedActivation):
    if level == n: evaluate the chosen set; keep it if cheapest so far; return
    if committedActivation + coverCost(chosen + undecided) >= best: return        // prune
    activate supplier[level];  search(level+1, committedActivation + a);  undo
    skip supplier[level];      search(level+1, committedActivation)
  ```
* **Dry run** (suppliers A cap 6 act 6, B cap 6 act 6, C cap 10 act 15, deficit 10, penalty 100): the search finds {A, B} with cost 12 (covers 12 ≥ 10); {C} costs 15; {} costs 1000; {A} costs 6 + 400. The demo shows it visiting all 15 of the 15 tree nodes here: the bound ignores future activation costs, so on tiny instances it prunes nothing. Over 40 random instances (up to 11 suppliers) pruning keeps the optimum and visits fewer nodes in total (tested).
* **Greedy comparison:** best-improvement greedy adds the single supplier that lowers cost most and stops when none helps. On the instance above it adds C (15 beats 406 for A) and stops, cost 15, while the optimum is 12. It is never better than the optimum (tested on random instances).
* **Complexity:** worst case `O(2^n)` nodes, each `O(n)`; pruning improves typical runs, not the worst case. Limited to 24 suppliers by an explicit check. Why no polynomial algorithm is expected: `docs/NP_COMPLETENESS.md`.
* **Correctness evidence:** equals the minimum over all `2^n` subsets evaluated with the reference cost function on 40 seeded random instances, with and without pruning; reported subsets re-cost to the reported total.
* **Limitations:** one deficit, one pool of suppliers, no network structure. In the full network the activation decision interacts with route capacities; that case is handled by the recovery strategies, and its exact version is the exhaustive solver.

## 10. Divide and conquer: merge sort — `mergeSort` (`advanced/MergeSort.h`)

* **Purpose:** order shipment and event records (by cost, quantity or route) for reports. A supporting algorithm, not part of recovery.
* **Idea:** divide the range in half, sort each half recursively, merge the two sorted halves.
* **Recurrence:** `T(n) = 2T(n/2) + Θ(n)`. **Master Theorem:** `a = 2, b = 2, f(n) = Θ(n) = Θ(n^(log_b a))`, case 2, so `T(n) = Θ(n log n)`. Space `O(n)` for the merge buffer; depth `ceil(log2 n)`.
* **Stable:** on ties the merge takes from the left half first, so equal keys keep their original order.
* **Dry run** `[5, 2, 9, 1]`: split `[5,2] | [9,1]`; merge `[5,2]` gives `[2,5]` (1 comparison); merge `[9,1]` gives `[1,9]` (1 comparison); final merge compares 2 vs 1 (take 1), 2 vs 9 (take 2), 5 vs 9 (take 5), copies 9 (3 comparisons): `[1,2,5,9]`, 5 comparisons.
* **Verified:** identical to `std::stable_sort` (including tie order) on sizes 0 to 1000 with many equal keys; comparisons never exceed `n·ceil(log2 n)`; recursion depth never exceeds `ceil(log2 n)`; exactly `n - 1` merges.

## 11. Topological sort — `topologicalSort` (`advanced/TopologicalSort.cpp`)

* **Purpose:** order an optional acyclic manufacturing dependency graph (edge `a → b`: `a` before `b`) and report invalid (circular) dependencies. The transportation network is NOT assumed acyclic.
* **Algorithm (Kahn):** count each node's unfinished prerequisites; repeatedly output a node with none (smallest id first, so results are deterministic) and release its dependents. If nodes remain but none is free, the graph has a cycle; a DFS with white/grey/black colouring over the blocked nodes extracts one explicit cycle for the error message.
* **Dry run** (edges 0→2, 1→2, 2→3, 2→4): free = {0,1}; output 0, 1 (releasing 2); output 2 (releasing 3, 4); output 3, 4. Order `0 1 2 3 4`. With edges 0→1, 1→2, 2→1, 2→3: output 0; nodes 1, 2, 3 stay blocked; DFS finds the cycle `1 2`.
* **Complexity:** `O(V + E)` time and space.
* **Verified:** results agree with an independent oracle (Floyd-Warshall transitive closure: cyclic iff some node reaches itself) on 400 random graphs; every returned order is checked edge by edge; every reported cycle is checked to be a real cycle of the input (edges present, nodes distinct). A supply-chain graph with a loop of routes is reported cyclic, and acyclic again once that route is closed.

## 12. NP-completeness extension

Proof of NP-completeness for the restricted supplier-activation decision problem, with its limits (weak NP-completeness only): `docs/NP_COMPLETENESS.md`.

## 13. Multi-day simulation: time-expanded min-cost flow with rolling-horizon re-planning (`multiday/`)

The full model, the daily event order and the C++ components are in `MULTI_DAY_SIMULATION.md`; this is the algorithmic summary.

* **Problem:** each day, decide what to ship given stock, goods already in transit (arriving after a fixed number of days), today's capacities and what is owed to customers, and which recovery actions to buy.
* **Idea (reduction to min-cost flow):** copy every node once per day of a planning window of `W` days. A route with transit `L` becomes arcs `(i, τ) → (k, τ + L)`, holding stock overnight is an arc `(i, τ) → (i, τ + 1)`, serving demand is an arc to the sink with a negative cost (the penalty avoided), and serving late is an arc backwards in time at a customer costing the daily backlog cost. Stock on hand and known arrivals must enter the plan (cost −M). One min-cost flow then gives the cheapest plan for the whole window.
* **Rolling horizon:** execute only today's shipments, then re-plan tomorrow with new observations. The window must be longer than the supply lead time, otherwise the planner cannot see any benefit of buying today; the simulator enforces `W ≥ lead + supply lead + 1`.
* **Pseudocode (one simulated day):**
  ```
  beginDay(t): arrivals → events → finished actions → capacities → today's demand
  actions = policy.decide(state, forecast)            // the only part that differs between policies
  pay for actions; schedule them for t + lead
  plan = minCostFlow(timeExpandedNetwork(state, forecast, W))
  check plan (capacities, supply, no negative stock); dispatch today's part
  serve customers (oldest backlog first); backlog, costs, records; check conservation of goods
  ```
* **Policies:** `greedy` and `bottleneck_aware` (immediate benefit over a short window, O(k²) plans per day); `lookahead` (simulate `h` days per candidate inside the forecast, O(k² · h) plans); `rolling_exact` (all 2^k subsets of today's candidates); and a **clairvoyant lower bound** (all 2^k subsets of the actions that ever become possible, one full-horizon plan each, with perfect information).
* **Correctness:** at the true horizon end the planner's objective equals the simulator's accounting (hand-checked: 715); goods are conserved every day (`initial stock + initial pipeline + drawn = stock + in transit + served`); no policy can beat the lower bound, which every experiment run checks.
* **Complexity:** one plan is a min-cost flow on about `V·W` nodes and `(E + 2V)·W` arcs; see `COMPLEXITY_ANALYSIS.md`.
* **Limitations:** only today's choices are exhaustive in `rolling_exact`; the bound is not always attainable; look-ahead is blind when its horizon is too short.

## Test evidence

`backend/tests/test_graph.cpp`, `test_traversal_dijkstra.cpp`, `test_max_flow.cpp`, `test_min_cost_flow.cpp`, `test_recovery.cpp`, `test_scenario_io.cpp`, `test_simulation.cpp`, `test_api.cpp`, `test_inventory_dp.cpp`, `test_supplier_selection.cpp`, `test_sorting_topological.cpp`, `test_multiday_core.cpp`, `test_multiday_policies.cpp`, `test_multiday_experiments.cpp` (run with `ctest`). Cases include disabled nodes/edges, zero capacity, unreachable nodes, invalid ids, directed-reachability, and relaxation replacing a worse distance.
