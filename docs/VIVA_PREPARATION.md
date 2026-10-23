# Viva Preparation

Questions an examiner is likely to ask, with answers that match this implementation. Each answer points to where the detail is. Practise answering in your own words; the point is to understand, not to recite.

## 1. A five-minute demonstration

1. **Start** `scripts\start.ps1`, open the dashboard, load **Bundled file → small_demo**. Overview: 3 suppliers (one an offline backup), 120 units of demand, all served, cost 890.
2. **Supply Network:** point out node types, `flow/capacity · cost` labels, and the orange bottleneck routes. Click a route.
3. **Disruption Simulator:** shut down S1 and S2. Unmet demand jumps (110 units short), two active disruptions, failed nodes drawn in red.
4. **Algorithm Lab, BFS from S1:** visits nothing; "cut off since the initial network" lists what the failure isolated. **Dijkstra S3-backup → C1:** unreachable, reported rather than crashing.
5. **Algorithm Lab, Greedy recovery, Run and apply:** activates S3-backup for 40; unmet demand falls to 55; decision trace shows the score.
6. **Supply Network → Undo recovery**, then **Comparison**: all six strategies on the same disrupted state, lowest cost marked, all valid; before/after graphs.
7. **Experiment Lab:** 20 runs at severity 0.4. Explain the paired column (● the 95% interval excludes zero, ○ it includes zero). For the saved 200-run experiment, bottleneck-aware vs greedy is +10 ± 17: an interval containing zero, i.e. no demonstrated difference.
8. **Learning Center:** open Merge sort and show the recurrence and Master Theorem case.

## 2. Project and model

**Q: What problem does the project solve?** After a disruption, decide which paid recovery actions to take (activate a backup supplier, reopen a route) and how to ship goods so that purchase + transport + holding + shortage penalty + action cost is minimal, subject to capacities, supply, stock and flow conservation. (`PROBLEM_STATEMENT.md`)

**Q: How is the supply chain represented?** A directed graph with adjacency lists: `std::vector<Node>`, `std::vector<Edge>`, and per node a list of outgoing and incoming edge ids. O(V + E) memory, O(out-degree) to scan neighbours. Ids are vector indices and never change; disruptions only flip `active` or lower `capacity`, and `baseCapacity` is never modified, so a reset is just a copy of the pristine graph.

**Q: How do you model unmet demand without counting it as delivered?** Unmet demand r_j = d_j − delivered_j. Since p_j·r_j = p_j·d_j − p_j·delivered_j, each customer gets a delivery arc to the super-sink with cost −p_j, and the constant p_j·d_j is added back. No fake goods ever enter the network. An earlier draft used a dummy shortage source, which is unsound when customers also pass goods on; it was replaced. (`MATHEMATICAL_MODEL.md` §7)

**Q: How do you avoid double-counting costs?** Purchase is charged only on units drawn from supply; stock already held is a sunk cost and is charged only through holding on what is left unused; each recovery action is charged once regardless of how much flow uses it. A separate `computeCost` recomputes the objective from raw flows and tests check it against the solver.

**Q: What does your model not include?** Multiple periods, multiple commodities, manufacturing conversion and production capacity, backlogged demand, stochastic demand. (`LIMITATIONS_AND_FUTURE_WORK.md`)

## 3. Algorithms

**Q: BFS vs DFS here?** Both answer directed reachability in O(V + E). BFS gives fewest-hop distances and is used for failure impact (compare reachability before and after a disruption). DFS gives the finish order used by topological sorting. Both follow edge direction: starting at a customer reaches only that customer, so it is not undirected connectivity.

**Q: Why is DFS iterative?** An explicit stack of (node, next-edge index) holds exactly what recursion would keep, without risking a stack overflow on deep graphs.

**Q: Why can't Dijkstra alone allocate shipments?** It finds the cheapest route for one unit and ignores capacities and other shipments. Two customers can both choose the same cheap route that cannot carry both. Our dispatch baseline (repeated Dijkstra, never undoing a shipment) is measurably worse than min-cost flow: +258 ± 122 even with no disruption, about 4.7% in the 200-run experiment.

**Q: Explain relaxation.** If dist[u] + cost(u, v) < dist[v], going through u is cheaper, so update dist[v] and its parent. With non-negative costs, the node popped with the smallest tentative distance cannot be improved later.

**Q: Why does Dijkstra need non-negative costs, and how does min-cost flow cope with negative reverse arcs?** A negative arc could improve a node after it is settled. Min-cost flow keeps node potentials π and runs Dijkstra on reduced costs c(u, v) + π(u) − π(v), which are non-negative for every residual arc; π is updated with the distances after each round. The initial potentials come from Bellman-Ford because the delivery arcs (−p) and stock arcs (−h) are negative.

**Q: Ford-Fulkerson vs Edmonds-Karp?** Edmonds-Karp is Ford-Fulkerson where every augmenting path is a shortest (fewest-edge) path found by BFS. That bounds the number of augmentations by O(VE), so the total is O(VE²); with arbitrary paths Ford-Fulkerson can take time proportional to the flow value.

**Q: Why are reverse residual arcs needed?** They let a later path undo flow sent earlier. `testReverseResidualArcIsUsed` has a network where the first BFS path blocks the optimum; the maximum (2) is reached only through a reverse arc.

**Q: What is the minimum cut and how do you find it?** After the last augmentation, the nodes still reachable from the source in the residual graph form one side; saturated edges leaving that side form a minimum cut, and its capacity equals the maximum flow (max-flow min-cut theorem, checked in tests). In the dashboard these are the bottleneck routes.

**Q: Max flow vs min-cost flow?** Max flow answers how much can be delivered, ignoring cost. Min-cost flow answers what the cheapest plan is; it may deliver less than the maximum if the extra units would cost more than their shortage penalty. We stop at the first augmenting path whose cost is non-negative.

**Q: Is min-cost flow optimal for the whole recovery problem?** Only for a fixed set of active suppliers and routes. Choosing which backups to activate adds fixed charges, which is the NP-hard part, handled by greedy, bottleneck-aware or exhaustive search.

**Q: Describe the greedy recovery.** Candidates are offline backups and failed routes. Each round, trial-apply every candidate to a copy of the network, re-solve min-cost flow, keep only candidates that reduce unmet demand and save more than they cost, and pick the best score (units of shortage removed per unit of action cost). Repeat until none qualifies. O(k²) min-cost-flow solves.

**Q: Is greedy optimal? Give a counterexample.** No. Demand 10, penalty 10: backup B1 offers 6 units for 6 (ratio 1.0), B2 offers 10 units for 30 (ratio 0.33). Greedy takes B1, then still needs B2: cost 56. B2 alone costs 50. Measured: about 2.2% above the exact optimum on average; it also misses actions that only save cost (it requires a shortage reduction).

**Q: What is the bottleneck-aware heuristic and did it work?** Score = L / (cost + λ·R + ε), where L is the penalty-weighted shortage avoided and R = f·L with f the share of shipped units on routes at least 90% utilised. λ = 0 removes the risk term (the ablation). Measured result: no paired difference to greedy or to λ = 0 had a 95% interval excluding zero in any experiment; 88% of λ > 0 plans had exactly the cost of the λ = 0 plan. The hypothesis is not supported here. It is not refuted in general: the model has one period, so the slack it buys is never rewarded by a second disruption.

**Q: Why include an exhaustive solver?** It is the exact reference that heuristics are measured against: 2^k min-cost-flow solves, limited to k ≤ 16 (API) or k ≤ 10 (experiments).

**Q: Explain the inventory DP.** State (day t, stock i); value = cheapest cost of days t..T−1. Base: 0 after the last day. Recurrence: try every legal order q and take min of today's cost + value of tomorrow's stock. Optimal substructure: the future depends on the past only through the stock level. Worked example: 26 by ordering ahead, versus 29 for ordering each day's need. O(T·C·Q) time.

**Q: How does your backtracking prune?** Lower bound = activation cost already committed + cost of covering the shortage if every undecided supplier were free. No completion can beat it, so if it is not below the best known cost the branch is cut. Honest note: on the tiny demo it prunes nothing (the bound ignores future activation costs); pruning saves work only on average.

**Q: Merge sort recurrence and Master Theorem?** T(n) = 2T(n/2) + Θ(n); a = 2, b = 2, f(n) = Θ(n) = Θ(n^(log_b a)), case 2, so Θ(n log n). It is stable because ties are taken from the left half. Used to order shipment records.

**Q: How does topological sort detect a cycle?** Kahn's algorithm outputs nodes with no remaining prerequisites; if some nodes are never released, they lie on or behind a cycle, and a DFS with white/grey/black colouring extracts one explicit cycle. The transport network is not assumed acyclic: a loop of routes is reported as a cycle.

## 4. Correctness and testing

**Q: How do you know min-cost flow is correct?** Theory (no negative residual cycle at optimality), hand-computed optima, and equality with exhaustive enumeration of every integer plan on 25 seeded random networks; every plan also passes the independent validator.

**Q: What is a mutation check and why did you do it?** Deliberately break the code and confirm the tests fail. It found tests that passed for the wrong reason (for example a path-traversal test that used a file that does not exist). (`CORRECTNESS_AND_TESTING.md` §5)

**Q: How did you verify the NP-completeness reduction?** The proof is the argument. As evidence that the code agrees with it, subset-sum instances were mapped to real supply-chain networks and solved by the exhaustive recovery solver; its answer matched brute-force subset sum on 60 instances.

**Q: Any bug you found that the tests missed?** The first generator produced "healthy" networks that already had unmet demand, so every disruption scenario was meaningless, yet all tests passed. Inspecting the printed numbers revealed it; a regression test now guards it.

## 5. Complexity and hardness

**Q: Prove the restricted supplier-activation problem NP-complete.** In NP: given a set S, check Σc_i ≥ D and Σa_i ≤ B in polynomial time. NP-hard: reduce SUBSET-SUM (v_1..v_n, target T) by c_i = a_i = v_i, D = B = T. If a subset sums to T it satisfies both; conversely T ≤ Σc_i = Σa_i ≤ T forces Σ = T. (`NP_COMPLETENESS.md`)

**Q: Strong or weak NP-completeness?** Weak: numbers are in binary and a pseudo-polynomial DP in O(n·T) exists (not implemented). No strong NP-hardness claim is made.

**Q: What did the scaling experiment show?** Over 27 to 216 nodes: dispatch about n^1.95, min-cost flow about n^2.1, greedy and bottleneck-aware about n^3.1 (greedy does k trial solves per round, each about n²); heap about linear. Six sizes, one machine: indicative only. (`COMPLEXITY_ANALYSIS.md` §3)

**Q: Is min-cost flow polynomial?** The successive-shortest-path bound O(VE + F(V + E) log V) depends on the flow value F, so it is pseudo-polynomial; in our networks F grows with demand.

## 6. Experiments and statistics

**Q: Why a paired design?** Every strategy solves the identical disrupted network in each run, so differences are due to the algorithm, not the scenario. Paired differences have much smaller variance than comparing two separate averages.

**Q: What does a 95% interval that includes zero mean?** The data cannot distinguish the difference from noise. It is not proof that there is no difference.

**Q: What are the main threats to validity?** One generator family, small networks; exact search only on easier scenarios; many intervals (about 1 in 20 would exclude zero by chance); timing from one machine; a single-period model. (`EXPERIMENTAL_EVALUATION.md` §4)

**Q: Why did you omit some points from the charts?** Where exhaustive search could not run on every scenario, its average was over a different, easier subset and looked misleadingly cheap (22% below everyone at 100% route loss). Those points are omitted and only paired comparisons on the same runs are reported.

## 6b. Multi-day simulation

**Q: How does the multi-day model plan shipments?** A time-expanded network: a copy of every node for every day of a planning window, with routes going forward by their transit days, holding arcs between days, and late-service arcs at customers. One min-cost flow plans the whole window; only today's shipments are executed, and tomorrow the plan is redone with new observations (rolling horizon).

**Q: What is the daily order and why does it matter?** Arrivals, then scheduled events, then actions that finish today, then capacities and demand, then the policy's decision, then plan, validate, dispatch, serve, backlog, costs, record. A fixed order prevents counting goods twice, using goods before they arrive, or charging a shortage twice. Conservation of goods is checked after every day.

**Q: How do you avoid information leakage?** Policies never see the scenario. They get a forecast: persistence (today's state and demand continue) or perfect (the true future inside the window). Perfect-forecast results are reported separately, as an upper limit. Two leakage mutations were each caught by a test.

**Q: How is recovery time defined?** From the first disruption day to the first day of 3 consecutive days at or above min(95%, normal service), after service first fell below it. Normal service is the same scenario without disruptions. If that never happens within the horizon, no number is reported: "not recovered".

**Q: Did the bottleneck-aware heuristic help over time?** No measurable difference in any of 29 settings. Greedy and its λ = 0 version made identical decisions in every run. Its risk signal does predict later shortage (Spearman 0.41), but almost nothing beyond today's backlog (R² +0.012).

**Q: What worked best?** A look-ahead of 8-12 days. It was cheaper than greedy and also than per-day exhaustive search on the same scenarios, at 3.5-6.5 times greedy's decision time. Look-ahead shorter than the action lead plus the supply lead never acts at all.

**Q: Why is the clairvoyant bound not reachable?** It plans around actions it will buy later, which an online planner cannot anticipate. So the gap to the bound includes the value of information. With a perfect forecast, rolling exact came within 6% of it, against 57% under persistence.

## 7. C++ and software design

**Q: Why C++ and how is it organised?** Performance and control for the algorithms; one static library `scx_core` that the server, tools and tests link against, so every algorithm is testable without a network.

**Q: How does trying an action not disturb the real state?** `SupplyChainGraph` is a value type: `SupplyChainGraph trial = state;` is a deep copy, so a heuristic can change `trial` and throw it away.

**Q: How do greedy and bottleneck-aware share code?** One loop, `runGreedyLoop`, takes the scoring function as a `std::function` built from a lambda; only the score differs.

**Q: How is memory measured?** Global `operator new/delete` are replaced; each block carries a 16-byte header with its size, and counters track live and peak bytes. Pitfall: with a dynamically linked C++ runtime the DLL allocated with its own `operator new`, causing heap corruption; the measuring executables are linked statically.

**Q: How does the API stay consistent under bad input?** Every field is type-checked; events are applied to a copy and swapped in only on success; a failed scenario load keeps the previous scenario; requests are serialised with a `std::lock_guard`.

## 8. Numbers worth remembering

| Fact | Value |
|---|---|
| Recovery vs re-routing only (200 runs, severity 0.4) | −2,675 ± 350 mean cost (about 21% lower); fulfilment 84% → 96% |
| Greedy vs exact optimum (200 runs) | +222 ± 37 (about 2.2%) |
| Bottleneck-aware vs greedy (200 runs) | +10 ± 17: no demonstrated difference; equal cost in 174 of 200 runs |
| Ablation, 100 runs, λ = 0.25 … 5 vs λ = 0 | every interval includes zero; 88% of plans with exactly the λ = 0 cost |
| Growth exponents | dispatch 1.95, min-cost flow 2.10, greedy 3.05 |
| Textbook max-flow test | 23 |
| Greedy counterexample | 56 vs optimum 50 |
| Inventory DP example | 26 vs 29 for ordering each day's need |
| Multi-day hand-calculated chain (5 days) | total 715; settlement 250 |
| Multi-day: none vs greedy | dearer in 21 of 25 settings |
| Multi-day: rolling exact vs greedy | cheaper in 20 of 25 settings, never dearer |
| Multi-day: bottleneck-aware vs greedy / λ = 0 | no measurable difference in 29 of 29 settings |
| Multi-day: look-ahead(12) vs greedy, vs rolling exact | −27,074 ± 23,821; −3,129 ± 1,688 |
| Multi-day look-ahead time growth | about h^1.9 (3 ms at h = 1 to 319 ms at h = 12 per 30-day run) |
| Risk signal vs future shortage | Spearman 0.41 (backlog alone 0.78); R² 0.544 → 0.556 |
