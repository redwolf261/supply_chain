# SupplyChainX

**An algorithm-driven digital twin for supply-chain disruption analysis, recovery planning and optimization** (CS3212, Design and Analysis of Algorithms).

The supply chain is a directed, capacitated, weighted graph. You can load or generate a network, disrupt it, run classic graph and flow algorithms plus recovery heuristics on it, and compare their decisions, costs, runtime and memory. All algorithms are implemented in C++17; the React dashboard only displays what the C++ engine returns.

> Status: all ten development phases and the multi-day simulation extension are complete and were verified from a clean build (see `PROJECT_PROGRESS.md`). Not implemented: the manufacturing extension; see `docs/LIMITATIONS_AND_FUTURE_WORK.md`. Nothing in this README describes functionality that does not exist. The final report is `docs/FINAL_REPORT.md`.

## What is implemented

| Area | Content |
|---|---|
| Graph algorithms | BFS, DFS (directed reachability, failure impact), Dijkstra |
| Network flow | Edmonds-Karp max flow with minimum cut, minimum-cost flow (successive shortest paths with potentials) |
| Recovery | Greedy, experimental bottleneck-aware heuristic (configurable λ, ablation at λ = 0), shortest-path dispatch baseline, exhaustive reference for small cases, independent plan validator |
| Simulation | Six disruption types, reset / checkpoint, scripted events, seeded network generator, eight standard scenarios |
| Syllabus modules | Inventory dynamic programming, backtracking supplier selection, merge sort, topological sort (standalone, tested; `build\backend\scx_advanced_demo.exe` prints worked examples), NP-completeness proof in `docs/NP_COMPLETENESS.md` |
| Multi-day simulation | 5-30 days with transit times, stock, backorders, dated disruptions and recovery; daily re-planning by a time-expanded min-cost flow; greedy, bottleneck-aware, look-ahead and per-day exhaustive policies; clairvoyant lower bound; recovery time (`docs/MULTI_DAY_SIMULATION.md`) |
| API | Local JSON API with measured runtime and peak heap per run |
| Experiments | Seeded batch runner with paired comparisons, 95% intervals, runtime and heap; the seven standard experiments A to G and the ten multi-day experiments M1 to M10 with saved results and charts (`docs/EXPERIMENTAL_EVALUATION.md`) |
| Dashboard | Overview, interactive network graph, disruption simulator, algorithm lab, strategy comparison, experiment laboratory, multi-day simulator (day-by-day timelines and network snapshots), learning center; dark and light themes |

## Quick start (Windows)

Requirements: g++ with C++17 (MinGW-w64), CMake, Ninja, Node.js and npm. Versions used and tested: g++ 15.2, CMake 4.2, Node 24 / npm 11 (other versions are untested).

```powershell
# 1. Build and test the C++ backend
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

# 2. Install, test and build the dashboard
cd frontend
npm install
npm test
npm run build
cd ..

# 3. Run everything (API on :8080, dashboard on :5173)
powershell -ExecutionPolicy Bypass -File scripts\start.ps1
```

Open <http://127.0.0.1:5173>, load a scenario on the Overview page, and explore. `scripts\start.ps1 -Production` serves the built dashboard on port 4173 instead.

To run only the API: `build\backend\scx_server.exe --port 8080 --data-dir data` (see `docs/API_REFERENCE.md`).

## Verification commands

| Command | What it checks |
|---|---|
| `ctest --test-dir build` | 15 C++ test executables: algorithms, flow, recovery, simulation, scenario I/O, API, syllabus modules, experiments, multi-day simulation, policies and experiments |
| `powershell -File scripts\smoke_test_api.ps1` | starts the real server, runs the specification's workflow over HTTP |
| `cd frontend; npm test` | frontend unit tests (API client, layout, disruption options, formatting, experiment CSV export, lesson completeness) |
| `cd frontend; npm run e2e` | real browser (installed Chrome/Edge) drives the dashboard against the real server through the 10-step workflow; writes screenshots to `frontend/e2e-output` |

## Layout

```
backend/   C++17 core library (graph, algorithms, optimization, simulation, api), server, tests
frontend/  React + TypeScript + Vite dashboard
data/      scenarios (JSON)
docs/      mathematical model, architecture, algorithms, API reference, C++ concepts, screenshots
scripts/   start.ps1, smoke_test_api.ps1
```

New here? Read `docs/PROJECT_OVERVIEW.md` first (it maps every document). Then `docs/MATHEMATICAL_MODEL.md` (what is being optimized), then `docs/ALGORITHMS.md`, then `docs/EXPERIMENTAL_EVALUATION.md` (what was measured).

## Experiments

```powershell
build\backend\scx_experiments.exe --out experiments\results      # runs A-G, about 85 seconds
python experiments\scripts\analyze.py > experiments\results\analysis.txt
python experiments\scripts\plot_results.py                        # charts in experiments\results\plots
```

Multi-day experiments (about 16 minutes):

```powershell
build\backend\scx_multiday_experiments.exe --out experiments\results\multiday
python experiments\scripts\analyze_multiday.py ; python experiments\scripts\tables_multiday.py ; python experiments\scripts\plot_multiday.py
```

The committed results in `experiments/results` were produced this way. Headline findings (details, uncertainty and caveats in the evaluation document): recovery actions cut mean cost by about 21% against re-routing alone; greedy is about 2% above the exact optimum on average; and the experimental bottleneck-aware heuristic showed **no measurable improvement** over greedy. Over 30 simulated days: per-day exhaustive choice of actions beat greedy in 20 of 25 settings, a look-ahead of 8-12 days was cheapest of all, and the bottleneck-aware heuristic again made no measurable difference (29 of 29 settings).

## Screenshots

`docs/screenshots/` contains images produced by the end-to-end test run.
