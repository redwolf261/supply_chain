# Multi-Day Simulation

Status: **implemented and tested.** Sections 1-12 were written as the design before any code; they have since been corrected where the implementation taught us something (the warm start in section 10, the minimum planning window in section 6, the normal-service rule in section 9, and the attainability note in section 8). Section 13 defines the experiments and section 14 explains each C++ component. Measured results are in `EXPERIMENTAL_EVALUATION.md` (part 2) and `FINAL_REPORT.md`.

Code: `backend/include/multiday/` and `backend/src/multiday/` (model, planner, simulator, policies, scenario builder, JSON), `backend/src/experiments/MultiDayExperiments.cpp`, `backend/tools/multiday_experiments_main.cpp`. Tests: `backend/tests/test_multiday_core.cpp`, `test_multiday_policies.cpp`, `test_multiday_experiments.cpp`, and the multi-day cases in `test_api.cpp`.

The single-period model (`MATHEMATICAL_MODEL.md`) answers "what should we ship today?". It cannot show consequences over time: a backup supplier activated today that only helps next week, goods that are on a truck and not yet usable, customers who wait instead of being lost. The multi-day simulation adds time while keeping the same single-product network, the same cost parameters, and the same recovery actions.

## 1. Time, units and the horizon

* Days `t = 0, 1, ..., H-1`, with horizon `H` between 5 and 30.
* All quantities are integer units of the one product; all costs are integer currency units.
* Each node keeps its single-period fields, reinterpreted **per day**:
  * `supply`: units a (regular or activated backup) supplier can produce and ship **per day**. Unused supply does not carry over: it is a capacity, not stock.
  * `capacity` of a route: units that may **depart** on that route **per day**. Capacity resets every day.
  * `inventory`: stock on hand at the start of day 0.
  * `holdingCostPerUnit`: charged per unit of stock held at the **end** of each day.
  * `shortagePenaltyPerUnit` (`p_j`) at customers: see backorders below.
* New per-route field (held by the multi-day scenario, not by the graph): **`transitDays[e] >= 0`**, the number of days between departure and arrival.
* Demand: a fixed sequence `demand[j][t]` per customer and day, computed once when the scenario is built and identical for every policy.

## 2. State

The state at the start of day `t`:

| Part | Type | Meaning |
|---|---|---|
| `network` | `SupplyChainGraph` | which nodes and routes are active, per-day route capacities (disruptions applied) |
| `stock[i]` | integer ≥ 0 per node | goods physically at node `i` (warehouses, distribution centers, also suppliers and customers if goods end up there) |
| `backlog[j]` | integer ≥ 0 per customer | demand from earlier days not yet served (backorders) |
| `pipeline` | list of shipments `(route, quantity, departureDay, arrivalDay)` | goods in transit; **not** part of any stock |
| `pending` | list of recovery actions `(action, decidedDay, effectiveDay)` | paid actions that have not taken effect yet |
| totals | counters | cumulative supplied, consumed, costs, metrics |

## 3. Backorder semantics (chosen precisely)

Unserved demand is **backordered**: the customer waits.

* On day `t` customer `j` is **owed** `due_j(t) = backlog_j(t) + demand_j(t)`.
* Goods on hand at the customer are used immediately: `served_j(t) = min(stock_j, due_j(t))`. A policy cannot withhold goods from a customer.
* `backlog_j(t+1) = due_j(t) - served_j(t)`.
* **Backlog cost**: every unit still owed at the **end** of a day costs `b_j` for that day, with `b_j = max(1, round(backlogPenaltyFraction × p_j))` (default fraction 0.2). A unit served late by `k` days costs exactly `k · b_j`.
* **End-of-horizon settlement**: units still owed after day `H-1` are treated as lost and cost `p_j` each, **once**.
* So each unit-day of waiting is charged once, and each unit is charged the lost-sale penalty at most once: no double counting.

Without the settlement, a policy could postpone service until the horizon ends and escape the cost; the settlement removes that end-of-horizon effect.

## 4. The daily event order

Every policy is simulated with exactly this order:

1. **Arrivals.** Shipments with `arrivalDay == t` are added to their destination's stock and removed from the pipeline (so each arrives exactly once).
2. **Scheduled events.** Disruptions and natural recoveries scheduled for day `t` are applied: node failure and restore, route failure and restore, capacity reduction.
3. **Completed actions.** Pending recovery actions with `effectiveDay == t` take effect (a backup becomes active; a failed route is reopened). They are applied after the scheduled events of the same day.
4. **Today's capacities.** Supply per supplier and capacity per route are set from the current network. Nothing carries over from yesterday.
5. **Observe.** Today's demand is revealed; `due = backlog + demand`.
6. **Recovery decision.** The policy may choose recovery actions from the legal candidates. Their cost is charged today; each takes effect on `t + leadDays` (activation lead and repair lead are configurable, default 1). With a lead of 0 the action takes effect immediately, before planning.
7. **Dispatch plan.** The common planner (section 6) computes today's shipments, using the observed state, the policy's information (section 7) and the policy's pending actions.
8. **Validation.** The plan is checked (section 5). An invalid plan is a program error and stops the simulation; it is never silently repaired.
9. **Dispatch.** Supply is drawn and goods leave their origins. A shipment on a route with `transitDays ≥ 1` enters the pipeline with `arrivalDay = t + transitDays`. **Zero-day transit** routes deliver within the same day: the goods are at the destination before step 10 and may already have been planned to move on along another zero-day route the same day.
10. **Fulfilment** (section 3).
11. **Backlog update.**
12. **Daily cost:** purchase (units drawn × unit cost) + transport (units dispatched × cost per unit) + holding (end-of-day stock × holding cost, every node) + backlog cost (end-of-day backlog × `b_j`) + recovery actions decided today.
13. **Metrics** recorded; `t ← t + 1`. After day `H-1` the end-of-horizon settlement is added.

Same-day shipments therefore exist only on zero-day routes. Goods in transit are never counted as stock, delivered, or available.

## 5. Correctness conditions checked every day

* Dispatch on a route ≤ its capacity today; 0 on a route that is inactive or touches an inactive node.
* Supply drawn at a node ≤ its supply today (0 if inactive or an unactivated backup).
* For every node: `stock + drawn + same-day inflow − outflow − served = end stock ≥ 0`. Inventory never becomes negative.
* **Conservation of goods**, checked after every day:
  `initial stock + initial pipeline + total drawn = Σ stock + Σ pipeline quantity + total served`
  (the initial pipeline is the warm start of section 10).
  A violation stops the simulation.
* Every pipeline shipment arrives exactly `transitDays` after departure, and exactly once.
* Each recovery action is charged once and cannot be bought twice.

## 6. The dispatch planner (time-expanded minimum-cost flow)

Daily replanning uses a **rolling horizon**: each day, plan the next `W` days (default 7, clipped at the end of the horizon), execute only today's shipments, and plan again tomorrow with fresh observations.

**Minimum window (found during implementation).** A unit bought today reaches a customer only after the supply lead `S` = the largest, over suppliers, of the shortest transit time to any customer. If `W ≤ S` the planner never sees a delivery of anything it buys today, so it stops buying and the pipeline runs dry: with 3 transit days on every route (paths of 9 days) a healthy network served only about 71% of demand in a trial run. The simulator therefore raises the window to at least `max(activation lead, repair lead) + S + 1` (`minimumPlanningWindow`, tested) and reports the window it actually used. This is the multi-day counterpart of the single-period min-cost flow and is shared by every policy, so policies differ only in which recovery actions they choose.

The planning network has a copy of every node for every day in the window. For node `i` and day `τ` call it `(i, τ)`, plus a source `σ` and a sink `ω`:

| Arc | Capacity | Cost per unit | Meaning |
|---|---|---|---|
| `σ → (i, t)` | current stock | −M | stock on hand must be accounted for |
| `σ → (j, τ)` | pipeline units arriving at `j` on day `τ > t` | −M | known arrivals |
| `σ → (i, τ)` | expected supply on day `τ` | `a_i` | production |
| `(i, τ) → (k, τ + L_e)` for route `e = (i, k)` if `τ + L_e` is in the window | expected capacity on day `τ` | `c_e` | shipment |
| `(i, τ) → (i, τ+1)` | unlimited | `h_i` | hold stock overnight |
| `(i, last) → ω` | unlimited | `h_i` | stock left at the end of the window |
| `(j, τ) → ω` at a customer | units owed on day `τ` (today: backlog + demand) | `−R_{j,τ}` | serve |
| `(j, τ+1) → (j, τ)` at a customer | unlimited | `b_j` | serve day-`τ` demand one day late |

`M` is a large constant, so all stock and arriving pipeline goods enter the plan. The reward `R_{j,τ} = b_j · (last − τ + 1) + p_j` is the cost avoided by serving a unit of day `τ` instead of leaving it unserved to the end of the window. Only arcs leaving `σ` or entering `ω` are negative, so there is no negative cycle, and the existing solver (Bellman-Ford for the initial potentials, then Dijkstra on reduced costs) applies unchanged. It stops at the first non-negative augmenting path: units are only moved when it pays.

When the window reaches the true horizon end, its objective equals the simulation's own accounting exactly (purchase, transport, holding, backlog unit-days, and settlement). For inner windows the term `p_j` stands in for "still unserved when the plan ends".

## 7. Information available to a policy (no leakage)

A policy never sees the scenario's event list. It receives a **forecast view** built by the simulator, which is identical for all policies in an experiment:

* `persistence` (the default, an uncertain forecast): tomorrow looks like today. The current network state persists, and demand stays at today's level. A scheduled failure or recovery is unknown until it happens.
* `perfect` (perfect forecast): the true future network state and demand within the window.

Pending actions of the policy itself are known to it in both modes. Experiments report the two modes separately.

## 8. Recovery policies (common interface)

```
class MultiDayPolicy {
    virtual std::string name() const = 0;
    virtual std::vector<RecoveryAction> decide(const PolicyContext& context) = 0;   // step 6
};
```

`PolicyContext` holds the observed state, the forecast view, the legal candidate actions, the planner, and the configuration. Dispatch (step 7) is not part of the policy: it is the common planner, the same for everyone.

| Policy | Chooses recovery actions by |
|---|---|
| `none` | never: rerouting and daily replanning only (baseline) |
| `greedy` | **immediate benefit**: plans only until the first day the action can help (`lead + 1` days). Each round it takes the action with the most units of planned shortage removed per unit of cost, and only if the planned saving exceeds its cost. Repeats while one qualifies. |
| `bottleneck_aware` | the same window and eligibility; score `L / (cost + λ · f · L + ε)`, where `L` is the penalty-weighted planned shortage removed and `f` the share of planned shipments on route-days at or above 90% of capacity |
| `bottleneck_aware_lambda0` | the same with `λ = 0` (ablation: the risk term removed) |
| `lookahead` | **limited look-ahead**: for each candidate, copy the state, apply the action, simulate `lookaheadDays` days with the common planner inside the **forecast world** (`forecastScenario`: the forecast's demand, and future events only in perfect mode), and compare simulated cost + settlement of what is still owed at the end of the look-ahead with the alternatives (including doing nothing). Greedy over candidates, one action at a time. The horizon is configurable. |
| `rolling_exact` | the optimization-based reference: each day, try every subset of the legal candidates (small candidate sets only) and choose the subset with the lowest planned window cost plus action cost |

All policies start from the same state, receive the same disruption schedule, demand sequence and forecast mode, pay the same costs, and are executed under the same rules and validation.

**Clairvoyant reference (a bound, not a policy).** With perfect knowledge of the whole horizon, solve one time-expanded min-cost flow over all `H` days for every subset of the recovery actions that ever become possible, each taken at the earliest day it can be. Every policy's executed run is a feasible solution of that model with the same cost: actions bought earlier never cost more, because their cost is one-off. So the clairvoyant value is a **lower bound** on the cost of any policy, and "cost above the bound" measures what the policies lose to imperfect information and heuristic choice. Only computed for small candidate sets.

*Attainability (found during testing).* Replaying the bound's actions reaches the bound exactly when all of them are decided on day 0. When it repairs a route on the day that route fails, a replay costs more: on the days before, the replay's planner does not know about its own future purchase, while the bound's single plan does. So the bound is valid but not always attainable by any online policy; "gap to the bound" therefore includes the value of knowing the future, not only heuristic loss.

## 9. Recovery time and metrics

**Service level** of day `t`: `SL(t) = served(t) / due(t)`, defined as 1 when nothing is due.

**Normal service** `N(t)`: the service level of the same scenario with no events, no demand spike and no recovery actions (same network, initial state and demand noise; `MultiDaySimulator::normalService`). It is needed because with demand noise and no safety stock even a healthy network sometimes serves below 95%; an absolute threshold would then call such a network "never recovered". Day `t` counts as **served normally** when `SL(t) ≥ r(t) = min(θ, N(t))`. When normal service always reaches `θ`, this is exactly the rule with `θ` alone.

**Recovery time.** Let `t0` be the first day a disruption occurs, `θ` the threshold (default 0.95) and `w` the stability window (default 3 days). Let `d` be the first day on or after `t0` with `SL(d) < r(d)`; the impact may be delayed, for example while stock cushions a supplier failure. The recovery day is the first `t* > d` such that `SL(τ) ≥ r(τ)` for every `τ` in `t*, ..., t*+w−1`, with all those days inside the horizon. **Recovery time = `t* − t0` days**, counted from the start of the disruption. If service never falls below `r` after `t0`, recovery time is 0. If no such `t*` exists inside the horizon, the run is reported as **not recovered**, never as a number. A second dip after recovery is not counted again: the metric describes the first disruption episode.

Daily records: costs by component and cumulative cost, demand, due, served, new unmet units, backlog, stock per node and in total, units in transit, service level, recovery actions taken and their cost, the policy's decision time, and the planner time.

## 10. Disruption schedules and scenarios

A multi-day scenario is a generated network (the existing generator) plus:

* `transitDays` per route (fixed, or seeded random between a minimum and maximum);
* a demand sequence: the customer's base daily demand, multiplied by a seeded fluctuation `1 + amplitude × u` with `u` uniform in [−1, 1], and optionally a spike of `x%` from day `s` for `d` days;
* a schedule of dated events: the largest regular suppliers fail on day `s` for `d` days (then restore), and/or the busiest routes fail or lose capacity, each element failing at most once per horizon;
* cost settings: backlog fraction, penalty scale, lead times;
* an initial stock level in days of demand;
* a **warm start** (default on): the routes already carry the healthy one-day optimal flow (from supplier production only) on each of their transit days, i.e. the batches that left on days `−τ_e … −1`. Without it every pipeline starts empty and customers starve for the first days; in the first trial runs that start-up shortage, which has nothing to do with the disruptions, dominated every cost. These goods were paid for before day 0 and are counted in the conservation check.

The same seed always gives the same scenario.

## 11. Complexity

With `n` nodes, `m` routes and window `W`, the planning network has about `nW + 2` nodes and `(m + 2n)W` arcs. One plan costs one min-cost flow on it. Per simulated day, `none` costs 1 plan, `greedy`/`bottleneck_aware` up to `O(k²)` short plans for `k` candidates, `lookahead` `O(k · lookaheadDays)` plans per round, and `rolling_exact` `2^k` plans. Exact measured times are reported by the experiments (M8 measures look-ahead time against its horizon). The clairvoyant bound solves `2^k` full-horizon plans; the normal-service baseline adds one no-action run per disrupted scenario.

## 12. What this phase does not include

Manufacturing transformations, multiple products, storage capacity limits, stochastic disruptions drawn during the run (schedules are fixed per seed), and actions other than activating backups and reopening routes.

## 13. Experiments M1-M10 (definitions)

`scx_multiday_experiments` (`backend/tools/multiday_experiments_main.cpp`, presets in `MultiDayExperiments.cpp`) runs every policy on identical seeded scenarios (seeds 5000, 5001, ...) and writes `experiments/results/multiday/<ID>_runs.csv`, `<ID>_daily.csv` and `<ID>_summary.csv`. `experiments/scripts/analyze_multiday.py` and `plot_multiday.py` compute every reported number and chart from those files.

Base scenario: 4 suppliers, 3 backups, 3 warehouses, 3 distribution centers, 6 customers (19 nodes), capacity factor 1.2, horizon 30 days, transit 1-2 days, demand noise 0.1, one day of warehouse stock, warm start, persistence forecast, window 7 (raised when needed), leads 1 day; the 2 largest regular suppliers fail on day 2 for 6 days. 20 seeds per setting unless stated.

| ID | Varies | Values | Question |
|---|---|---|---|
| M1 | failed suppliers | 1, 2, 3 | supplier failures |
| M2 | failed busiest routes (no supplier failure) | 1, 2, 4 | route failures |
| M3 | 2 suppliers + 2 routes + 50% demand spike on days 4-6 | one setting | multiple disruptions |
| M4 | capacity factor | 1.05, 1.2, 1.5, 2.0 | spare capacity |
| M5 | transit days on every route | 0, 1, 2, 3 | transit times |
| M6 | demand noise amplitude | 0, 0.2, 0.4, 0.6 | demand fluctuation |
| M7 | shortage-penalty scale | 0.5, 1, 2, 4 | penalties |
| M8 | look-ahead horizon (none, greedy, lookahead only) | 1, 2, 3, 5, 8, 12 | look-ahead value and runtime |
| M9 | λ of bottleneck-aware, on the M3 scenario, 40 seeds | 0.5, 1, 2, 5 | ablation: risk term vs λ = 0 |
| M10 | forecast mode on the M3 scenario | persistence, perfect | value of information |

## 14. The C++ components explained

**Model (`MultiDayModel.h`).** *Data:* `MultiDayScenario` (graph, horizon, `transitDays` per route, `demand[node][day]`, sorted `events`, `initialPipeline`, `normalDemand`), `MultiDayState` (graph with today's up/down state, `stock`, `backlog`, `pipeline` of `Shipment{edge, qty, departure, arrival}`, `pending` actions, `purchased` set, running totals), `DayRecord`. *Correctness:* `validateMultiDayScenario` rejects wrong sizes, negative values, unsorted or out-of-horizon events and inconsistent initial shipments. *Cost:* O(n·H + m + events).

**Dispatch planner (`DispatchPlanner.cpp`).** *Data:* a `FlowNetwork` with node `(i, k)` at index `k·n + i`, source `W·n`, sink `W·n + 1`. *Decision:* the arcs of section 6; one min-cost flow (Bellman-Ford potentials, then Dijkstra on reduced costs; stop at the first non-negative path). *Correctness:* forced stock and arrivals use cost −M, and a plan that leaves forced units unaccounted throws; at the true horizon end the plan's cost equals the simulator's accounting (tested: 715 on the hand-calculated chain). *Cost:* about `nW` nodes and `(m + 2n)W` arcs; Bellman-Ford O(VE) once, then O(F · E log V) for F augmentations.

**Simulator (`MultiDaySimulator.cpp`).** *State transitions:* `beginDay` = steps 1-5 (arrivals leave the pipeline exactly once, events, completed actions, capacities, demand), `finishDay` = steps 6-13 (pay and schedule actions, plan, validate, dispatch, fulfil oldest backlog first, update backlog, cost, record). *Correctness:* illegal or repeated actions throw `invalid_argument`; any capacity, supply, negative-stock or conservation violation throws `logic_error` (never repaired silently). `ForecastProvider` is the only path from the scenario to a policy (no leakage; tested by mutation). *Cost:* one plan per day plus O(pipeline) bookkeeping; a run is O(H · plan).

**Policies (`MultiDayPolicies.cpp`).** *Common interface:* `decide(PolicyContext) → actions`; the context holds the state, forecast, candidates (inactive backups and failed routes not already pending) and config, never the scenario. *Greedy / bottleneck-aware:* per round, for each candidate, the delivery delay (Dijkstra on transit days from the activated supplier) fixes a short window; plan with and without it; eligible iff planned shortage falls and saving > cost; pick the best score, repeat. O(k²) short plans per day. Because the loop keeps adding every eligible candidate, the score only sets the ORDER; it changes the chosen set only when an earlier pick makes a later one ineligible. *Look-ahead:* copy the state, simulate `h` days in the forecast world with and without each candidate; O(k² · h) plans per day in the worst case. *Rolling exact:* all `2^k` subsets over the planning window; throws `PolicyNotApplicable` (run reported as skipped) above the limit. *Clairvoyant bound:* all `2^k` subsets of every action that ever becomes possible at its earliest day, each one full-horizon perfect-information plan.

**Scenario builder (`MultiDayScenarioBuilder.cpp`).** Seeded generator network, warehouse stock, scaled penalties, seeded transit days, demand noise and spike, failure of the largest suppliers / busiest routes (from a one-day min-cost flow) with restoration, warm-start pipeline. Same seed, same scenario on every compiler (portable random mapping).

**Experiments (`MultiDayExperiments.cpp`).** For each run: build the scenario once, compute the bound once, run every policy on the same simulator; aggregate with the existing `Statistics` (mean, sd, 95% t interval, paired differences). A policy beating the bound throws `logic_error`.

**JSON / API (`MultiDayJson.cpp`, `POST /api/multiday/run`).** Type- and range-checked request; horizon 5-30 days, at most 60 nodes, look-ahead ≤ 12, exhaustive searches ≤ 10 candidates; returns the scenario, normal service, every policy's daily records with per-node/per-route snapshots, and the bound.
