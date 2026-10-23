# Project Overview

**SupplyChainX: Adaptive Supply Chain Recovery and Optimization Simulator.** An algorithm-driven digital twin for supply-chain disruption analysis, recovery planning and optimization, built for CS3212 Design and Analysis of Algorithms.

## What it is

A supply chain is modelled as a directed, weighted, capacitated graph: suppliers (some of them offline backups), warehouses and distribution centers that pass goods on and may hold stock, customers with demand, and routes with a capacity and a cost per unit. You can load or generate such a network, disrupt it (a supplier fails, a route closes or loses capacity, demand spikes), and run algorithms on it to analyse the damage and plan a recovery. Every strategy is scored by the same objective, so their decisions, costs, running times and memory use can be compared on identical inputs.

The algorithms are the core of the project, not decoration around a database. All of them are implemented in C++17; the React dashboard only displays what the C++ engine returns.

## Components

| Component | What it does | Where |
|---|---|---|
| Graph engine | Nodes, edges, adjacency lists, availability, validation | `backend/*/graph` |
| Algorithms | BFS, DFS, Dijkstra, Edmonds-Karp max flow with min cut, min-cost flow, shortest-path dispatch, greedy and bottleneck-aware recovery, exhaustive recovery search | `backend/*/algorithms`, `backend/*/optimization` |
| Syllabus modules | Inventory dynamic programming, backtracking supplier selection, merge sort, topological sort | `backend/*/advanced` |
| Simulation | Disruption events, reset and checkpoint, scripted events, seeded network generator, eight standard scenarios, metrics | `backend/*/simulation` |
| Multi-day simulation | 5-30 days with transit times, stock, backorders, dated disruptions and their recovery; daily re-planning by a time-expanded min-cost flow; six recovery policies and a clairvoyant lower bound | `backend/*/multiday` |
| Experiments | Seeded batches with paired comparisons and 95% intervals; single-period experiments A to G and multi-day experiments M1 to M10 | `backend/*/experiments`, `experiments/` |
| API | Local JSON API with measured runtime and peak heap per run | `backend/*/api`, `scx_server.exe` |
| Dashboard | Overview, supply network graph, disruption simulator, algorithm lab, comparison, experiment lab, multi-day simulator, learning center | `frontend/` |

## The optimization objective

Minimize purchase cost + transport cost + holding cost of unused stock + shortage penalty on unmet demand + one-off cost of each recovery action, subject to route capacities, supply and stock limits and flow conservation. The formal model is in `MATHEMATICAL_MODEL.md`.

## What the project shows (from the measured experiments)

* Choosing paid recovery actions (activate a backup supplier, reopen a route) and then shipping optimally cuts mean cost by about 21% compared with re-routing alone over 200 random disruption scenarios.
* The greedy recovery heuristic is about 2% above the exact optimum on average, and measurably worse in specific situations.
* Allocating shipments with repeated shortest paths (Dijkstra alone) is consistently worse than network-wide min-cost flow.
* The experimental bottleneck-aware heuristic showed **no measurable improvement** over greedy. This negative result is reported as such, with its caveats.
* Choosing which suppliers to activate is NP-complete (weakly) in a restricted form, which is why the recovery strategies are heuristics with an exponential exact reference.
* Over 30 simulated days, recovery actions again cut cost strongly; per-day exhaustive choice of actions was cheaper than greedy in 20 of 25 settings; a look-ahead of 8-12 days was cheapest of all; and the bottleneck-risk term again made **no measurable difference** (its signal predicts later shortage, but almost nothing beyond today's backlog).

Details and uncertainty: `EXPERIMENTAL_EVALUATION.md`.

## Document map

| Document | Read it for |
|---|---|
| `README.md` | quick start and verification commands |
| `PROBLEM_STATEMENT.md` | the problem, inputs, outputs, scope |
| `MATHEMATICAL_MODEL.md` | decision variables, constraints, objective, reduction to min-cost flow |
| `SYSTEM_ARCHITECTURE.md` | modules, design decisions, scenario file schema |
| `ALGORITHMS.md` | every algorithm: pseudocode, dry run, correctness, complexity, limitations |
| `COMPLEXITY_ANALYSIS.md` | all complexities in one place, with measured growth |
| `NP_COMPLETENESS.md` | the hardness proof |
| `DAA_SYLLABUS_MAPPING.md` | where each course topic appears |
| `CORRECTNESS_AND_TESTING.md` | how correctness was established |
| `MULTI_DAY_SIMULATION.md` | the multi-day model: daily event order, planner, policies, recovery time, experiments, C++ components |
| `EXPERIMENTAL_EVALUATION.md` | method, measured results, threats to validity (Part 1 single-period, Part 2 multi-day) |
| `FINAL_REPORT.md` | the final report: algorithms, experiments and results in one document |
| `API_REFERENCE.md` | every HTTP endpoint |
| `INSTALLATION_GUIDE.md` / `USER_MANUAL.md` | installing and using it |
| `CPP_CONCEPTS_EXPLAINED.md` | the C++ ideas used in the code |
| `VIVA_PREPARATION.md` | likely questions with answers, and a demo script |
| `LIMITATIONS_AND_FUTURE_WORK.md` | what the project does not do |
| `PROJECT_PROGRESS.md` | development log, test results, known issues |
