# System Architecture

Status: describes what exists today: all single-period phases plus the multi-day extension.

## Layers

```
frontend/ (React + TypeScript)  --JSON/HTTP-->  backend/src/api (cpp-httplib)
                                                      |
                 simulation (Phase 5) -> recovery + flow algorithms (Phases 3-4) -> graph (Phase 2)
                 multiday (simulator, planner, policies) -> FlowNetwork min-cost flow -> graph
```

| Module | Files | Status |
|---|---|---|
| Graph engine (nodes, edges, adjacency lists, validation) | `graph/SupplyChainGraph` | done |
| BFS, DFS, Dijkstra | `algorithms/{GraphTraversal,Dijkstra}` | done |
| Residual network, Edmonds-Karp, min-cost flow | `algorithms/{FlowNetwork,SupplyChainFlow}` | done |
| Plan validation, cost, metrics | `optimization/FlowSolution` | done |
| Recovery actions, validator, exact reference | `optimization/RecoveryPlan` | done |
| Greedy + bottleneck-aware recovery | `algorithms/RecoveryHeuristics` | done |
| Shortest-path dispatch baseline | `algorithms/DispatchBaseline` | done |
| Disruption engine | `simulation/Disruption` | done |
| Strategy registry | `simulation/Strategy` | done |
| Simulation state (reset, checkpoint, script stepping), metrics, scenario runner | `simulation/Simulation` | done |
| Scenario/event JSON, network/plan/metrics JSON export | `simulation/ScenarioIO` | done |
| Seeded network + disruption generator, 8 standard scenarios | `simulation/NetworkGenerator` | done |
| Heap measurement (replaced operator new, per-run peak) | `memory/`, `util/AllocationTracker` | done |
| HTTP API (cpp-httplib + nlohmann/json), see `API_REFERENCE.md` | `api/ApiService`, `api/HttpServer`, `main.cpp` | done |
| Dashboard (8 pages) | `frontend/` | done |
| Statistics, experiment runner, standard experiments A-G, CSV/JSON output | `experiments/{Statistics,ExperimentRunner,ExperimentPresets,ExperimentIO}`, `tools/experiments_main.cpp` | done |
| Experiment Laboratory page and `/api/experiments/run` | `frontend/src/pages/ExperimentsPage.tsx`, `api/ApiService` | done |
| DP, backtracking, merge sort, topological sort | `advanced/{InventoryDP,SupplierSelection,MergeSort,TopologicalSort}` | done |
| Multi-day model, time-expanded dispatch planner, simulator with forecast provider | `multiday/{MultiDayModel,DispatchPlanner,MultiDaySimulator}` | done |
| Multi-day policies (none, greedy, bottleneck-aware, look-ahead, rolling exact) and clairvoyant bound | `multiday/MultiDayPolicies` | done |
| Seeded multi-day scenarios (warm start, dated failures and restorations, demand noise and spike) | `multiday/MultiDayScenarioBuilder` | done |
| Multi-day experiments M1-M10, `/api/multiday/run`, JSON | `experiments/MultiDayExperiments`, `tools/multiday_experiments_main.cpp`, `multiday/MultiDayJson` | done |
| Multi-day Simulator page | `frontend/src/pages/MultiDayPage.tsx`, `frontend/src/utils/multiday.ts` | done |

## Key design decisions

* `scx_core` is a static library holding all logic. Tests and (later) the HTTP server link against it, so every algorithm is testable without a network.
* Graph ids are vector indices, stable forever. Disruptions only toggle `active` or lower `capacity`; `baseCapacity` is immutable.
* Edge usability is defined in one place (`SupplyChainGraph::isEdgeUsable`: edge active, capacity > 0, both endpoints active), so every algorithm agrees on what a disrupted network looks like.
* **Three states in `Simulation`:** `initial_` (never modified), `current_` (events and executed recovery change it), and an optional `checkpoint_` (copy of `current_` taken just before a recovery plan is executed). So: *Reset* = copy `initial_`; *Reset to the same disrupted state* = copy `checkpoint_`; planning (`planRecovery`) never mutates anything.
* **Disruptions vs recovery actions.** Disruptions are outside events (free, including `restore_*`). Recovery actions (activate a backup, reopen a route) are paid decisions. A plan is validated against the current state before it may be executed.
* **Atomic events.** An event is applied to a copy and swapped in only if it succeeds, so an invalid event never leaves partial changes.
* **Disruption metrics come from state, not from a counter:** failed/reduced/spiked elements are found by comparing `current_` with `initial_`, so they stay correct however the state was reached.
* **Controls the engine supports** (for the UI): apply a single event, load and step through an event script (`stepForward`; start/pause/resume are a timer around it), reset to initial, reset to disrupted state, plan with any strategy, execute a plan.
* Dependencies are vendored header-only in `backend/third_party` (nlohmann/json 3.11.3, cpp-httplib 0.18.3). Tests use a tiny in-repo helper (`tests/test_framework.h`); CTest runs each test executable.

## Scenario file schema

```json
{ "name": "...", "seed": 1,
  "nodes": [ { "name": "S1", "type": "supplier", "supply": 80, "unitPurchaseCost": 4 },
             { "name": "B1", "type": "supplier", "supply": 70, "isBackup": true, "activationCost": 40 },
             { "name": "C1", "type": "customer", "demand": 40, "shortagePenaltyPerUnit": 50 } ],
  "edges": [ { "from": "S1", "to": "W1", "capacity": 60, "costPerUnit": 2, "transitTime": 4, "reopenCost": 15 } ],
  "events": [ { "type": "supplier_failure", "node": "S1" } ] }
```

* `type`: `supplier | factory | warehouse | distribution_center | customer`. Omitted numeric fields are 0; `active` defaults to true (false for backups).
* Edges reference nodes by name. `capacity` alone is the design capacity; give both `baseCapacity` and `capacity` to describe an already-reduced route.
* Event types: `node_failure`, `supplier_failure` (must be a supplier), `route_failure {edge}`, `capacity_reduction {edge, percentRemaining 0-100}`, `demand_spike {node, percentIncrease}`, `restore_node`, `restore_route`. Nodes may be given by name or id; edges by id.
* All values must be integers. Loading is strict and raises `ScenarioError` with a message naming the offending item (bad type, unknown node, duplicate name, negative quantity, customer with supply, self-loop, ...).

## Strategies (all solve the same problem and are scored by the same objective)

`dijkstra_dispatch` (baseline), `min_cost_flow` (optimal shipments, no paid actions), `greedy`, `bottleneck_aware`, `bottleneck_aware_lambda0` (ablation), `exact` (exhaustive; small networks only). BFS/DFS/max-flow/Dijkstra are analysis tools and are deliberately not in this list.

## Standard scenarios (seeded generated network: 4 suppliers + 2 backups, 3 warehouses, 3 distribution centers, 6 customers)

`normal_operations`, `supplier_failure` (largest regular supplier), `route_failure` (busiest route in the optimal plan), `capacity_reduction` (same route to 30%), `demand_spike` (+50% everywhere), `multiple_disruptions` (supplier + busiest remaining route + spike), `recovery_after_failure` (fail then restore), `stress_test` (12+4 suppliers, 8 warehouses, 8 DCs, 30 customers, severity 0.5).

## Frontend (`frontend/`)

React 18 + TypeScript + Vite; React Flow for the graph, Recharts for charts, plain CSS with design tokens (`styles/theme.css`, light and dark, categorical colors from a validated palette in fixed order). No UI framework.

* **State:** one context (`hooks/useApp.tsx`) holds the server snapshot, the last comparison (tagged with the network state it was computed on, so it is never shown against a different state), highlights, and busy/error state. Every server call goes through one guard that turns failures into a visible message.
* **API client:** `services/api.ts` is typed against `types/api.ts` and takes `fetch` as a parameter so tests can fake the server. Unreachable server and non-JSON errors are reported with instructions rather than a raw exception.
* **Routing:** a small hash router (`#/network` etc.), no routing dependency.
* **Network graph:** `components/NetworkGraph.tsx`. Layered layout computed by the pure function `utils/layout.ts`. Failed nodes are struck through in red, offline backups dashed, blocked routes dotted, bottlenecks (>= 90% utilisation) orange with a triangle, highlighted results purple; node type is shown by a letter as well as color.
* **Experiment Laboratory:** configure a network, a scenario type, a number of seeded runs and strategies; shows mean, spread, min/max, paired differences with 95% intervals, runtime, heap and validity per strategy, with error-bar charts and CSV/JSON export. The server enforces work limits (see `API_REFERENCE.md`).
* **Multi-day Simulator:** configure a 5-30 day scenario and the policies; shows a summary per policy (cost, gap to the lower bound, fill rate, backlog, actions, recovery time only when defined), day-by-day line charts of service level (with the normal-service line), backlog, cumulative cost, goods in transit, stock, and demand against deliveries, and a day slider that redraws the network as it stood that day. Every value is from `/api/multiday/run`; the pure transformations are in `utils/multiday.ts` (unit-tested).
* **Tests:** unit tests with Vitest; `scripts/e2e.mjs` drives the built dashboard in real Chrome/Edge through `puppeteer-core` against the real C++ server.
