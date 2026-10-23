# Limitations and Future Work

What SupplyChainX does not do, why, and what it would take. Nothing listed under "future work" is implemented.

## 1. Modelling limitations

| Limitation | Consequence |
|---|---|
| **The single-period model has no time.** The main recovery strategies (Part 1 of the evaluation) solve one planning period with lost sales. Time is modelled only in the separate multi-day simulation (`MULTI_DAY_SIMULATION.md`). | Single-period results say nothing about recovery time; multi-day results describe the multi-day model only. |
| **Multi-day model scope.** One product, no manufacturing, no storage limits, backorders with a linear daily cost plus a lost-sale penalty at the horizon end, fixed per-route transit days, 5 to 30 days. | Perishability, capacity of warehouses, and transit times that vary from day to day are not represented. |
| **Disruption schedules are fixed per seed.** The schedule is known to the simulator and, in perfect-forecast mode, to the policies; under persistence the policies learn about an event only when it happens. There is no probabilistic forecast. | "Perfect" is an upper limit on what forecasting could give, not a realistic forecast. |
| **No safety-stock policy.** The planner sees no uncertainty, so it keeps no buffer against demand noise; with noise, even a healthy network sometimes serves below 95%. | This is why recovery time is measured against *normal service* (the same scenario without disruptions), not against a fixed 95%. |
| **One commodity.** | No product mix, no substitution between products. |
| **No manufacturing.** Factories exist as a node type but behave like any node that passes goods on or supplies them. `processingCapacity` is stored and validated but not used by any algorithm; the generators contain no factories. | Production constraints and bills of materials are outside the optimization. The topological sort is the only manufacturing-related module, and it is standalone. |
| **Linear costs.** All costs are linear in quantity except one-off action costs. | Economies of scale and tiered pricing are not represented. |
| **Restricted DP module.** The inventory DP is one product at one location. | It illustrates dynamic programming; the network over time is handled by the multi-day simulator instead. |
| **Recovery actions are limited** to activating a backup supplier and reopening a failed route; a route that only lost part of its capacity cannot be repaired. | Expanding capacity, expediting, or renting transport are not options. |

## 2. Algorithmic limitations

* **Greedy and bottleneck-aware are myopic.** They add one action at a time and only accept actions that reduce shortage and pay for themselves alone, so they miss actions that only help together and actions that only save cost. Single-period gap to the exact optimum: about 2.2% on average. In the multi-day model the ranking score barely matters because every eligible action is eventually taken (see `EXPERIMENTAL_EVALUATION.md` section 7.3).
* **Exhaustive searches are exponential**: the single-period exact solver (limited to 16 candidates in the API, 10 in the experiments), `rolling_exact` (2^k plans per day, default limit 8), and the clairvoyant bound (2^k full-horizon plans, limit 10 in the experiments and the API). Above the limits they are reported as skipped or not computed, never approximated silently.
* **`rolling_exact` is only exhaustive over one day's choices.** It is not optimal over the horizon: look-ahead with 8-12 days beat it on the same scenarios.
* **The clairvoyant bound is valid but not always attainable.** It can plan around actions bought later, which no online planner can; its gap therefore mixes heuristic loss with the value of information.
* **Look-ahead needs a long enough horizon** (more than the action lead plus the supply lead) to see any benefit, and its time grows about quadratically with the horizon. Its value of an action is truncated at the end of the look-ahead, which undervalues one-off activations at short horizons.
* **The backtracking bound is weak**: it ignores future activation costs, so it prunes nothing on tiny instances.
* **Min-cost flow is pseudo-polynomial** in the amount of flow; fine at the tested sizes, slower for very large demands. The multi-day planner solves one such flow per day on a network about `W` times larger.
* **NP-completeness is proved only for a restricted problem and only in the weak sense** (numbers in binary). No strong NP-hardness claim is made, and the pseudo-polynomial DP for that restricted problem is not implemented.

## 3. Experimental limitations

* One network generator and one parameter family in each part: layered networks with 27 nodes in Part 1 (27 to 216 in the size sweep), and 19 nodes over 30 days in Part 2. Results may differ for other topologies, cost structures, longer action leads or repeated disruptions.
* Comparisons with the single-period exact search use only the runs where it could run, which are the easier ones.
* Many confidence intervals are reported (over 100 in Part 2); about 1 in 20 would exclude zero by chance.
* Multi-day cost differences are skewed: a few scenarios with large shortages dominate the means, so intervals can include 0 although 19 of 20 runs agree. Run counts are reported alongside.
* The risk-signal analysis (Step 5) is correlational and uses one target window (the next 3 days).
* Timing and memory come from one Windows machine; sub-millisecond times are noisy. The multi-day runner does not report heap figures.
* The bottleneck-aware result is a null result for these settings, not a refutation of the idea in general.

Details: `EXPERIMENTAL_EVALUATION.md` sections 4 and 8.

## 4. Software limitations

* **Single-user local tool.** One live simulation per server; requests are serialised; the server binds to 127.0.0.1 only, with no authentication (by design: the specification excludes authentication).
* **Long requests block the server.** Experiment and multi-day requests occupy the server until they finish; work limits (single-period: 200 runs, 400 nodes, runs × nodes ≤ 6000; multi-day: 5-30 days, 60 nodes, look-ahead ≤ 12, exhaustive searches ≤ 10 candidates) keep that to seconds.
* **Platform.** Built and tested only with MinGW-w64 g++ 15.2 on Windows 11. MSVC and Linux builds are untested, and the memory hooks rely on static linking under MinGW.
* **Browser.** The end-to-end test used Chrome only; no mobile testing beyond responsive CSS.
* **Frontend bundle** has no code splitting. Graph nodes cannot be dragged (fixed layered layout).
* **Saved results are large.** The multi-day daily CSV files total about 13 MB in the repository.

## 5. Future work

In rough order of value for this project:

1. **Repeated and random disruptions in the multi-day model**: draw failures during the run, so that the slack the bottleneck-aware term prefers could pay off at a *second* disruption; and a probabilistic forecast between persistence and perfect.
2. **Safety stock in the planner**: a target buffer at customers or distribution centers, to separate noise-driven shortage from disruption-driven shortage.
3. **Better recovery search**: pairwise look-ahead in greedy (consider two actions together), a look-ahead that values an action beyond its window (a terminal value for activated capacity), a stronger branch-and-bound bound, or a mixed-integer formulation solved exactly for medium instances.
4. **Pseudo-polynomial DP for supplier activation** (knapsack-style over covered units), to contrast with backtracking on the same instances.
5. **Manufacturing**: production capacity on factory nodes (node splitting turns it into an edge capacity in the flow model), and a restricted bill of materials checked against the topological order.
6. **Broader experiments**: other topologies, more backup suppliers, longer action leads, larger instances with a faster min-cost flow (for example cost scaling).
7. **Engineering**: a Linux/MSVC build in continuous integration; code splitting of the frontend; tests in Edge and Firefox.
