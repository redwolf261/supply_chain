"""Analysis of the multi-day experiments M1-M10 from experiments/results/multiday/*.csv (written by
scx_multiday_experiments). Nothing is typed in by hand: every number below is computed from those files.

Prints, and saves to <results_dir>/analysis.txt:
  1. per experiment and setting: mean cost with 95% interval, fill rate, recovered runs and mean recovery time, decision
     time, gap to the clairvoyant lower bound;
  2. paired cost comparisons (same scenarios) against a reference policy, with a verdict that only says "differs" when
     the 95% interval of the mean difference excludes 0;
  3. a helps / hurts / no-difference table for the bottleneck-aware heuristic against greedy and against its own
     lambda = 0 ablation, over every setting;
  4. Step 5: does the bottleneck-risk signal (planned saturated share) predict FUTURE shortage, beyond what today's
     backlog already tells the planner?
  5. the look-ahead runtime cost against its horizon.

Usage:  python experiments/scripts/analyze_multiday.py [results_dir]
"""
import io
import math
import sys
from contextlib import redirect_stdout
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze import paired, t95  # noqa: E402  (same t table and pairing code as the single-day analysis)

RESULTS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "results" / "multiday"
IDS = [f"M{i}" for i in range(1, 11)]
FUTURE_DAYS = 3        # risk analysis: shortage over days t+1 .. t+3


def load(exp):
    return (pd.read_csv(RESULTS / f"{exp}_summary.csv"), pd.read_csv(RESULTS / f"{exp}_runs.csv"))


def paired_by_x(runs, a, b):
    """paired() from analyze.py with total_cost; returns {x: (n, mean, half, a_cheaper, a_dearer, ties)}."""
    return {x: (n, m, h, lo, hi, eq) for x, n, m, h, lo, hi, eq in paired(runs, a, b)}


def verdict(n, mean, half):
    if n < 2:
        return "too few runs"
    if mean == 0 and half == 0:
        return "identical"
    if abs(mean) <= half:
        return "no clear difference"
    return "cheaper" if mean < 0 else "dearer"


def section_tables():
    for exp in IDS:
        s, runs = load(exp)
        print(f"\n===== {exp} =====")
        cols = ["x", "label", "runs_completed", "runs_skipped", "cost_mean", "cost_ci95", "fill_rate_mean", "final_backlog_mean",
                "action_cost_mean", "recovered_runs", "recovery_applicable_runs", "recovery_days_mean", "decision_ms_mean",
                "gap_to_bound_percent_mean", "bound_runs", "wins"]
        with pd.option_context("display.width", 260, "display.max_columns", 30, "display.float_format", lambda v: f"{v:,.3f}"):
            print(s[cols].to_string(index=False))
        reference = "bottleneck_aware_lambda0" if exp == "M9" else "greedy"
        for label in sorted(set(runs.label) - {reference}):
            print(f"  paired {label} - {reference}: mean difference +/- 95% half-width, runs cheaper/dearer/equal")
            for x, (n, m, h, lo, hi, eq) in paired_by_x(runs, label, reference).items():
                print(f"    x={x:<7g} n={n:<3d} {m:+11.1f} +/- {h:9.1f}   {lo}/{hi}/{eq}   {verdict(n, m, h)}")


def section_helps_hurts():
    print("\n===== Where does bottleneck_aware help, hurt, or make no difference? =====")
    print("Each line is one experimental setting; 'cheaper'/'dearer' only when the 95% interval of the paired difference excludes 0.")
    for other in ["greedy", "bottleneck_aware_lambda0"]:
        tally = {}
        print(f"\n  bottleneck_aware vs {other}")
        for exp in IDS:
            _, runs = load(exp)
            if not {"bottleneck_aware", other} <= set(runs.label):
                continue
            for x, (n, m, h, lo, hi, eq) in paired_by_x(runs, "bottleneck_aware", other).items():
                v = verdict(n, m, h)
                tally[v] = tally.get(v, 0) + 1
                print(f"    {exp} x={x:<6g} n={n:<3d} diff {m:+10.1f} +/- {h:8.1f}  cheaper/dearer/equal runs {lo}/{hi}/{eq}  -> {v}")
        print(f"    settings by verdict: {tally}")


def spearman(a, b):
    ra, rb = pd.Series(a).rank(), pd.Series(b).rank()
    if ra.std() == 0 or rb.std() == 0:
        return float("nan")
    return float(np.corrcoef(ra, rb)[0, 1])


def r2(X, y):
    X = np.column_stack([np.ones(len(y))] + X)
    coef, *_ = np.linalg.lstsq(X, y, rcond=None)
    resid = y - X @ coef
    return 1.0 - resid.var() / y.var() if y.var() > 0 else float("nan")


def section_risk():
    print("\n===== Step 5: does the bottleneck-risk signal predict future shortage? =====")
    print(f"Signal f_t = planned saturated share on day t (share of planned flow on route-days at >= 90% utilisation),")
    print(f"target y_t = new backorders on days t+1..t+{FUTURE_DAYS}. Policy 'none' only (no recovery actions change the future),")
    print("settings M1-M7 and M10 (M8/M9 repeat M1/M3 scenarios). Days whose window would run past the horizon are dropped.")
    rows = []
    for exp in ["M1", "M2", "M3", "M4", "M5", "M6", "M7", "M10"]:
        d = pd.read_csv(RESULTS / f"{exp}_daily.csv")
        d = d[d.label == "none"].sort_values(["x", "run", "day"])
        for (x, run), g in d.groupby(["x", "run"]):
            nb = g["new_backorders"].to_numpy()
            horizon = len(g)
            for i in range(horizon - FUTURE_DAYS):
                rows.append((exp, x, run, g["planned_saturated_share"].iloc[i], g["backlog"].iloc[i], g["active_disruptions"].iloc[i],
                             nb[i + 1:i + 1 + FUTURE_DAYS].sum()))
    r = pd.DataFrame(rows, columns=["exp", "x", "run", "f", "backlog", "disruptions", "future"])
    print(f"  observations: {len(r)} (days) from {r.groupby(['exp', 'x', 'run']).ngroups} runs")
    print(f"  Spearman(f, future shortage)            = {spearman(r.f, r.future):+.3f}")
    print(f"  Spearman(backlog today, future)          = {spearman(r.backlog, r.future):+.3f}")
    print(f"  Spearman(active disruptions, future)     = {spearman(r.disruptions, r.future):+.3f}")
    y = r.future.to_numpy(dtype=float)
    base = r2([r.backlog.to_numpy(dtype=float), r.disruptions.to_numpy(dtype=float)], y)
    full = r2([r.backlog.to_numpy(dtype=float), r.disruptions.to_numpy(dtype=float), r.f.to_numpy(dtype=float)], y)
    print(f"  linear R^2: backlog + disruptions = {base:.3f};  adding f = {full:.3f}  (increase {full - base:+.3f})")
    # Within-run: does a day with higher f than usual FOR THAT RUN precede more shortage than usual?
    within = []
    for _, g in r.groupby(["exp", "x", "run"]):
        c = spearman(g.f, g.future)
        if not math.isnan(c):
            within.append(c)
    w = np.array(within)
    if len(w) > 1:
        half = t95(len(w) - 1) * w.std(ddof=1) / math.sqrt(len(w))
        print(f"  within-run Spearman, mean over {len(w)} runs = {w.mean():+.3f} +/- {half:.3f} (95%)")
    print("  by quartile of f: mean future shortage (units)")
    r["fq"] = pd.qcut(r.f.rank(method="first"), 4, labels=["Q1 (lowest f)", "Q2", "Q3", "Q4 (highest f)"])
    for q, g in r.groupby("fq", observed=True):
        print(f"    {q:<15} f in [{g.f.min():.2f}, {g.f.max():.2f}]  n={len(g):<5d} mean future shortage {g.future.mean():8.1f}")


def section_lookahead_runtime():
    print("\n===== Look-ahead: value and runtime against its horizon (M8) =====")
    s, runs = load("M8")
    for x, g in s.groupby("x"):
        la = g[g.label == "lookahead"].iloc[0]
        gr = g[g.label == "greedy"].iloc[0]
        n, m, h, lo, hi, eq = paired_by_x(runs, "lookahead", "greedy")[x]
        print(f"  horizon {int(x):>2} days: decision time {la.decision_ms_mean:8.1f} ms per run (greedy {gr.decision_ms_mean:6.1f}),"
              f" cost {la.cost_mean:10.0f} vs greedy {gr.cost_mean:10.0f}; paired {m:+9.1f} +/- {h:8.1f} -> {verdict(n, m, h)}")
    # M8 uses exactly the seeds and scenario of M1 with 2 failed suppliers, so look-ahead can be paired with rolling_exact.
    m1 = pd.read_csv(RESULTS / "M1_runs.csv")
    m1 = m1[m1.x == 2]
    same = (m1[m1.label == "greedy"].set_index("seed").total_cost == runs[(runs.x == runs.x.max()) & (runs.label == "greedy")].set_index("seed").total_cost).all()
    print(f"  M8 and M1 (x=2) are the same scenarios: {bool(same)} (greedy costs identical seed by seed)")
    if same:
        for x in sorted(runs.x.unique()):
            both = pd.concat([runs[(runs.x == x) & (runs.label == "lookahead")].assign(label="la"), m1[m1.label == "rolling_exact"].assign(label="re", x=x)])
            for _, n, m, h, lo, hi, eq in paired(both, "la", "re"):
                print(f"  look-ahead({int(x)}) - rolling_exact: {m:+9.1f} +/- {h:8.1f}  cheaper/dearer/equal {lo}/{hi}/{eq} -> {verdict(n, m, h)}")
    xs = s[s.label == "lookahead"].sort_values("x")
    slope = np.polyfit(np.log(xs.x.to_numpy(float)), np.log(xs.decision_ms_mean.to_numpy(float)), 1)[0]
    print(f"  empirical growth of look-ahead decision time with horizon h: ~ h^{slope:.2f} (log-log slope)")


def main():
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        print(f"Multi-day analysis of {RESULTS}")
        section_tables()
        section_helps_hurts()
        section_risk()
        section_lookahead_runtime()
    text = buffer.getvalue()
    print(text)
    (RESULTS / "analysis.txt").write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
