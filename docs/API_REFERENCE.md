# API Reference

Local JSON API served by `scx_server` (default `http://127.0.0.1:8080`). Every number comes from the C++ engine. The server binds to loopback only and allows browser calls from `http://localhost:5173` / `http://127.0.0.1:5173` (the Vite dev server).

Start it: `build\backend\scx_server.exe --port 8080 --data-dir data` (statically linked: no DLLs needed).

## Conventions

* Requests: `POST` bodies are JSON objects (an empty body counts as `{}`). All integers must be integers.
* Errors: `{"error": {"code": "...", "message": "..."}}` with these statuses
  * `400` bad input (`invalid_json`, `missing_field`, `invalid_field`, `unknown_node`, `unknown_algorithm`, `unknown_strategy`, `invalid_scenario`, `invalid_request`)
  * `404` unknown endpoint, `405` wrong method
  * `409` state conflict (`no_scenario`, `nothing_to_undo`, `script_finished`)
  * `422` request understood but too large to run (`too_many_actions`: exhaustive search limited to 16 candidate actions)
  * `500` unexpected internal error
* A rejected request never changes server state (events are applied atomically; a failed scenario load keeps the previous scenario).
* Requests are processed one at a time (mutex), so timing and memory figures are not disturbed by concurrent calls.
* Timing: `runtimeMs` is wall-clock time of the algorithm call. `peakMemoryBytes` is the peak heap (bytes requested through `operator new`) above the level when the call started, or `null` if the allocation hooks are not linked. It excludes stack memory and allocator overhead.

## Endpoints

| Method + path | Purpose |
|---|---|
| `GET /api/health` | `{status, version, scenarioLoaded, memoryTrackingActive}` |
| `GET /api/algorithms` | catalog: id, title, category (`analysis` or `recovery`), description, time/space complexity, parameters |
| `GET /api/scenarios` | standard scenario names, bundled scenario files, strategy names |
| `POST /api/scenario/load` | one of `{standard, seed?}`, `{file}`, `{scenario:{...}}`, `{generate:{suppliers?, backupSuppliers?, warehouses?, distributionCenters?, customers?, edges?, capacityFactor?, demandMin?, demandMax?, seed?}, disruptionSeverity?}`. Returns a network snapshot. Scenario events become a script that is NOT applied yet. |
| `POST /api/scenario/reset` | `{to: "initial" \| "disrupted"}`. `disrupted` returns to the state just before the last executed recovery (409 if none). |
| `GET /api/network` | snapshot: `network` (nodes/edges with status, flow, utilisation), `metrics`, `flowSource`, `recoveryExecuted`, `appliedEvents`, `pendingEvents` |
| `POST /api/disruption/apply` | an event (`node_failure`, `supplier_failure`, `route_failure`, `capacity_reduction`, `demand_spike`, `restore_node`, `restore_route`; see SYSTEM_ARCHITECTURE.md) or `{step:true}` for the next scripted event |
| `POST /api/algorithm/run` | `{algorithm, ...}`. Analysis: `bfs`/`dfs` (`source`), `dijkstra` (`source`, `target?`), `max_flow`. Recovery: `min_cost_flow`, `dijkstra_dispatch`, `greedy`, `bottleneck_aware` (`lambda?`, `saturationThreshold?`), `bottleneck_aware_lambda0`, `exact`; with `execute: true` the plan is applied to the live scenario. |
| `POST /api/algorithms/compare` | `{strategies?: [...], lambda?, saturationThreshold?}`: same disrupted network, same objective, every listed recovery strategy (default all). Only recovery strategies are accepted: BFS/Dijkstra/max flow answer different questions. `exact` is reported as skipped when there are more than 16 candidate actions. Never changes the scenario. |
| `GET /api/results[?format=csv]` | one row per run/compare so far (scenario, algorithm, cost, unmet demand, fulfilment, utilisation, runtime, memory); CSV or JSON export |

### `POST /api/experiments/run`

Runs a seeded batch of scenarios and returns measured aggregates (and every raw run). In each run the network is generated from seed `seed + run`, a disruption is applied, and every selected strategy solves the **identical** disrupted network.

Request fields (all optional): `kind` (`random_disruption` | `supplier_failures` | `demand_spike` | `route_capacity_loss` | `network_size`), `x` (the swept value; its meaning depends on `kind`, see `backend/include/experiments/ExperimentRunner.h`), `severity` (used by `network_size`), `runs`, `seed`, `strategies` (names, default all), `lambda`, `saturationThreshold`, `timingRepeats`, `exactMaxCandidates`, `pairedAgainst` (a selected strategy; default `greedy` if selected), and `network` (`suppliers`, `backupSuppliers`, `warehouses`, `distributionCenters`, `customers`, `edges`, `capacityFactor`, `demandMin`, `demandMax`).

Response: per strategy `cost`, `unmetDemand`, `fulfilment`, `utilisation`, `runtimeMs`, `actions` (each `{n, mean, stddev, min, max, ci95}`), `feasibilityRate`, `meanRegretPercent`, `wins`, `peakMemoryBytes` (or `null`), and `pairedVs` (mean paired cost difference with its 95% interval against `pairedAgainst`); plus `records` (one row per run and strategy), `meanNodes`, `meanEdges`, `totalRuntimeMs`.

Errors: `400` for any bad field (type, range, unknown kind or strategy, unusable network); `422 too_much_work` when the request exceeds the limits: at most 200 runs, 400 nodes, 5 timing repeats, and `runs × nodes ≤ 6000` (use the `scx_experiments` tool for larger batches). The live scenario is never touched.

The seven standard experiments (A to G) are run from the command line: `build\backend\scx_experiments.exe` (results in `experiments/results`, evaluation in `docs/EXPERIMENTAL_EVALUATION.md`).

### `POST /api/multiday/run`

Runs one multi-day simulation (`docs/MULTI_DAY_SIMULATION.md`): a seeded scenario is built once and every selected policy runs on it with the same initial state, disruption schedule, demand, costs and forecast. Never touches the live single-period scenario.

Request fields (all optional): `seed`, `horizon` (5-30 days, default 20), `transitMin`, `transitMax` (days), `demandAmplitude` (0-1), `initialStockDays`, `penaltyScale`, `warmStart` (default true), `network` (`suppliers`, `backupSuppliers`, `warehouses`, `distributionCenters`, `customers`, `capacityFactor`), `spike` (`percent`, `start`, `duration`), `supplierFailures` (`count`, `day`, `duration`; 0 = until the end), `routeFailures` (`count`, `day`, `duration`, `capacityLossPercent`), `forecast` (`persistence` | `perfect`), `planningWindowDays`, `activationLeadDays`, `repairLeadDays`, `backlogPenaltyFraction`, `policies` (names from `none`, `greedy`, `bottleneck_aware`, `bottleneck_aware_lambda0`, `lookahead`, `rolling_exact`; default all), `lambda`, `lookaheadDays`, `maxExactCandidates`, `clairvoyant` (default true), `clairvoyantMaxCandidates`.

Response: `scenario` (nodes, routes with `transitDays`, dated `events`, `demandPerDay`, `firstDisruptionDay`, `supplyLeadDays`, the `planningWindowDays` actually used, thresholds), `normalService` (per day, the no-disruption service level), `policies` (per policy: `totals` {totalCost, settlement, actionCost, actions, totalDemand, totalServed, finalBacklog, finalInTransit, finalStock, fillRate, decisionMs, runtimeMs, feasible}, `recovery` {applicable, recovered, days, recoveryDay} where `days` is `null` unless the recovery definition was met, and `days`: one record per day with demand, due, served, newBackorders, backlog, stock, inTransit, serviceLevel, cost by component, cumulativeCost, actions, events, activeDisruptions, decision/planning time, plannedSaturatedShare, and per-node/per-route snapshots `stockByNode`, `backlogByNode`, `dispatchedByEdge`, `inTransitByEdge`, `capacityByEdge`, `nodeActive`, `edgeUsable`), `clairvoyant` (the full-information lower bound, or `computed: false` with a reason), `lowestTotalCost`, `totalRuntimeMs`, `limits`.

Errors: `400 invalid_field` for a bad type or range (including a horizon outside 5-30), `400 invalid_scenario` when the parameters cannot form a scenario, `422 too_much_work` above 60 nodes, a look-ahead above 12 days, or exhaustive searches above 10 candidate actions (use `scx_multiday_experiments` for larger studies), `500 internal_error` if a simulation invariant is ever violated.

The ten standard multi-day experiments (M1 to M10) are run with `build\backend\scx_multiday_experiments.exe` (results in `experiments/results/multiday`).

## Testing

* `backend/tests/test_api.cpp`: every endpoint and error path through `ApiService`, plus a real-socket test (CORS, preflight, malformed bodies) and the specification's 10-step workflow over HTTP.
* `scripts/smoke_test_api.ps1`: starts the real `scx_server.exe` and runs the workflow from a separate process.
