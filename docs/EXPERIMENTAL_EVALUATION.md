# Experimental Evaluation

Part 1 (sections 1-5): the single-period experiments A to G. Part 2 (sections 6-9): the multi-day experiments M1 to M10.

Everything in this document was **measured** by running the C++ engine; the raw data are in `experiments/results/` and every number below can be regenerated with the commands in section 2. Nothing here is estimated, tuned after the fact, or typed in from theory.

## 1. Questions and the hypothesis under test

1. How do the strategies compare on cost and demand fulfilment after a disruption? (B, C, D, E, G)
2. How do running time and memory grow with network size? (A)
3. **Hypothesis (experimental, not assumed):** accounting for downstream capacity bottlenecks (the `bottleneck_aware` heuristic, weight λ) gives better recovery than the plain greedy baseline. The ablation (F) compares the same scoring with the risk term (λ > 0) and without it (λ = 0).

The null result matters as much as a positive one: if the intervals below include zero, the data do **not** support the hypothesis.

## 2. Method

**Paired design.** One experiment is `runs` seeded scenarios. For run `r` a layered network is generated from seed `1000 + r` (27 nodes: 6 regular and 3 backup suppliers, 4 warehouses, 4 distribution centers, 10 customers), a disruption is applied, and **every strategy solves the identical disrupted network**. Differences between strategies are therefore differences in the algorithm, not in the luck of the scenario. The generator guarantees that the *healthy* network can serve all demand, so every shortage in these results is caused by the disruption.

**Scenario kinds (the swept value `x`).** B: random disruption of severity `x` (supplier failures, route failures, capacity cuts and demand spikes together); C: the `x` largest regular suppliers fail; D: the busiest quarter of routes lose `x` percent of capacity (100 = they fail); E: every customer's demand rises by `x` percent; A: all layers scaled by `x` at severity 0.3; F and G: a single fixed severity (0.5 and 0.4).

**Strategies.** `dijkstra_dispatch` (shortest-path baseline), `min_cost_flow` (optimal re-routing, no paid actions), `greedy`, `bottleneck_aware` (λ = 1), `bottleneck_aware_lambda0` (ablation), `exact` (exhaustive search over recovery actions). All are scored by the same objective, so their costs are directly comparable (`MATHEMATICAL_MODEL.md`). `exact` is skipped, and reported as skipped, in any run with more than 10 candidate actions.

**Measured per run:** total cost (flow cost + penalties + action costs), unmet demand, fulfilment, network utilisation, number of actions, validity (independent validator), runtime (median wall-clock over repeats; 3 repeats in A, 1 elsewhere) and peak heap (bytes requested through `operator new` during the call).

**Reported statistics:** mean, sample standard deviation, min and max over runs, 95% confidence intervals (Student t, exact table for up to 30 degrees of freedom, 1.96 beyond), *regret* (average percentage above the cheapest strategy in the same run), and *paired differences* (mean of cost(a) − cost(b) over the same runs, with a 95% interval). A paired interval that excludes 0 is evidence of a real difference; one that includes 0 means the data cannot tell it apart from noise.

**Not measured in Part 1:** "recovery time". The single-period model has no time dimension (route transit hours are used only for reporting), so a recovery time would not be meaningful there. It is measured in the multi-day experiments of Part 2.

**Reproduce**
```powershell
cmake -S . -B build -G Ninja ; cmake --build build
build\backend\scx_experiments.exe --out experiments\results        # about 85 seconds on the development machine
python experiments\scripts\analyze.py > experiments\results\analysis.txt
python experiments\scripts\plot_results.py                          # charts in experiments\results\plots
```
Files: `<ID>_summary.csv` (per setting and strategy), `<ID>_runs.csv` (every single run), `all_results.json`, `analysis.txt`, `plots/*.png`. Runs per setting: A 5, B–E 30, F 100, G 200.

## 3. Results

### 3.1 Recovery beats re-routing alone, and by a lot (B, C, D, E, G)

Greedy (and every strategy that can take recovery actions) is much cheaper than re-routing with the network as it stands. Paired difference cost(greedy) − cost(min-cost flow), 30 runs per setting:

| Experiment | setting | mean difference | 95% interval | greedy cheaper / dearer / equal |
|---|---|---|---|---|
| B severity | 0.3 | −1,934 | ± 855 | 17 / 0 / 13 |
| B severity | 0.5 | −5,283 | ± 1,144 | 27 / 0 / 3 |
| B severity | 1.0 | −10,717 | ± 740 | 30 / 0 / 0 |
| C failed suppliers | 3 | −3,569 | ± 955 | 26 / 0 / 4 |
| C failed suppliers | 5 | −8,778 | ± 594 | 30 / 0 / 0 |
| E demand +100% | | −2,889 | ± 973 | 22 / 0 / 8 |
| E demand +200% | | −5,878 | ± 788 | 29 / 0 / 1 |
| G severity 0.4 (200 runs) | | −2,675 | ± 350 | 150 / 0 / 50 |

Greedy is never more expensive than re-routing in any run (the "dearer" count is 0 everywhere), as the eligibility rule guarantees. In the 200-run experiment G the mean total cost is 12,758 (min-cost flow) vs 10,083 (greedy): 21% lower, and mean fulfilment rises from 84% to 96% (unmet demand 75.4 → 17.6 units). In B the saving in mean cost is 23% at severity 0.4, 30% at 0.5, 31% at 0.6 (its largest) and 23% at 1.0. At mild disruption (D up to 60% capacity loss; C with one failed supplier) there is no measurable difference, because nothing paid for itself.

**Shortest-path dispatch is the worst** of the full-network strategies in every experiment. In B it costs 258 ± 122 more than min-cost flow with no disruption at all (x = 0), up to 671 ± 344 at severity 0.4, and is dearer in every one of the 30 runs at those settings. At severity 1.0 the gap shrinks to 31 ± 20 because penalties dominate everything. In G it is 4.7% above min-cost flow (13,355 vs 12,758). This is the practical cost of choosing customers one at a time and never undoing a shipment.

Charts: `plots/B_cost.png`, `B_unmet.png`, `C_*`, `D_*`, `E_*`.

### 3.2 Greedy is close to optimal on average but measurably worse (B, C, D, E, G)

Against the exact optimum (only in runs with at most 10 candidate actions, compared on the same runs): in G (all 200 runs) greedy is **221.9 ± 37.4 above exact**, 2.2% of the exact mean (10,083 vs 9,861), worse in 157 runs and equal in 43. Gaps grow with severity in B (+67 ± 46 at 0.1; +441 ± 197 at 0.5; +618 ± 300 at 0.8) and vanish when so many suppliers fail that every beneficial action is obvious (C with 4 or 5 failures: identical cost in all 30 runs).

Two specific findings:

* **Greedy ignores pure cost savings.** With *no* disruption (B, x = 0), exact is cheaper than greedy in 4 of 30 runs. In those runs exact activates one backup supplier because it lowers the cost of normal operations, while greedy takes 0 actions because there is no shortage to reduce (greedy's eligibility rule requires a shortage reduction). This was checked in the saved plans, not assumed.
* **Route failures: a large gap on the runs we can check.** In D at 100% loss (the busiest quarter of routes fail) greedy is 1,305 ± 135 above exact in all 18 of the runs where exact could run: on those 18 runs greedy averages 7,054 and exact 5,749, i.e. 23% more. Exact could not run in the other 12 (too many candidate actions), so this comparison covers the easier scenarios only. A plausible cause is the myopia documented in `ALGORITHMS.md` (reopening several routes together can pay off when no single reopening does), but this experiment did not isolate the cause.

### 3.3 The hypothesis is not supported: the bottleneck-risk term shows no measurable effect (B–G)

Paired difference cost(bottleneck-aware, λ = 1) − cost(greedy):

* **B, C, D, E: all 26 settings, none excludes zero.** The largest mean differences are small (+22 ± 33 at B 0.2; −26 ± 33 at C with 2 failures; +21 ± 37 at B 0.5; −9 ± 29 at E +150%) against costs of 7,000 to 49,000. In most runs the two chose plans of identical cost (for example 28 of 30 at B 0.2).
* **G, 200 runs:** +10.4 ± 16.9; bottleneck-aware cheaper in 11 runs, dearer in 15, equal in 174. Mean costs 10,093 vs 10,083; fulfilment 96% for both.
* **F, the ablation, 100 paired runs at severity 0.5:**

| λ | mean cost | cost(λ) − cost(λ = 0) | 95% interval | cheaper / dearer / equal |
|---|---|---|---|---|
| 0 | 12,881 | (reference) | | |
| 0.25 | 12,897 | +16.0 | ± 38.1 | 2 / 2 / 96 |
| 0.5 | 12,893 | +12.0 | ± 39.3 | 5 / 3 / 92 |
| 1 | 12,868 | −13.7 | ± 72.1 | 7 / 5 / 88 |
| 2 | 12,859 | −22.7 | ± 74.9 | 10 / 6 / 84 |
| 5 | 12,866 | −15.6 | ± 78.4 | 10 / 10 / 80 |

Every interval includes zero. The λ ≥ 1 means are slightly lower than λ = 0 but by less than a third of their own uncertainty, which is not evidence of an effect. Across all five λ > 0 variants and 100 runs, 440 of 500 plans (88%) had *exactly* the cost of the λ = 0 plan: the risk term rarely changes a decision in this setup. Also, λ = 0 matched plain greedy in all 100 runs (the penalty-weighted numerator made no difference to the final cost here).

**What this does and does not show.** On this scenario family and objective, the data give no evidence that the bottleneck-risk term helps, and no evidence that it hurts. It does **not** show that the idea is wrong in general: (a) the model is one planning period, so the slack the risk term buys is never rewarded by a later disruption (the unit test where it changes the choice shows it paying 2 more now for a more robust plan); (b) the generator offers only 3 backup suppliers, so many scenarios have a single obviously best action and nothing for the term to decide; (c) one scenario family and one saturation threshold (0.9) were tested.

### 3.4 Running time and memory (A)

Mean per scenario, 5 runs per size (27 to 216 nodes), median of 3 timings per run:

| nodes | dispatch ms | min-cost flow ms | greedy ms | bottleneck-aware ms |
|---|---|---|---|---|
| 27 | 0.06 | 0.11 | 1.49 | 1.24 |
| 54 | 0.18 | 0.54 | 20.1 | 19.6 |
| 81 | 0.40 | 1.04 | 25.3 | 25.5 |
| 108 | 0.76 | 2.21 | 189 | 186 |
| 162 | 1.82 | 4.64 | 358 | 347 |
| 216 | 3.26 | 8.98 | 911 | 924 |

Peak heap grows about linearly: greedy 24 KB → 217 KB (n^1.03), min-cost flow 13 KB → 159 KB (n^1.18), dispatch 3 KB → 25 KB (n^1.02).

Empirical growth exponents (slope of log runtime against log nodes over the six sizes): dispatch **n^1.95**, min-cost flow **n^2.10**, greedy **n^3.05**, bottleneck-aware **n^3.13**. This is consistent with the analysis: greedy runs one min-cost-flow solve per candidate action in each round, and both the cost of one solve and the number of candidate actions grow with the network. The steps are irregular because greedy's work depends on how many candidate actions exist (on average 5, 11, 16, 22, 33 and 44 across the six sizes) and on how many it actually takes (0.4 to 2.6 on average, one round each): at 81 nodes only 0.4 actions were taken on average, so it was barely slower than at 54 nodes. The exponents are therefore indicative only (six sizes, 5 runs each, a single machine), not a precise fit. Greedy and bottleneck-aware cost the same (the extra O(E) risk computation is invisible). Charts: `plots/A_runtime.png`, `A_memory.png`.

### 3.5 Validity and feasibility

Every plan of every strategy in every run passed the independent validator (feasibility rate 100% in all summaries). Costs reported are recomputed from raw flows, not taken from the solver.

## 4. Threats to validity

* **One generator, one parameter family.** Results describe layered networks of 27 nodes (216 in A) with this cost and capacity structure. Other topologies, ratios of penalty to cost, or many more backup suppliers could behave differently.
* **Exact is only available for easy scenarios** (at most 10 candidate actions). Comparisons with exact use only the runs where it ran, so they are biased toward easier cases. In B at severity 1.0 and D at 100% exact ran on 20 and 18 of 30 runs; those points are omitted from the cost charts for that reason and appear only as paired differences.
* **Many intervals.** There are dozens of 95% intervals; about 1 in 20 would exclude zero by chance alone. This strengthens the "no effect" reading for bottleneck-aware (none excluded) but means an isolated "differs" in a table with a small mean difference should not be over-read.
* **Timing is a single machine and noisy at sub-millisecond scale.** Runtimes below 1 ms in particular are dominated by timer and cache effects. Memory counts bytes requested through `operator new`; it excludes stack memory and allocator overhead.
* **Single period, static demand.** Nothing measures robustness to a *second* disruption, which is what the bottleneck-risk term is designed for.

## 5. Conclusions supported by the data

1. Paid recovery actions chosen by greedy, bottleneck-aware or exact search reduce cost substantially compared with re-routing only (21% lower mean cost in G, 23% to 31% lower in B at severities 0.4 to 0.6) and raise fulfilment, once disruption is more than mild. This is well supported (intervals far from zero, 150 of 200 runs strictly better, 0 worse).
2. Greedy is within about 2% of the exact optimum on average but is measurably worse, and it misses cost-saving actions when nothing is short.
3. Shortest-path dispatch is consistently worse than min-cost flow; the gap is largest at moderate disruption.
4. The experimental bottleneck-aware heuristic **did not demonstrably improve** on greedy in any experiment; its plans had the same cost as greedy's in most runs.
5. Greedy and bottleneck-aware running time grows roughly with the cube of the network size in this range, min-cost flow and dispatch roughly with the square; heap grows about linearly.

---

# Part 2: Multi-day experiments (M1 to M10)

Part 1 above is single-period. This part evaluates the **multi-day simulation** (`MULTI_DAY_SIMULATION.md`): 30 simulated days with transit times, stock, backlog, daily re-planning, and disruptions that start and end. Every number comes from `experiments/results/multiday/*.csv`, written by `scx_multiday_experiments`. The tables are generated by `experiments/scripts/tables_multiday.py`, and the remaining figures come from `analyze_multiday.py` (its output is saved in `experiments/results/multiday/analysis.txt`).

## 6. Method

* **Paired design.** For each run, one scenario is built from seed `5000 + run` and every policy runs on it: same network, opening stock, goods already in transit (warm start), disruption schedule, daily demand, costs and forecast mode. The clairvoyant lower bound is computed once per scenario (it was computable in every run: at most 10 possible actions).
* **Base scenario:** 19 nodes (4 suppliers, 3 backups, 3 warehouses, 3 distribution centers, 6 customers), capacity factor 1.2, transit 1-2 days, demand noise ±10%, one day of warehouse stock. The two largest suppliers fail on day 2 for 6 days. Persistence forecast, 30 days, 20 seeds per setting (40 in M9). Section 13 of `MULTI_DAY_SIMULATION.md` lists what each experiment varies.
* **Policies:** `none` (daily re-planning, no paid actions), `greedy`, `bottleneck_aware` (λ = 1), `bottleneck_aware_lambda0` (ablation), `lookahead` (5 days unless stated), `rolling_exact` (every subset of today's candidates, evaluated over the planning window). The look-ahead's own risk weight is 0.
* **Metrics:** total cost over 30 days, including the end-of-horizon penalty for units still owed. Also fill rate (late deliveries count as served, and their lateness is charged as backlog cost), final backlog, action cost, decision time per run, percentage above the clairvoyant bound, and recovery time. Recovery time is reported only for runs that met the definition (`MULTI_DAY_SIMULATION.md` section 9).
* **Statistics:** as in Part 1. A result is "cheaper" or "dearer" only if the 95% interval of the paired difference excludes 0. The run counts (cheaper / dearer / equal) are shown as well, because cost differences are skewed: a few scenarios with large shortages dominate the mean.

**Reproduce**
```powershell
build\backend\scx_multiday_experiments.exe --out experiments\results\multiday    # about 16 minutes on the development machine
python experiments\scripts\analyze_multiday.py
python experiments\scripts\tables_multiday.py
python experiments\scripts\plot_multiday.py
```

## 7. Results

<!-- generated by experiments/scripts/tables_multiday.py from the CSV files; do not edit by hand -->

#### Paired cost difference to greedy

Mean of cost(policy) − cost(greedy) over the same 20 scenarios, ± the 95% half-width, then (runs cheaper / dearer / equal). **Bold**: the 95% interval excludes 0 (negative = cheaper than greedy); otherwise the interval includes 0; = identical in every run.

| Exp | x | none − greedy | BA − greedy | BA λ=0 − greedy | look-ahead(5) − greedy | rolling exact − greedy |
|---|---|---|---|---|---|---|
| M1 | 1 | +7,284 ± 8,303 (0/8/12) | = (all equal) | = (all equal) | +3,653 ± 7,112 (1/5/14) | -9,135 ± 12,848 (13/1/6) |
| M1 | 2 | **+46,584 ± 25,452** (0/15/5) | = (all equal) | = (all equal) | +18,764 ± 23,719 (3/9/8) | **-23,945 ± 23,521** (19/0/1) |
| M1 | 3 | **+148,463 ± 39,324** (0/20/0) | +787 ± 1,617 (0/2/18) | = (all equal) | **+51,445 ± 24,781** (1/16/3) | **-12,715 ± 6,956** (15/1/4) |
| M2 | 1 | +335 ± 897 (1/1/18) | = (all equal) | = (all equal) | **-1,167 ± 410** (19/0/1) | **-2,544 ± 1,315** (20/0/0) |
| M2 | 2 | +11,419 ± 11,985 (1/7/12) | +9 ± 20 (0/1/19) | = (all equal) | **-1,512 ± 711** (19/1/0) | **-3,025 ± 1,327** (20/0/0) |
| M2 | 4 | **+55,037 ± 26,703** (2/14/4) | +209 ± 248 (0/3/17) | = (all equal) | **-2,726 ± 1,181** (19/1/0) | **-4,324 ± 1,464** (20/0/0) |
| M3 | 1 | **+94,705 ± 37,480** (2/18/0) | = (all equal) | = (all equal) | +31,637 ± 36,457 (10/10/0) | **-44,125 ± 34,086** (20/0/0) |
| M4 | 1.05 | **+76,885 ± 37,051** (0/15/5) | = (all equal) | = (all equal) | +23,849 ± 26,188 (5/8/7) | **-41,083 ± 39,871** (19/0/1) |
| M4 | 1.2 | **+46,584 ± 25,452** (0/15/5) | = (all equal) | = (all equal) | +18,764 ± 23,719 (3/9/8) | **-23,945 ± 23,521** (19/0/1) |
| M4 | 1.5 | **+8,345 ± 5,028** (1/13/6) | = (all equal) | = (all equal) | +3,976 ± 4,693 (4/8/8) | **-6,710 ± 4,973** (20/0/0) |
| M4 | 2 | +228 ± 273 (2/4/14) | = (all equal) | = (all equal) | -9 ± 345 (2/3/15) | **-2,338 ± 1,311** (13/3/4) |
| M5 | 0 | **+72,074 ± 29,584** (0/19/1) | +109 ± 180 (0/5/15) | = (all equal) | **-1,106 ± 626** (15/3/2) | **-1,258 ± 683** (16/3/1) |
| M5 | 1 | **+84,433 ± 32,019** (0/19/1) | +19 ± 64 (1/1/18) | = (all equal) | -112 ± 137 (5/0/15) | **-493 ± 337** (9/1/10) |
| M5 | 2 | **+89,683 ± 30,650** (0/20/0) | = (all equal) | = (all equal) | **+89,683 ± 30,650** (0/20/0) | -257 ± 376 (2/1/17) |
| M5 | 3 | **+80,787 ± 26,577** (0/20/0) | = (all equal) | = (all equal) | **+80,787 ± 26,577** (0/20/0) | -168 ± 261 (2/0/18) |
| M6 | 0 | **+48,300 ± 26,436** (0/14/6) | +90 ± 188 (0/1/19) | = (all equal) | +19,762 ± 24,512 (3/9/8) | **-24,478 ± 24,403** (20/0/0) |
| M6 | 0.2 | **+45,302 ± 24,055** (0/15/5) | = (all equal) | = (all equal) | +18,709 ± 21,964 (2/9/9) | **-23,161 ± 22,461** (19/0/1) |
| M6 | 0.4 | **+42,364 ± 21,195** (0/16/4) | = (all equal) | = (all equal) | +16,899 ± 18,730 (2/10/8) | **-21,524 ± 19,941** (18/0/2) |
| M6 | 0.6 | **+42,061 ± 19,737** (0/16/4) | = (all equal) | = (all equal) | **+18,505 ± 16,798** (1/11/8) | **-18,643 ± 17,304** (18/0/2) |
| M7 | 0.5 | **+20,001 ± 12,026** (0/13/7) | +2 ± 5 (0/1/19) | = (all equal) | +8,200 ± 11,313 (3/8/9) | **-16,986 ± 11,631** (19/0/1) |
| M7 | 1 | **+46,584 ± 25,452** (0/15/5) | = (all equal) | = (all equal) | +18,764 ± 23,719 (3/9/8) | **-23,945 ± 23,521** (19/0/1) |
| M7 | 2 | **+90,349 ± 51,439** (0/15/5) | = (all equal) | = (all equal) | +37,013 ± 48,182 (5/8/7) | -46,662 ± 47,798 (19/0/1) |
| M7 | 4 | **+176,310 ± 103,127** (0/15/5) | = (all equal) | = (all equal) | +74,662 ± 97,432 (5/8/7) | -90,518 ± 95,539 (19/0/1) |
| M10 | 0 | **+94,705 ± 37,480** (2/18/0) | = (all equal) | = (all equal) | +31,637 ± 36,457 (10/10/0) | **-44,125 ± 34,086** (20/0/0) |
| M10 | 1 | **+68,860 ± 33,438** (0/17/3) | = (all equal) | = (all equal) | +24,731 ± 29,869 (7/10/3) | **-34,520 ± 30,228** (20/0/0) |

#### Mean total cost (and mean % above the clairvoyant lower bound)

| Exp | x | none | greedy | BA | look-ahead(5) | rolling exact | bound runs |
|---|---|---|---|---|---|---|---|
| M1 | 1 | 131.1k (29%) | 123.8k (22%) | 123.8k (22%) | 127.4k (25%) | 114.7k (12%) | 20/20 |
| M1 | 2 | 190.3k (84%) | 143.7k (39%) | 143.7k (39%) | 162.5k (56%) | 119.8k (16%) | 20/20 |
| M1 | 3 | 311.8k (190%) | 163.4k (53%) | 164.1k (54%) | 214.8k (99%) | 150.6k (41%) | 20/20 |
| M2 | 1 | 116.2k (15%) | 115.8k (15%) | 115.8k (15%) | 114.7k (13%) | 113.3k (12%) | 20/20 |
| M2 | 2 | 128.2k (26%) | 116.8k (15%) | 116.8k (15%) | 115.2k (14%) | 113.7k (12%) | 20/20 |
| M2 | 4 | 178.2k (66%) | 123.2k (16%) | 123.4k (16%) | 120.4k (14%) | 118.8k (12%) | 20/20 |
| M3 | 1 | 331.4k (171%) | 236.7k (92%) | 236.7k (92%) | 268.3k (117%) | 192.5k (57%) | 20/20 |
| M4 | 1.05 | 244.3k (125%) | 167.4k (54%) | 167.4k (54%) | 191.3k (76%) | 126.3k (19%) | 20/20 |
| M4 | 1.2 | 190.3k (84%) | 143.7k (39%) | 143.7k (39%) | 162.5k (56%) | 119.8k (16%) | 20/20 |
| M4 | 1.5 | 126.4k (28%) | 118.0k (20%) | 118.0k (20%) | 122.0k (24%) | 111.3k (13%) | 20/20 |
| M4 | 2 | 107.8k (14%) | 107.6k (14%) | 107.6k (14%) | 107.6k (14%) | 105.2k (12%) | 20/20 |
| M5 | 0 | 193.2k (64%) | 121.1k (3%) | 121.2k (3%) | 120.0k (2%) | 119.9k (2%) | 20/20 |
| M5 | 1 | 206.0k (89%) | 121.5k (12%) | 121.5k (12%) | 121.4k (12%) | 121.0k (12%) | 20/20 |
| M5 | 2 | 212.5k (113%) | 122.8k (24%) | 122.8k (24%) | 212.5k (113%) | 122.6k (23%) | 20/20 |
| M5 | 3 | 199.3k (120%) | 118.5k (31%) | 118.5k (31%) | 199.3k (120%) | 118.3k (31%) | 20/20 |
| M6 | 0 | 182.3k (77%) | 134.0k (30%) | 134.0k (30%) | 153.7k (48%) | 109.5k (6%) | 20/20 |
| M6 | 0.2 | 199.2k (93%) | 153.9k (49%) | 153.9k (49%) | 172.6k (66%) | 130.8k (26%) | 20/20 |
| M6 | 0.4 | 223.0k (113%) | 180.7k (72%) | 180.7k (72%) | 197.6k (88%) | 159.1k (51%) | 20/20 |
| M6 | 0.6 | 251.1k (135%) | 209.1k (95%) | 209.1k (95%) | 227.6k (112%) | 190.4k (77%) | 20/20 |
| M7 | 0.5 | 151.0k (46%) | 131.0k (28%) | 131.0k (28%) | 139.2k (35%) | 114.0k (11%) | 20/20 |
| M7 | 1 | 190.3k (84%) | 143.7k (39%) | 143.7k (39%) | 162.5k (56%) | 119.8k (16%) | 20/20 |
| M7 | 2 | 267.9k (159%) | 177.6k (72%) | 177.6k (72%) | 214.6k (105%) | 130.9k (26%) | 20/20 |
| M7 | 4 | 420.1k (304%) | 243.8k (134%) | 243.8k (134%) | 318.5k (200%) | 153.3k (47%) | 20/20 |
| M10 | 0 | 331.4k (171%) | 236.7k (92%) | 236.7k (92%) | 268.3k (117%) | 192.5k (57%) | 20/20 |
| M10 | 1 | 233.6k (91%) | 164.7k (34%) | 164.7k (34%) | 189.4k (53%) | 130.2k (6%) | 20/20 |

#### Recovered runs / runs, and mean recovery time of the recovered runs

| Exp | x | none | greedy | BA | look-ahead(5) | rolling exact |
|---|---|---|---|---|---|---|
| M1 | 1 | 16/20, 9.4 d | 19/20, 9.6 d | 19/20, 9.6 d | 17/20, 8.9 d | 20/20, 9.1 d |
| M1 | 2 | 10/20, 17.5 d | 18/20, 14.3 d | 18/20, 14.3 d | 15/20, 15.2 d | 20/20, 11.7 d |
| M1 | 3 | 3/20, 19.0 d | 16/20, 16.6 d | 15/20, 16.9 d | 10/20, 18.1 d | 16/20, 14.9 d |
| M2 | 1 | 20/20, 8.1 d | 20/20, 7.8 d | 20/20, 7.8 d | 20/20, 3.9 d | 19/20, 3.6 d |
| M2 | 2 | 18/20, 8.8 d | 20/20, 7.0 d | 20/20, 7.0 d | 20/20, 4.5 d | 19/20, 4.4 d |
| M2 | 4 | 11/20, 9.2 d | 16/20, 6.9 d | 16/20, 7.2 d | 16/20, 6.8 d | 16/20, 6.1 d |
| M3 | 1 | 1/20, 25.0 d | 7/20, 18.4 d | 7/20, 18.4 d | 6/20, 22.0 d | 9/20, 16.4 d |
| M4 | 1.05 | 6/20, 14.3 d | 15/20, 14.1 d | 15/20, 14.1 d | 11/20, 15.6 d | 20/20, 13.2 d |
| M4 | 1.2 | 10/20, 17.5 d | 18/20, 14.3 d | 18/20, 14.3 d | 15/20, 15.2 d | 20/20, 11.7 d |
| M4 | 1.5 | 20/20, 14.2 d | 20/20, 12.2 d | 20/20, 12.2 d | 20/20, 13.4 d | 19/20, 9.7 d |
| M4 | 2 | 20/20, 8.0 d | 20/20, 8.7 d | 20/20, 8.7 d | 20/20, 7.8 d | 20/20, 8.5 d |
| M5 | 0 | 13/20, 12.6 d | 20/20, 3.5 d | 20/20, 3.5 d | 20/20, 2.0 d | 20/20, 2.4 d |
| M5 | 1 | 11/20, 16.5 d | 20/20, 10.2 d | 20/20, 10.2 d | 20/20, 10.2 d | 20/20, 9.6 d |
| M5 | 2 | 6/20, 17.7 d | 17/20, 14.7 d | 17/20, 14.7 d | 6/20, 17.7 d | 17/20, 14.6 d |
| M5 | 3 | 3/20, 13.7 d | 20/20, 17.9 d | 20/20, 17.9 d | 3/20, 13.7 d | 20/20, 17.9 d |
| M6 | 0 | 12/20, 14.9 d | 19/20, 12.5 d | 19/20, 12.5 d | 16/20, 13.5 d | 20/20, 6.8 d |
| M6 | 0.2 | 9/20, 16.2 d | 16/20, 13.7 d | 16/20, 13.7 d | 14/20, 14.8 d | 19/20, 11.7 d |
| M6 | 0.4 | 7/20, 17.9 d | 15/20, 15.2 d | 15/20, 15.2 d | 10/20, 17.1 d | 20/20, 14.1 d |
| M6 | 0.6 | 6/20, 15.0 d | 16/20, 14.8 d | 16/20, 14.8 d | 13/20, 16.5 d | 17/20, 11.4 d |
| M7 | 0.5 | 10/20, 17.7 d | 15/20, 14.8 d | 15/20, 14.8 d | 14/20, 16.2 d | 20/20, 11.6 d |
| M7 | 1 | 10/20, 17.5 d | 18/20, 14.3 d | 18/20, 14.3 d | 15/20, 15.2 d | 20/20, 11.7 d |
| M7 | 2 | 11/20, 18.2 d | 18/20, 14.2 d | 18/20, 14.2 d | 15/20, 14.9 d | 20/20, 11.7 d |
| M7 | 4 | 11/20, 18.1 d | 18/20, 14.3 d | 18/20, 14.3 d | 15/20, 14.9 d | 20/20, 12.0 d |
| M10 | 0 | 1/20, 25.0 d | 7/20, 18.4 d | 7/20, 18.4 d | 6/20, 22.0 d | 9/20, 16.4 d |
| M10 | 1 | 8/20, 18.1 d | 14/20, 14.1 d | 14/20, 14.1 d | 11/20, 15.6 d | 16/20, 10.4 d |

#### Look-ahead horizon (M8, scenario of M1 with 2 failed suppliers)

| look-ahead days | mean cost | look-ahead − greedy (95%) | decision time per run | greedy decision time |
|---|---|---|---|---|
| 1 | 190.3k | **+46,584 ± 25,452** (0/15/5) | 3.2 ms | 50.8 ms |
| 2 | 190.3k | **+46,584 ± 25,452** (0/15/5) | 10.9 ms | 50.7 ms |
| 3 | 190.3k | **+46,584 ± 25,452** (0/15/5) | 28.9 ms | 49.5 ms |
| 5 | 162.5k | +18,764 ± 23,719 (3/9/8) | 97.3 ms | 49.5 ms |
| 8 | 117.7k | **-25,987 ± 23,539** (19/0/1) | 171.4 ms | 49.4 ms |
| 12 | 116.6k | **-27,074 ± 23,821** (20/0/0) | 318.6 ms | 49.3 ms |

#### Ablation of the risk term (M9, multiple disruptions, 40 seeds)

| λ | BA(λ) − BA(λ=0), mean ± 95% | runs cheaper / dearer / equal | greedy − BA(λ=0) |
|---|---|---|---|
| 0.5 | -723 ± 1446 | 2 / 2 / 36 | = (all equal) |
| 1 | -1340 ± 1778 | 4 / 2 / 34 | = (all equal) |
| 2 | -1217 ± 1575 | 5 / 2 / 33 | = (all equal) |
| 5 | -1105 ± 1599 | 5 / 2 / 33 | = (all equal) |

### 7.1 Recovery actions matter, and the per-day exhaustive reference is the strongest policy in most settings

`none` is dearer than `greedy` in 21 of 25 settings, with the interval excluding 0. The exceptions are mild disruptions (one supplier or one or two routes) and plenty of spare capacity (factor 2.0), where re-planning alone almost suffices. `rolling_exact` is cheaper than greedy in 20 of 25 settings and never dearer with an interval excluding 0. In the other 5 settings it is cheaper in more runs than it is dearer: 13/1, 2/1, 2/0, 19/0, 19/0. The two cases with 19 cheaper runs and no dearer run (penalty scales 2 and 4) have intervals that include 0 only because a few scenarios with very large shortages make the spread wide.

### 7.2 Where the policies differ most

* **Tight capacity, more failures, higher penalties.** The advantage of `rolling_exact` over greedy is largest here: −41,083 at capacity factor 1.05 and −44,125 with multiple disruptions (M3), and its gap to the bound grows slowly. With spare capacity it shrinks: at factor 2.0 every policy is within 12-14% of the bound, and `none` is within noise of greedy.
* **Transit time** (M5). With same-day transit, greedy is only 3% above the bound. The gap grows with transit time (12%, 24%, 31%), because decisions take effect later and the delay is not visible to the short planning windows. Greedy and `rolling_exact` stay close here (at 2 and 3 transit days the difference is within noise).
* **Demand noise** (M6) raises every policy's cost and gap to the bound; the ordering of the policies does not change.

### 7.3 The bottleneck-aware heuristic: no measurable effect (the hypothesis is not supported)

* `greedy` and `bottleneck_aware_lambda0` made **identical decisions in every run of every experiment**: 25 settings × 20 runs, plus M9's 4 × 40. So weighting the shortage by its penalty (BA's score with λ = 0) changes nothing compared with counting units.
* `bottleneck_aware` with λ > 0 differs from greedy in only 7 of 25 settings, and in each of them only in 1-5 of 20 runs. None of these differences has an interval excluding 0, and none is larger than 1% of the mean cost. In single-disruption settings the runs where it differs are mostly **dearer** (14 dearer runs against 1 cheaper). In M9 (multiple disruptions, 40 seeds) they are mostly cheaper (2-5 cheaper against 2 dearer per λ), and the mean difference is −723 to −1,340 against λ = 0, with intervals of about ±1,500 to ±1,800 that include 0 for every λ.
* **Why.** The policy keeps adding every candidate that passes the shared eligibility test (planned shortage falls and the saving exceeds the cost). The score only orders the candidates, and the order changes the chosen set only when an earlier choice makes a later candidate ineligible. With few candidates per day, which is typical (3 backups, a handful of failed routes), that rarely happens.

Verdict: **helps** in no setting, **hurts** in no setting, **no measurable difference** in all 29. A heuristic that wins in a few individual runs is not evidence for the hypothesis.

### 7.4 Step 5: does the risk signal predict future shortage?

The signal `f` is the planned saturated share used by the bottleneck-aware score: the share of planned flow on route-days at or above 90% utilisation. It is tested against the new backorders of the next 3 days, on the `none` runs (13,500 days from 500 runs) so that recovery actions do not change the future being predicted.

* `f` does correlate with later shortage: Spearman ρ = +0.41. Mean future shortage rises from 106.5 units in the lowest quartile of `f` to 287.2 in the highest. Within a single run the rank correlation is +0.23 ± 0.03.
* But today's backlog predicts the same thing much better (ρ = +0.78), and adding `f` to a linear model with backlog and active disruptions raises R² only from 0.544 to 0.556 (+0.012).

So the signal is **real but almost redundant**: most of what it knows is already in the backlog that every policy plans with. This is consistent with 7.3, where using it as a penalty changed almost no decisions.

### 7.5 Limited look-ahead: value and runtime (M8, M5)

* A look-ahead shorter than the activation lead plus the supply lead (the time from a supplier to a customer) **cannot see any benefit** of an action. At 1, 2 and 3 days it never acted and cost exactly as much as `none`. The same blind spot explains M5: with 2 or 3 transit days per route, look-ahead(5) never acted (+89,683 and +80,787 against greedy, every run dearer).
* At 5 days it acts but undervalues one-off activations, whose benefit continues beyond the window. It is within noise of greedy in most settings, dearer with three failed suppliers (M1, +51,445) and cheaper for route failures (M2, 19 of 20 runs cheaper in each setting).
* At 8 and 12 days it is clearly cheaper than greedy (−25,987 ± 23,539 and −27,074 ± 23,821; 19 and 20 of 20 runs cheaper). On the same 20 scenarios it is also **cheaper than `rolling_exact`** (−2,042 ± 1,711 and −3,129 ± 1,688; 14/1/5 and 18/0/2 runs): seeing the consequences of an action over several days is worth more than an exhaustive choice that only sees the planning window of one day.
* **Runtime cost:** decision time per 30-day run grows roughly as h^1.9 with the horizon h: 3.2, 10.9, 28.9, 97.3, 171.4 and 318.6 ms for h = 1, 2, 3, 5, 8, 12. Greedy needs about 50 ms in the same setting.

### 7.6 Value of information (M10)

With perfect knowledge of the schedule inside the planning window, every policy is cheaper on the multiple-disruption scenario. `none` drops from 331.4k to 233.6k, greedy from 236.7k to 164.7k, and `rolling_exact` from 192.5k to 130.2k, which brings `rolling_exact` to 6% above the clairvoyant bound (from 57%). Under the persistence forecast, most of the gap to the bound is therefore the **value of knowing the future**, not heuristic weakness. Perfect-forecast results are an upper limit on what forecasting could give, and are reported separately from the persistence results everywhere.

### 7.7 Recovery time

Recovery is counted only when the definition is met inside the 30 days. More failures, tighter capacity and longer transit times make recovery later and rarer. With multiple disruptions (M3), at most 9 of 20 runs recover for any policy. `rolling_exact` recovers in the most runs in most settings and usually earliest, for example 20/20 runs at 11.7 days against greedy's 18/20 at 14.3 days in the base setting. Recovery-time means are over different subsets of runs (only the recovered ones), so they must be read together with the recovered counts.

### 7.8 Feasibility

Every run of every policy passed the daily validation and the conservation check (feasibility rate 100%). No policy ever cost less than the clairvoyant lower bound, which the runner checks for and would report as an error.

## 8. Threats to validity (multi-day)

* **One scenario family:** 19-node layered networks, 30 days, 1-day action leads, and failures that start on day 2. Other structures, longer leads or repeated disruptions could rank the policies differently.
* **`rolling_exact` is a reference, not an optimum over time.** It is exhaustive only over today's choices with today's information. Look-ahead(8) and look-ahead(12) beat it, and the clairvoyant bound is not attainable by any online policy (section 8 of `MULTI_DAY_SIMULATION.md`).
* **Skewed differences.** A few scenarios with large shortages dominate the means; this is why intervals are wide even when 19 of 20 runs agree. The run counts are reported for that reason.
* **Many intervals.** There are over 100 paired intervals, so about 1 in 20 would exclude 0 by chance alone. Conclusions rest on patterns across settings, not on single cells.
* **The risk-signal analysis is correlational.** It uses the `none` policy only, and the 3-day target window is one choice among several.
* **Timing:** one machine, wall-clock, small networks.

## 9. Conclusions supported by the multi-day data

1. Paid recovery actions reduce cost strongly once a disruption is more than mild. `none` is dearer than greedy in 21 of 25 settings.
2. Choosing actions by exhaustive search over the day's candidates (`rolling_exact`) is cheaper than greedy in 20 of 25 settings and never dearer. It helps most when capacity is tight, failures are many and penalties are high, and least with spare capacity or long transit times.
3. The bottleneck-aware risk term **made no measurable difference** anywhere (29 settings). Its signal does predict later shortage, but almost nothing beyond today's backlog. The research hypothesis is **not supported** by these experiments.
4. Look-ahead is only useful when its horizon exceeds the action lead plus the supply lead. With 8-12 days it was the cheapest policy tested, at about 3.5 (8 days) to 6.5 (12 days) times greedy's decision time, and its decision time grows about quadratically with the horizon.
5. Under the persistence forecast, most of the distance to the clairvoyant bound is the value of information about the future.
