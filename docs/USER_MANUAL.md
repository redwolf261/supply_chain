# User Manual

Start the system as described in `INSTALLATION_GUIDE.md` (usually `scripts\start.ps1`) and open `http://127.0.0.1:5173`. The sidebar shows whether the C++ engine is connected and whether heap measurement is on, and has the theme switch (system, light, dark). Every number in the dashboard comes from the C++ engine.

## 1. Load a scenario (Overview page)

Use **Load a scenario** at the bottom of the Overview page (and at the top before anything is loaded):

* **Standard**: one of eight prepared scenarios (normal operations, supplier failure, route failure, capacity reduction, demand spike, multiple disruptions, recovery after failure, stress test) with a random seed. The scenario's disruptions are queued as a script, not applied yet.
* **Bundled file**: a JSON file from `data\scenarios` (for example `small_demo`). The format is described in `SYSTEM_ARCHITECTURE.md`; you can add your own files there.
* **Generate**: a new layered network from the numbers of suppliers, backups, warehouses, distribution centers, customers and routes, a capacity factor, a seed, and a disruption severity (which queues a random disruption script).

The Overview then shows supplier, factory, warehouse and customer counts, total, fulfilled and unmet demand, network utilisation, total cost, the number of active disruptions, a cost breakdown, and the applied and queued events. The figures describe the current network with optimal re-routing (min-cost flow) unless a recovery plan has been executed.

## 2. Supply Network page

An interactive directed graph: suppliers on the left, customers on the right.

* **Navigate:** scroll to zoom, drag to pan, the + / − / fit buttons at the bottom left.
* **Inspect:** click a node or a route label to see its details on the right; hover for a tooltip.
* **Read the styling:** each node shows its type letter (S, F, W, D, C) and colour. Failed nodes are red and struck through; offline backups are dashed; routes show `flow/capacity · $cost`; a blue route carries flow, an orange route with ▲ is a bottleneck (at least 90% utilised), a red dashed route has failed, a dotted route is blocked by a failed endpoint, and purple marks the result highlighted from the Algorithm Lab. The legend is under the graph.
* **Bottlenecks** lists the routes at or above 90% utilisation.
* **Reset network** returns to the initial state. **Undo recovery** (shown after a plan was executed) returns to the disrupted state just before it.

## 3. Disruption Simulator

* **Apply a disruption:** choose an event (shut down a supplier or facility, disable a route, reduce route capacity, increase demand, restore a node or route), a target, and a percentage where relevant, then **Apply event**. Only targets the engine would accept are offered (for example only failed routes can be restored). Restoring is free; bringing an offline *backup* supplier online is a paid recovery action and is done in the Algorithm Lab instead.
* **Scripted events:** **Start** plays the queued events at about one per second, **Pause** / **Resume** control it, **Step forward** applies one event.
* **Reset scenario** restores the initial network and rewinds the script.

The tiles show the effect immediately (active disruptions, unmet demand, cost, active suppliers), and the graph below shows the network.

## 4. Algorithm Lab

Choose an algorithm, set its parameters, and **Run**. The result card shows runtime and peak heap, and the graph on the right draws the decision.

| Algorithm | Parameters | What you see |
|---|---|---|
| BFS, DFS | source | visit order (DFS also finish order), fewest hops, what the disruption cut off since the initial network |
| Dijkstra | source, optional target | cost to every node, the cheapest path (or "unreachable") |
| Maximum flow | none | maximum deliverable, the minimum cut (bottleneck routes), supply- or demand-limited nodes |
| Recovery strategies (min-cost flow, dispatch, greedy, bottleneck-aware, exact) | λ and saturation threshold for bottleneck-aware | total cost, unmet demand, actions chosen with their cost, the decision trace (score of each step), validity |

**Run** is a preview: the scenario is unchanged. **Run and apply to scenario** pays for the plan's actions and updates the live network (the Supply Network page then shows the executed plan's flows). Exhaustive search refuses networks with more than 16 candidate actions.

## 5. Comparison

Tick the strategies, set λ, and **Run comparison**. Every strategy solves the same disrupted network and is scored by the same objective, so total costs are directly comparable; BFS, DFS, Dijkstra and max flow are not offered because they answer different questions. You get a table (total cost, delivered and unmet demand, fulfilment, utilisation, actions, runtime, peak memory, validity, with the lowest-cost strategy marked), four bar charts, before/after graphs for any strategy, a run history, and **Export CSV / Export JSON**. A comparison is shown only while the network is in the state it was computed for.

## 6. Experiment Lab

Runs a batch of seeded random scenarios; in each run every selected strategy solves the identical disrupted network.

1. Choose a **scenario type** and its value (severity, number of failed suppliers, demand increase, capacity lost on the busiest routes, or network size multiplier).
2. Set the **network** (layer sizes, routes, capacity factor, demand range), the **number of runs**, the **seed**, λ, and the strategies. Exhaustive search is off by default and is skipped in runs with more than 10 candidate actions.
3. Choose which strategy the others are **compared against**, and **Run experiments**. The button is disabled, with the reason shown, above the server's limits (at most 200 runs, 400 nodes, and runs × nodes ≤ 6000; use `scx_experiments.exe` for bigger batches).

How to read the results table:

* **Total cost (mean ± sd)** and **min – max** over the runs; **Unmet**, **Fulfilment**, **Utilisation**, **Runtime**, **Peak memory** are means.
* **Cost − reference (95%)**: the average cost difference to the reference strategy over the same runs, with its 95% interval. Negative means cheaper. ● the interval excludes zero (a real difference is likely); ○ it includes zero (it could be noise). Hover for the number of runs cheaper, dearer and equal.
* **Regret**: average percentage above the cheapest strategy in the same run. **Lowest in**: runs in which the strategy tied for the lowest cost. **Valid**: share of plans accepted by the independent validator.

The charts show mean cost and mean runtime with 95% intervals. Export the raw runs or the summary as CSV, or everything as JSON. The same seed always reproduces the same costs.

## 6b. Multi-day Simulator

Simulates 5 to 30 days. Goods take days to travel, stock and unserved orders (backlog) carry over from day to day, and disruptions start and end on given days. Every selected policy runs on the same scenario. The model is described in `MULTI_DAY_SIMULATION.md`.

1. **Scenario:** horizon, seed, shortest and longest transit time per route, daily demand noise, and warehouse stock (in days of demand).
2. **Network:** layer sizes and capacity factor (at most 60 nodes in the dashboard).
3. **Disruptions:** how many of the largest regular suppliers fail, on which day and for how long; how many of the busiest routes fail or lose capacity; an optional demand spike.
4. **Policies**, the look-ahead horizon (1-12 days), λ for bottleneck-aware, and **what policies may know**. *Today only (persistence)*: a policy assumes today's network and demand continue, and does not know when a failure will end. *The true schedule (perfect)*: it sees the real future events and demand inside its planning window.
5. **Run simulation.**

How to read the results:

* **Summary table.** Total cost includes the shortage penalty for every unit still owed after the last day. *Above bound* compares with the clairvoyant lower bound (perfect knowledge of the whole horizon); no policy can go below it, and the gap includes the value of knowing the future. *Fill rate* counts late deliveries; lateness is charged as backlog cost. *Recovery time* is shown only when service came back to normal (at least min(95%, the undisrupted service)) for 3 consecutive days inside the horizon; otherwise it says "not recovered within N days".
* **Charts** (one line per policy): daily service level (with the dashed *normal* line of the same scenario without disruptions), backlog, cumulative cost, goods in transit, stock, and demand and deliveries for the policy chosen below. Shaded days have at least one supplier or route down. Lines that lie on top of each other mean the policies made the same decisions, and that is itself a result.
* **Day view.** Choose a policy and move the day slider. The tiles show delivered/owed, backlog, stock and goods in transit, cost, and active disruptions. The text lists that day's events, the actions decided and the cost split. The network shows failed suppliers and routes. Route labels read *dispatched today / capacity today · units still moving*, and customer cards show the units owed.

## 7. Learning Center

One lesson per algorithm: what it does, why it is used, where it is used here, how it works, pseudocode, the C++ in this project, a small dry run, complexity and limitations. Complexities of the algorithms the dashboard runs are read from the server; the syllabus modules (DP, backtracking, merge sort, topological sort) state theirs from the analysis.

## 8. Command-line tools

| Command | What it does |
|---|---|
| `build\backend\scx_server.exe [--port N] [--data-dir PATH]` | the API server (127.0.0.1 only) |
| `build\backend\scx_experiments.exe [--out DIR] [--only A-G] [--runs-scale F] [--list]` | runs the standard experiments and writes CSV/JSON |
| `build\backend\scx_multiday_experiments.exe [--out DIR] [--only M1-M10] [--runs-scale F] [--list]` | runs the ten multi-day experiments and writes CSV to `experiments\results\multiday` |
| `build\backend\scx_advanced_demo.exe` | worked examples: DP table, backtracking vs greedy, merge sort of shipments, topological order and a detected cycle |
| `python experiments\scripts\analyze.py` | paired comparisons and growth exponents from the saved results |
| `python experiments\scripts\analyze_multiday.py`, `plot_multiday.py` | multi-day analysis (paired comparisons, risk-signal test, look-ahead runtime) and charts |
| `python experiments\scripts\plot_results.py` | charts from the saved results into `experiments\results\plots` |

The HTTP endpoints are documented in `API_REFERENCE.md`.

## 9. A typical session (the specification's workflow)

1. Open the dashboard. 2. Load `small_demo` (Bundled file). 3. Look at the Supply Network page. 4. In the Disruption Simulator shut down suppliers S1 and S2. 5. In the Algorithm Lab run Greedy recovery and **Run and apply to scenario** (it activates the backup S3). 6. On the Supply Network page click **Undo recovery** to return to the same disrupted state. 7. Run Minimum-cost flow in the Lab (preview). 8. Run the Comparison. 9. Look at the updated flows on the Supply Network page. 10. Export the results from the Comparison page.

## 10. Messages you may see

| Message | Meaning |
|---|---|
| "Cannot reach the SupplyChainX server" | the API is not running; start it |
| "no scenario is loaded" | load one on the Overview page |
| "... is not a supplier", "route is already inactive", etc. | the engine rejected the event; the network is unchanged |
| "exhaustive search needs 2^k solves ..." | too many candidate actions for exact search on this network |
| "runs x nodes must be at most 6000 ..." | the experiment is too large for one request; reduce it or use `scx_experiments.exe` |
| "'horizon' must be between 5 and 30 days" | the multi-day simulator accepts 5 to 30 days |
| "networks above 60 nodes are not accepted here" | multi-day runs from the dashboard are limited to 60 nodes; use `scx_multiday_experiments.exe` |
