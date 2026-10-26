# PROJECT_PROGRESS

Last updated: 2026-10-26 (multi-day extension complete and verified)

## Environment (inspected, not assumed)
g++ 15.2.0 (MinGW-w64), CMake 4.2.1, Ninja, Node 24.18.1, npm 11.16.0, Python 3.13.1, Git 2.53. Project lives on D: (145 GB free; C: has only 8 GB). Git repository initialised before the multi-day work; one commit per increment.

## Build / test commands
```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Phase status
| Phase | Status |
|---|---|
| 0 Environment | done |
| 1 Architecture, math model, structure, CMake | done |
| 2 Graph engine, BFS, DFS, Dijkstra | done |
| 3 Max-flow, min cut, min-cost flow | done - 4/4 CTest executables pass |
| 4 Recovery: greedy, bottleneck-aware, plan validation, exact reference | done - 5/5 CTest executables pass |
| 5 Simulation engine: scenarios, disruptions, reset/checkpoint, metrics, generator, runner | done - 7/7 CTest executables pass |
| 6 API: service, HTTP server, allocation tracker, smoke script | done - 8/8 CTest executables + PowerShell smoke test pass |
| 7 Frontend: 6 dashboard pages, API client, unit tests, real-browser e2e | done - 15 unit tests, 23 e2e checks pass (Experiment Lab page deferred to Phase 9 with its backend) |
| 8 Advanced DAA modules: inventory DP, backtracking supplier selection, merge sort, topological sort, NP-completeness proof | done - 11/11 CTest executables pass |
| 9 Experiments: runner, statistics, experiments A-G with saved results and charts, API endpoint, Experiment Lab page, evaluation document | done - 12/12 CTest executables, 19 frontend unit tests, 30/30 browser checks pass |
| 10 Final verification and remaining documents | done - clean rebuild, 12/12 CTest (108 cases, 5,349 checks), 22 unit tests, 13/13 smoke, 31/31 browser checks, experiments reproduced, docs checked |
| M0 Preserve: build and tests verified, git initialised (.gitignore, .gitattributes), initial commit | done |
| M0 Design: `docs/MULTI_DAY_SIMULATION.md` (state, backorders, 13-step daily order, planner, information model, policies, recovery time) | done - commit 8dfa12c |
| M1 Multi-day core: model, time-expanded dispatch planner, simulator, forecast provider | done - commit 5a48e76; hand-calculated chain reproduced |
| M2 Policies (none, greedy, bottleneck-aware, λ = 0, look-ahead, rolling exact), clairvoyant bound, seeded scenarios | done - commit 1d86b0e |
| M3 Experiments M1-M10, risk-signal analysis, `/api/multiday/run`, Multi-day Simulator page; warm start, normal-service recovery, minimum planning window | done - commit b4dd846 |
| M4 Final verification, documentation, `docs/FINAL_REPORT.md` | done - clean rebuild (41 s, 0 warnings), 15/15 CTest (143 cases, 8,149 checks), 27 unit tests, smoke test, 38/38 browser checks, 29,760 multi-day records reproduced, docs checked |

## Files
Phase 1-2: `CMakeLists.txt`, `backend/CMakeLists.txt`, `backend/include|src/graph/SupplyChainGraph`, `backend/include|src/algorithms/{GraphTraversal,Dijkstra}`, `backend/tests/{test_framework.h,test_graph.cpp,test_traversal_dijkstra.cpp}`, `backend/third_party/*`, `data/scenarios/small_demo.json`.
Phase 3: `backend/include|src/algorithms/{FlowNetwork,SupplyChainFlow}`, `backend/include|src/optimization/FlowSolution` (validator, independent cost function, metrics), `backend/tests/{test_helpers.h,test_max_flow.cpp,test_min_cost_flow.cpp}`.
Phase 4: `backend/include|src/optimization/RecoveryPlan` (actions, apply, validator, exhaustive reference), `backend/include|src/algorithms/RecoveryHeuristics`, `backend/tests/test_recovery.cpp`.
Phase 5: `backend/include|src/simulation/{Disruption,Strategy,Simulation,ScenarioIO,NetworkGenerator}`, `backend/include|src/algorithms/DispatchBaseline`, `backend/tests/{test_scenario_io.cpp,test_simulation.cpp}`; `SCX_DATA_DIR` test definition in `backend/CMakeLists.txt`.
Phase 6: `backend/include|src/api/{ApiService,HttpServer}`, `backend/src/main.cpp` (scx_server), `backend/include/util/AllocationTracker.h` + `backend/src/memory/{AllocationTracker,AllocationHooks}.cpp`, `backend/tests/test_api.cpp`, `scripts/smoke_test_api.ps1`.
Phase 7: `frontend/` (`package.json`, `vite.config.ts`, `src/{types,services,hooks,utils,components,pages,styles}`, `scripts/e2e.mjs`), `scripts/start.ps1`, `README.md`, `docs/screenshots/`, `docs/API_REFERENCE.md`.
Phase 8: `backend/include|src/advanced/{InventoryDP,SupplierSelection,MergeSort.h,Shipments,TopologicalSort}`, `backend/tools/advanced_demo.cpp` (scx_advanced_demo), `backend/tests/{test_inventory_dp,test_supplier_selection,test_sorting_topological}.cpp`, `docs/NP_COMPLETENESS.md`, `docs/DAA_SYLLABUS_MAPPING.md`.
Phase 9: `backend/include|src/experiments/{Statistics,ExperimentRunner,ExperimentPresets,ExperimentIO}`, `backend/tools/experiments_main.cpp` (scx_experiments), `backend/tests/test_experiments.cpp`, `experiments/scripts/{analyze,plot_results}.py`, `experiments/results/*` (CSV, JSON, analysis.txt, plots/), `frontend/src/pages/ExperimentsPage.tsx`, `frontend/src/utils/experimentExport.ts`, `docs/EXPERIMENTAL_EVALUATION.md`.
Phase 10: `docs/{PROJECT_OVERVIEW,PROBLEM_STATEMENT,COMPLEXITY_ANALYSIS,CORRECTNESS_AND_TESTING,INSTALLATION_GUIDE,USER_MANUAL,VIVA_PREPARATION,LIMITATIONS_AND_FUTURE_WORK}.md`, `scripts/check_docs.py`, `frontend/src/utils/learn.test.ts`; four syllabus lessons added to the Learning Center; `scripts/start.ps1` fixed.
Multi-day: `backend/include|src/multiday/{MultiDayModel,DispatchPlanner,MultiDaySimulator,MultiDayPolicies,MultiDayScenarioBuilder,MultiDayJson}`, `backend/include|src/experiments/MultiDayExperiments`, `backend/tools/multiday_experiments_main.cpp` (scx_multiday_experiments), `backend/tests/{test_multiday_core,test_multiday_policies,test_multiday_experiments}.cpp`, multi-day cases in `backend/tests/test_api.cpp`, `experiments/scripts/{analyze_multiday,plot_multiday,tables_multiday}.py`, `experiments/results/multiday/` (CSV, analysis.txt, tables.md, plots/), `frontend/src/pages/MultiDayPage.tsx`, `frontend/src/utils/{multiday,multiday.test}.ts`, `docs/{MULTI_DAY_SIMULATION,FINAL_REPORT}.md`.
Docs: `docs/{MATHEMATICAL_MODEL,SYSTEM_ARCHITECTURE,ALGORITHMS,CPP_CONCEPTS_EXPLAINED}.md`.

## Tests executed
`ctest --test-dir build`: test_graph (5 cases), test_traversal_dijkstra (9), test_max_flow (7 cases, 25 checks), test_min_cost_flow (8 cases, 77 checks), test_recovery (10 cases, 609 checks), test_scenario_io (6 cases), test_simulation (16 cases, 454 checks), test_api (15 cases, 238 checks), test_inventory_dp, test_supplier_selection, test_sorting_topological, test_experiments (12 cases, 1564 checks) - 12/12 executables passed in the last full run.
Min-cost flow is verified against hand-computed optima AND exhaustive enumeration over 25 seeded random networks. A deliberate mutation (stop rule removed) was confirmed to fail the tests, then reverted and re-verified.

## Phase 4 tests (test_recovery: 10 cases, 609 checks)
Hand-checked totals (backup pays off: 50; not worth it: 100 and unmet demand reported; reopen route: 25), greedy provably suboptimal on a constructed case (56 vs exact 50), bottleneck term changes the choice (lambda=0 picks X total 40; lambda=1 picks Y total 42), illegal/duplicated/tampered plans rejected, and 40 seeded random networks checking: every plan valid, exact optimum <= every heuristic, every heuristic <= do-nothing, determinism.
Mutation checks (each confirmed to fail the tests, then reverted and re-verified): removing the lambda term; removing the "action must pay for itself" rule.
**Early, NOT conclusive:** on those 40 random cases greedy, bottleneck-aware (lambda=1) and lambda=0 each matched the exact optimum 37/40 times. No evidence yet that the bottleneck term helps; real evaluation is Phase 9.

## Phase 5 tests
* Scenario I/O: bundled file loads, JSON round trip (including reduced capacity / inactive routes), 10 kinds of malformed input each rejected with a message naming the problem, missing and corrupt files, event parsing by name or id.
* Disruptions: all six event types, invalid events (twice-failed, restoring a healthy element, spiking a non-customer, percent out of range, bad id, restoring a backup), atomicity (a failed event leaves state unchanged), capacity reduction idempotent from base capacity.
* Simulation: reset restores the pristine state exactly; checkpoint returns to the SAME disrupted state after executing a plan; invalid/tampered plans rejected with state unchanged; script stepping and rewind; metrics read disruptions from state.
* Scenario matrix: all 8 standard scenarios x all 6 strategies (exact skipped on the stress network): plans valid, cost accounting consistent, delivered + unmet = demand, greedy/bottleneck-aware/exact never worse than doing nothing, dispatch never better than min-cost flow, caller's network never mutated. `recovery_after_failure` with min-cost flow ends exactly at normal-operations cost.
* Reproducibility: same seed gives byte-identical scenario JSON and run results (timing excluded); different seed differs.
* Mutation check: disabling the reset made 4 checks fail; reverted and re-verified.

## Bug found by inspecting real numbers (not by a failing test)
The first generator version passed all tests but produced a "healthy" network with 15 units of unmet demand (a warehouse was fed only by an offline backup), so supplier failure changed cost by 1 and recovery never acted. Fixed: every warehouse is fed by a regular supplier, penalties exceed any path cost, and capacities/supply are scaled up (15% steps) until the healthy network's max flow covers all demand. Regression test `testHealthyGeneratedNetworksServeAllDemandAndDisruptionsBite` (15 seeds + 5 stress seeds) now guards this.

## Phase 6 tests
* `test_api` (182 checks): every endpoint and error path via `ApiService` (404/405/409/400/422, malformed JSON, wrong field types, unknown nodes/algorithms, path-traversal names, failed load keeps previous scenario, rejected events change nothing, preview vs execute, two kinds of reset, compare never mutates, CSV/JSON export, exact skipped when too large), allocation tracker measures a 2 MB allocation, real-socket test (health, content type, CORS allow/deny, preflight, malformed body), and the specification's 10-step workflow over HTTP.
* `scripts/smoke_test_api.ps1`: starts the real `scx_server.exe` (statically linked, 4.4 MB), runs the workflow from a separate process: 13/13 checks passed.
* Mutation check on the scenario-name validation: the FIRST version of the traversal test did not detect the mutation (`../secret` returned 400 anyway because the file does not exist). The test was strengthened with traversal names that resolve to a real file (`../scenarios/small_demo`), the mutation was then caught, and the code restored.

## Bugs found and fixed in Phase 6
* Heap corruption (0xC0000374) when the allocation hooks were combined with the dynamically linked libstdc++ (see CPP_CONCEPTS_EXPLAINED.md). Fixed by static linking for hooked executables.
* Two wrong test assumptions: stress_test loads with its events pending (not applied), so it has few candidate actions; and the compiler elided an unused test allocation. Tests corrected, not the server.
* Smoke script: CSV was parsed as JSON; error bodies are read via ErrorDetails on PowerShell 5.1.

## Phase 7 tests and findings
* Unit tests (Vitest, 15): API client (request shape, base URL, server error bodies, non-JSON errors, unreachable server, list unwrapping), layout (column order, no overlapping nodes, deterministic, empty network, canvas height bounds), disruption options (only targets the engine would accept; request bodies), formatting (unmeasured memory is never shown as 0).
* End-to-end (`npm run e2e`, 23 checks): real C++ server + built dashboard in real Chrome, following the specification's 10-step workflow through the UI (open, load, view graph with 9 nodes / 10 routes, two supplier failures drawn as failed, greedy executed, back to the same disrupted state, min-cost flow preview, 6-strategy comparison with charts, per-route flow labels, CSV export), plus BFS highlight, Dijkstra on an unreachable target, Learning Center complexity from the server, light and dark themes, and zero page/console errors.
* Screenshots were inspected by eye (not only checked programmatically) and found real problems that passing checks had not: a broken gray minimap slab in dark mode (removed; SVG fills cannot use CSS variables), a legend covering a node (moved below the canvas), recovery/min-cut highlights that faded the very flows being shown (now `dimOthers: false`), a decision graph squeezed into a narrow column at ~35% zoom (layout widened), over-tall canvases (auto height).
* Bugs found by the first e2e runs: Vite bound to IPv6 `localhost` only (pinned to 127.0.0.1); the comparison vanished when leaving the page (now kept in app state, keyed by the exact network state); the Lab defaulted its source to a failed supplier, giving an empty BFS (now the first active node); a state key based only on the NUMBER of applied events could show a stale comparison (now keyed on event descriptions).
* A Python edit script truncated NetworkGraph.tsx on a Unicode error (Windows default encoding); the file was rewritten in full and all later edits use explicit UTF-8.
* Dependencies installed in `frontend/node_modules` (126 MB): react 18.3.1, react-dom, @xyflow/react 12.12, recharts 2.15.4, vite 8, typescript, vitest, puppeteer-core (drives the already-installed Chrome; no browser download). Installed on the strength of the master specification naming these tools and the user's "continue" instruction.

## Phase 8 tests
* `test_inventory_dp` (275 checks): worked example by hand (cost 26, orders 2/5/0, spot-checked DP table entries), lot-for-lot comparison (29), strict independent policy evaluator, equality with brute force on 80 random problems (reconstructed policy re-costs to the DP value), edge cases (no days, no ordering allowed, demand above capacity), validation, refusal of oversized brute force.
* `test_supplier_selection` (503 checks): cost definition by hand, greedy provably suboptimal (15 vs 12), unit-cost vs penalty interaction, 40 random instances against an all-subsets oracle with and without pruning (pruning never changes the optimum, visits fewer nodes in total), validation, and the NP-completeness reduction checked twice: against brute-force subset sum (150 instances) and through the real recovery engine as an actual network (60 instances).
* `test_sorting_topological` (1507 checks): merge sort equals `std::stable_sort` on sizes 0..1000 with heavy ties, comparison / depth / merge-count bounds, special inputs, shipment extraction (records sum to the plan transport cost) and ordering; topological sort on DAGs, cycles, self-loops, disjoint cycles, 400 random graphs against a Floyd-Warshall oracle (every returned cycle verified as a real cycle), and the transport network rejected when it contains a loop.
* Mutation checks, each caught then reverted: merge sort made unstable (8 failing checks), unsound backtracking bound (21), holding cost dropped from the DP (5).
* `scx_advanced_demo` prints the worked examples from the real implementations.

## Honest notes on Phase 8
* Backtracking visited all 15 of 15 tree nodes on the tiny demo instance: the bound ignores future activation costs, so it is weak there. Savings show up only in aggregate over random instances.
* The NP-completeness result is WEAK NP-completeness (numbers in binary; a pseudo-polynomial DP exists but is not implemented). No claim of strong NP-hardness.
* These modules are standalone and tested; they are not wired into the API or dashboard (the specification asks for independent testability). Their numbers are therefore not part of the experiments.

## Phase 9 tests
* `test_experiments` (1564 checks): statistics by hand (mean, sample sd, Student-t half-width, paired comparison), every scenario kind does what it says (k largest suppliers fail, demand grows by x%, busiest quarter of routes lose capacity, size multiplier), per-run invariants over five scenario types (min-cost flow never worse than dispatch, greedy / bottleneck-aware never worse than re-routing, exact never worse than greedy, all valid, identical network for every strategy), aggregates and regret/wins recomputed independently from raw records, reproducibility (same seed gives identical costs, different seed differs), JSON parsing, CSV column counts (including skipped rows), JSON shape, preset validity, configuration validation.
* `test_api`: experiments endpoint returns measured aggregates with real heap figures, deterministic, 23 validation and limit requests (400 for bad input, 422 for too much work), live scenario untouched.
* Frontend: CSV export helpers and paired-difference wording (19 unit tests in total); the browser test now also runs a batch through the Experiment Lab, checks error-bar charts, seed reproducibility, the work-limit message, and a server-side validation error (30 checks in total).
* Mutation checks (each caught, then reverted): population sd instead of sample sd, flipped paired sign, changed demand-spike rounding, API work cap removed, regret formula broken, skipped-row CSV too short.
* Weak spots found in MY OWN tests while doing this, and fixed: a Windows-backslash path-traversal test had been silently mangled by my edit tooling into a different string, so it was not testing what it claimed (now fixed and proven to catch the mutation); two mutations I first reported as "caught" had not actually been applied, or were equivalent mutants; a regret/wins recomputation test was added because nothing had constrained those numbers. The shell tool used for edits collapses backslashes: file edits containing backslashes now go through the Edit tool, and all docs/sources were scanned for stray control characters (none).

## Phase 9 results (details in EXPERIMENTAL_EVALUATION.md; all measured, saved in experiments/results)
* Recovery actions vs re-routing alone: 21% lower mean cost over 200 random scenarios (-2,675 +/- 350 paired), up to 31% in the severity sweep; greedy never dearer in any run.
* Greedy vs exact optimum: +2.2% on average (+222 +/- 37 over 200 runs); greedy misses cost-saving actions when nothing is short (verified in saved plans) and is 23% above exact in the 18 route-failure runs where exact could run (cause not isolated).
* Dijkstra dispatch is consistently worse than min-cost flow (+258 +/- 122 even undisrupted; +4.7% in G).
* **The bottleneck-aware hypothesis is NOT supported:** no paired difference to greedy or to lambda = 0 excluded zero in 26 sweep settings, a 200-run experiment, or a 100-run ablation over lambda 0..5; 88% of lambda > 0 plans cost exactly the lambda = 0 plan. Not refuted in general (single period; only 3 backups; one scenario family).
* Scaling: runtime growth exponents (6 sizes, 27 to 216 nodes, 5 runs each) dispatch n^1.95, min-cost flow n^2.10, greedy n^3.05, bottleneck-aware n^3.13; heap about linear.
* A chart bug found by looking: exact's mean at 100% route loss was over only the 18 easier runs and looked 22% cheaper than everyone; such points are now omitted from the charts and flagged.

## Phase 10: final verification (from a clean state)
All generated artifacts were deleted first (`build/`, `frontend/dist`, `frontend/node_modules`, `frontend/e2e-output`), then everything was rebuilt by following `docs/INSTALLATION_GUIDE.md`:
* Backend: configure + build in 76 s with 0 warnings (`-Wall -Wextra -Wpedantic`); `ctest`: 12/12 executables, 108 cases, 5,349 checks, 0 failures (per-suite table in `docs/CORRECTNESS_AND_TESTING.md`).
* Frontend: `npm ci` (0 vulnerabilities), 22/22 unit tests in 4 files, production build OK (775 kB, 225 kB gzip).
* Process level: `smoke_test_api.ps1` 13/13; `npm run e2e` 31/31 (run twice); `start.ps1` checked with the default and a non-default API port.
* Reproducibility: experiments B, C, F and G re-run with the rebuilt binaries: 4,520 run records identical to the committed results in every non-timing column. `scx_advanced_demo` output matches the numbers quoted in the docs.
* Documentation: `scripts/check_docs.py` confirms all 15 required documents exist and all 174 path references resolve (and was shown to catch a deliberately bad reference).

Fixed during Phase 10:
* `start.ps1 -ApiPort N` started the server on N but the dashboard proxy still pointed at 8080 (found while writing the installation guide); now sets `SCX_BACKEND`, verified on port 9123.
* Learning Center now also teaches DP, backtracking, merge sort and topological sort (their complexity is stated by the lesson and labelled as such, since the server does not run them); a unit test keeps lessons complete; the page subtitle no longer claims every complexity comes from the server.
* Corrected my own earlier statements: the Phase 6 smoke count was 13 checks, not 14 (the 14th line is informational); three complexity entries were corrected after checking the code (greedy supplier selection is O(n^3 log n); Kahn with a heap is O(V log V + E); the 16-action limit gives HTTP 422 only in /algorithm/run); "identical plans" replaced by what was measured, "equal cost"; bundle size updated.

## Acceptance criteria (specification Part 19), with evidence
| Criterion | Status | Evidence |
|---|---|---|
| C++ backend builds successfully | met | clean build, 0 warnings |
| Graph algorithms execute correctly | met | test_graph, test_traversal_dijkstra (hand cases, disabled elements, unreachable targets) |
| Maximum-flow implementation passes tests | met | test_max_flow: textbook value 23, reverse arcs, zero capacity, disconnected, min cut = flow |
| Minimum-cost-flow implementation passes tests | met | test_min_cost_flow: hand optima + exhaustive enumeration on 25 random networks |
| Recovery algorithms produce validated plans | met | independent validator on every plan in tests and experiments (100% valid) |
| Disruption scenarios execute correctly | met | test_simulation: 8 standard scenarios x 6 strategies, all event types |
| Simulation results are reproducible | met | seeded tests; Phase 10 re-run identical in all non-timing columns |
| Frontend starts successfully | met | start.ps1 (dev) and vite preview in e2e |
| Frontend communicates with the C++ backend | met | e2e drives the real server; no JavaScript algorithm implementations |
| Interactive network visualisation works | met | e2e clicks nodes, counts nodes/routes, checks highlights; screenshots inspected |
| Disruption controls work | met | e2e applies events, resets; script step/start/pause implemented |
| Algorithm comparison uses actual results | met | /api/algorithms/compare runs the C++ strategies; e2e checks table and charts |
| Experimental charts use measured data | met | plots generated from CSV written by scx_experiments; Experiment Lab charts from the API |
| Complexity documentation matches implementations | met | COMPLEXITY_ANALYSIS.md checked against the code (3 corrections made) |
| Required DAA concepts are explained | met | ALGORITHMS.md, DAA_SYLLABUS_MAPPING.md, Learning Center (11 lessons) |
| C++ learning documentation is complete | met | CPP_CONCEPTS_EXPLAINED.md covers each concept listed in Part 13 |
| Final end-to-end demonstration passes | met (automated) | test_api 10-step workflow over HTTP; e2e through the UI; the live viva demo itself is scripted in VIVA_PREPARATION.md |
| Installation and execution instructions work | met | followed from a clean state in Phase 10 |
| Known limitations are documented | met | LIMITATIONS_AND_FUTURE_WORK.md |
| Final project report consistent with the implementation | met | `docs/FINAL_REPORT.md` built from the implemented algorithms and measured results (tables generated from the CSV files); check_docs.py plus manual review |

## Specification items NOT implemented (documented in LIMITATIONS_AND_FUTURE_WORK.md)
* Production events in the multi-day simulation (single product, no manufacturing, as specified for the extension).
* The manufacturing extension (production capacity, conversion): factories are a node type only; `processingCapacity` is not used by any algorithm.

## Design decisions
* Phase 3: dummy-shortage-source reduction replaced by delivery arcs with cost -p_j (the dummy version was unsound when customers transship). Inactive customers keep all demand unmet.
* Phase 4: recovery actions are only "activate backup supplier" and "reopen inactive route". Restoring a failed regular supplier is a simulation event (Phase 5), not a priced action. Both heuristics only accept actions that reduce unmet demand AND save more flow cost than they cost.

## Known issues / limitations
Heuristics are myopic (cannot see pairs of actions that only help together). Greedy/bottleneck-aware cost O(k^2) min-cost-flow solves for k candidates; fine for the sizes planned, to be measured in Phase 9. Memory usage is not measured yet (planned with the experiment runner). Many required docs (README, PROBLEM_STATEMENT, etc.) not yet written.

## Known limitations (frontend)
(Updated in Phase 10.) The JS bundle is 775 kB (225 kB gzip): no code splitting. Graph layout is a fixed layered layout (nodes are not draggable). Only Chrome was used for the browser test; Edge is the fallback path in the script but was not exercised. No mobile-specific testing beyond the responsive CSS.

## Multi-day findings (measured, `docs/EXPERIMENTAL_EVALUATION.md` Part 2)
* No actions is dearer than greedy in 21 of 25 settings; rolling exact is cheaper than greedy in 20 of 25 and never dearer.
* Look-ahead(8-12) is the cheapest tested policy (also cheaper than rolling exact on the same scenarios); look-ahead shorter than lead + supply lead never acts; decision time grows about h^1.9.
* Bottleneck-aware: no measurable difference in 29 of 29 settings; greedy and λ = 0 identical in every run. Risk signal: Spearman 0.41 with later shortage, but R² +0.012 beyond backlog. Hypothesis not supported.
* Perfect forecast lowers every policy's cost; rolling exact goes from 57% to 6% above the clairvoyant bound.

## Next exact action
None required: the multi-day extension is complete and verified. Optional future work (see `docs/LIMITATIONS_AND_FUTURE_WORK.md`): repeated/random disruptions in the multi-day model (the fairest test of the bottleneck-aware idea), safety stock in the planner, a look-ahead with a terminal value, the manufacturing extension.
