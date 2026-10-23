# Correctness and Testing

How the correctness of SupplyChainX was established, what each test suite checks, and where the evidence is weaker. Results of the last full run are in section 7.

## 1. Strategy

Correctness is argued in four independent ways, and an algorithm is only considered verified when the applicable ones agree:

1. **Proof or standard argument** for each algorithm (`ALGORITHMS.md`, `NP_COMPLETENESS.md`).
2. **Hand-computed answers** on small instances designed so the answer can be checked on paper (e.g. the textbook max-flow network with value 23; a min-cost plan of 52; the inventory DP table).
3. **Independent oracles** on many seeded random instances: an algorithm's answer must equal a slower, obviously correct method that shares no code with it.
4. **Independent validation of every plan**: a validator and a cost function that do not depend on the solver check every shipment plan the system produces, including inside the experiments.

Two further habits make the tests trustworthy rather than merely green:

* **Mutation checks.** After a suite passed, the code under test was deliberately broken (one change at a time) to confirm that the suite fails, then restored and re-run. Several weak tests were found and fixed this way (section 5).
* **Looking at real output.** Passing tests do not prove a scenario is meaningful; printed numbers and screenshots were inspected, which found bugs no test had caught (section 6).

## 2. Mathematical validation (independent of every solver)

`validateFlowSolution` (`backend/src/optimization/FlowSolution.cpp`) checks each item the specification requires; `computeCost` recomputes the objective from raw flows.

| Requirement | Check |
|---|---|
| Non-negative flow | every edge flow ≥ 0 |
| Capacity constraints | flow ≤ available capacity; 0 on failed routes and routes touching failed nodes |
| Supply limits | supply drawn within [0, supply] of an active node |
| Inventory conservation | stock drawn within [0, stock]; inventory cannot be invented |
| Flow conservation | drawn supply + stock + inflow − outflow = consumption at every node (demand − unmet at customers, 0 elsewhere) |
| Demand accounting | unmet demand within [0, demand]; unmet only at customers; shortage is never counted as delivered |
| Shortage penalties, total cost | `computeCost` recomputes purchase, transport, holding (unused stock only) and penalty; reported costs must match |
| Recovery plans | `validateRecoveryPlan`: actions legal in sequence, each counted once, action cost and unmet demand match recomputation |

Tests also tamper with plans on purpose (duplicated action, understated cost, claimed delivery without shipments) and require the validators to reject them.

## 3. Test suites (C++, run by CTest)

| File | What it verifies | Oracle / method |
|---|---|---|
| `test_graph.cpp` | stable ids, invalid input rejected, capacity reduction keeps design capacity, usability rules, structural validation | hand cases |
| `test_traversal_dijkstra.cpp` | BFS order and hop counts, DFS visit and finish order, directed (not undirected) reachability, disabled nodes and edges, failure impact, cheapest paths, unreachable targets, relaxation replacing a worse distance | hand cases |
| `test_max_flow.cpp` | textbook network (23), zero capacity, disconnected source and sink, a case that needs a reverse residual arc, min cut equals max flow, supply- and demand-limited cuts, disruptions | hand cases, max-flow min-cut theorem |
| `test_min_cost_flow.cpp` | hand-checked optima, shortage penalized and not delivered, low penalty means ship nothing, free stock with holding on leftovers, negative reverse costs, validator rejects broken plans | **exhaustive enumeration of every integer plan on 25 random networks** |
| `test_recovery.cpp` | backup activated only when it pays off, route reopening, greedy provably suboptimal (56 vs 50), risk term changes a choice (40 vs 42), tampered plans rejected | **exhaustive search over action subsets on 40 random networks**: every plan valid, exact ≤ every heuristic ≤ doing nothing |
| `test_scenario_io.cpp` | bundled file loads, JSON round trip, malformed scenarios rejected with a precise message, events parsed by name or id | hand cases |
| `test_simulation.cpp` | all disruption types, invalid events rejected atomically, reset restores the pristine state, checkpoint returns to the same disrupted state, scripted stepping, metrics read from state, all 8 standard scenarios × 6 strategies, reproducibility, generator structure, healthy generated networks serve all demand | invariants (dispatch ≥ min-cost flow ≤ ...) |
| `test_api.cpp` | every endpoint and error status (400/404/405/409/422), path traversal, failed loads keep state, preview vs execute, compare never mutates, CSV/JSON export, experiments endpoint and its limits, the multi-day endpoint (totals equal to a direct C++ run, daily records add up, dispatch within each day's capacity, recovery days only when defined, validation and work limits), real heap measurement, real HTTP with CORS, and the specification's 10-step workflow over sockets | end-to-end |
| `test_inventory_dp.cpp` | worked example and DP table entries by hand, planning ahead beats ordering each day's need, policy evaluator, edge cases | **brute force over all order sequences on 80 random problems** |
| `test_supplier_selection.cpp` | cost definition by hand, greedy suboptimal (15 vs 12), unit cost vs penalty, pruning keeps the optimum, the NP-completeness reduction | **all-subsets oracle on 40 random instances; subset-sum brute force on 150 instances; the reduction solved through the real recovery engine on 60 instances** |
| `test_sorting_topological.cpp` | merge sort equal to `std::stable_sort` (including ties) with comparison and depth bounds; shipment ordering; topological orders, cycles, self-loops; transport network not assumed acyclic | **`std::stable_sort`; Floyd-Warshall transitive closure on 400 random graphs** |
| `test_multiday_core.cpp` | the 13-step daily order on a hand-calculated chain (served, backlog, in transit and cost of every day, total 715, and the day-0 plan equal to that accounting), zero-day transit delivering the same day, arrivals exactly after their transit time and exactly once, several shipments in flight, capacities reset every day, persistent supplier failure with goods already moving, supplier recovery, route repair with lead time, action cost charged once and no repeat purchase, warehouse depletion never negative, demand spike and FIFO backorders (backlog cost 300, settlement 200 by hand), recovery-time definition (delayed dip, never dips, too late to confirm, relapse, relative to normal service), conservation every day by independent recount, deterministic replay and reset, forecast modes without leakage, minimum planning window, invalid scenarios | **hand calculations**; independent recount |
| `test_multiday_policies.cpp` | every policy produces valid runs on identical inputs; clairvoyant bound ≤ every policy (8 seeds × 2 forecast modes × cold and warm start) and attained exactly when all its actions are on day 0; bound by hand (100); look-ahead buys earlier than greedy when the benefit is delayed and costs less; persistence vs perfect forecast decide on different days (no leakage); no purchases in healthy scenarios; delivery delay by hand; rolling exact skipped above its limit; determinism; scenario builder; warm-start shape and a healthy network serving every day (also with 3-day transit); normal-service baseline equal to the undisrupted run | **clairvoyant lower bound**, hand cases |
| `test_multiday_experiments.cpp` | validation of experiment configs; every result row equal to an independent run of that policy on the same seed; identical demand for every policy in a run; bound ≤ every policy; aggregates and daily rows recomputed from records; CSV column counts; all ten presets valid; determinism | independent recomputation |
| `test_experiments.cpp` | statistics by hand, each scenario kind does what it says, per-run invariants for five scenario kinds, aggregates and regret recomputed from raw records, reproducibility, JSON parsing, CSV column counts, presets valid | independent recomputation |

## 4. Integration and end-to-end testing

| Level | Tool | What runs |
|---|---|---|
| Service | `test_api.cpp` | `ApiService` called directly and over a real socket; the specification's workflow (load, view, supplier failure, greedy, reset to the same disrupted state, min-cost flow, compare, view flows, export) |
| Process | `scripts/smoke_test_api.ps1` | the real `scx_server.exe` started as a separate process and driven over HTTP |
| Browser | `frontend/scripts/e2e.mjs` (`npm run e2e`) | the built dashboard in real Chrome (via `puppeteer-core`) against the real server: the same 10-step workflow through the UI, the Experiment Lab, the Multi-day Simulator (the table must show exactly the totals in the captured server response; recovery text matches the server's recovery flag; charts; day slider redraws that day's network), Lab visualisations, Learning Center, both themes, and zero page or console errors |
| Frontend units | Vitest (`npm test`) | API client error handling, layout, disruption target lists, CSV export, formatting, lesson completeness, multi-day timeline/snapshot helpers |

## 5. Mutation checks performed (each made the named suite fail, then was reverted)

| Phase | Deliberate defect | Detected by |
|---|---|---|
| 3 | min-cost flow stop rule removed | `test_min_cost_flow` |
| 4 | bottleneck term removed; "action must pay for itself" rule removed | `test_recovery` |
| 5 | reset does not restore the network | `test_simulation` |
| 6 | scenario-name validation disabled (path traversal) | `test_api` (only after the test was strengthened, see below) |
| 8 | merge sort made unstable; unsound backtracking bound; holding cost dropped from the DP | the three syllabus suites |
| 9 | population instead of sample sd; paired sign flipped; demand-spike rounding changed; API work cap removed; regret formula broken; skipped CSV rows too short | `test_experiments`, `test_api` |
| Multi-day | true future events leaked into the persistence planning input | `test_multiday_core` |
| Multi-day | true future events leaked into the forecast scenario used by look-ahead | `test_multiday_policies` |
| Multi-day | warm-start pipeline ignored by the initial state (12 failed checks) | `test_multiday_policies` |
| Multi-day | planning window no longer raised to the supply lead | `test_multiday_core`, `test_multiday_policies` |

**Weak tests found by this process and fixed:** the first path-traversal test used a file that does not exist, so it passed even without validation; a Windows-backslash traversal test had been silently corrupted by editing tooling into a different string; two mutations first reported as caught had in fact not been applied or were equivalent; nothing constrained the regret and win numbers until an independent recomputation test was added.

## 6. Regression tests for bugs found during development

| Bug | Regression test |
|---|---|
| Generated "healthy" networks already had unmet demand, making disruption scenarios meaningless (found by inspecting printed numbers) | `testHealthyGeneratedNetworksServeAllDemandAndDisruptionsBite` (15 seeds + 5 stress seeds) |
| Dummy-shortage-source reduction could forward fake goods when customers transship (found while reasoning about the model) | replaced by delivery arcs; hand and brute-force min-cost-flow tests |
| Heap corruption when replacing `operator new` with a dynamically linked C++ runtime | static linking of hooked executables; `test_api` runs with the hooks |
| Comparison vanished when leaving the page; Lab defaulted to a failed source; stale comparison keyed by event count | browser test checks for each |
| Experiment chart averaged exact search over an easier subset of runs | plotting omits incomplete points; documented |
| Multi-day recovery time was 0 when the impact of a failure was delayed by stock | measured after the first dip (`testRecoveryTimeDefinition`) |
| Multi-day runs started with empty routes, so a start-up shortage dominated every cost (found in the first experiment results) | warm start; `testWarmStart` |
| With 3 transit days per route the 7-day planning window could not see any delivery and the pipeline ran dry (fill rate about 71% with no disruption at all) | `testPlanningWindowCoversTheSupplyLead`, 3-day case in `testWarmStart` |
| With demand noise a healthy network sometimes serves below 95%, so no run ever 'recovered' | recovery against normal service; cases in `testRecoveryTimeDefinition` and `testWarmStart` |
| Delivery delay of an inactive backup was -1 (the search treated it as unusable) | `testDaysUntilFirstDelivery` |
| The generator threw on small networks (automatic route count above the possible number) | scenario builder tests on small networks |

## 7. Last full verification run

After the multi-day extension (October 2026), on Windows 11 with g++ 15.2, CMake 4.2 and Node 24. `build/` and `frontend/dist` were deleted first. `frontend/node_modules` was kept (the lockfile is unchanged since the Phase 10 run, which did a clean `npm ci`).

| Step | Result |
|---|---|
| `cmake -S . -B build -G Ninja` and `cmake --build build` | succeeded in 41 s, 0 warnings with `-Wall -Wextra -Wpedantic` |
| `ctest --test-dir build` | 15/15 executables passed |
| `npm test`, `npm run build` | 27/27 unit tests in 5 files; build succeeded |
| `scripts/smoke_test_api.ps1` | all checks passed |
| `npm run e2e` | 38/38 browser checks passed, no page or console errors |
| Re-running multi-day experiments M3, M8, M9 with the rebuilt binary | 29,760 run and daily records identical to the committed results in every non-timing column |
| `scripts/check_docs.py` | every path referenced by the documentation exists |

| Test executable | Cases | Checks |
|---|---|---|
| `test_api` | 17 | 1,965 |
| `test_experiments` | 12 | 1,564 |
| `test_graph` | 5 | 24 |
| `test_inventory_dp` | 6 | 275 |
| `test_max_flow` | 7 | 25 |
| `test_min_cost_flow` | 8 | 77 |
| `test_multiday_core` | 16 | 205 |
| `test_multiday_experiments` | 6 | 299 |
| `test_multiday_policies` | 11 | 569 |
| `test_recovery` | 10 | 609 |
| `test_scenario_io` | 6 | 43 |
| `test_simulation` | 16 | 454 |
| `test_sorting_topological` | 7 | 1,507 |
| `test_supplier_selection` | 7 | 503 |
| `test_traversal_dijkstra` | 9 | 30 |
| **Total** | **143** | **8,149** |

A "check" is one assertion; the counts are large where tests loop over many random instances (`test_api` now includes a capacity check for every route on every day of every multi-day policy).

## 8. Known gaps in the testing

* Only Chrome was used in the browser test (Edge is a fallback path in the script but was not exercised); no mobile testing beyond responsive CSS.
* Only the MinGW-w64 g++ toolchain on Windows was used; MSVC and Linux builds are untested.
* No fuzzing of the HTTP interface beyond the listed malformed-input cases.
* Runtime measurements are from a single machine.
* The multi-day planner is checked against hand calculations on small chains and against the clairvoyant lower bound, but there is no exhaustive oracle for whole multi-day runs (the space of daily plans is too large).
* The random-instance oracles use small instances (that is what makes exhaustive oracles possible); large instances are checked by invariants and the validator, not by an exact oracle.
