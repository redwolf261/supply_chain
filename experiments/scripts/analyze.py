"""Prints the key measured numbers of each experiment from experiments/results/*.csv, plus paired comparisons computed
from the raw per-run files. Everything is read from files written by scx_experiments; nothing is typed in by hand.

The 95% intervals use exactly the same Student-t table as the C++ code (backend/src/experiments/Statistics.cpp).

Usage:  python experiments/scripts/analyze.py [results_dir]
"""
import math
import sys
from pathlib import Path

import numpy as np
import pandas as pd

RESULTS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "results"

# Two-sided 95% critical values of Student's t for 1..30 degrees of freedom (identical to Statistics.cpp).
T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
       2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
       2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042]


def t95(df: int) -> float:
    return T95[df - 1] if 1 <= df <= 30 else 1.96


def paired(runs: pd.DataFrame, a: str, b: str):
    """Paired difference in total cost (a - b) over the runs where both completed, for each x value."""
    out = []
    for x, group in runs.groupby("x"):
        pa = group[group.label == a].set_index("run")["total_cost"]
        pb = group[group.label == b].set_index("run")["total_cost"]
        joined = pd.concat([pa, pb], axis=1, keys=["a", "b"]).dropna()
        if joined.empty:
            continue
        d = joined["a"] - joined["b"]
        n = len(d)
        sd = d.std(ddof=1) if n > 1 else 0.0
        half = t95(n - 1) * sd / math.sqrt(n) if n > 1 else 0.0
        out.append((x, n, d.mean(), half, int((d < 0).sum()), int((d > 0).sum()), int((d == 0).sum())))
    return out


def show_paired(runs, a, b, title):
    print(f"\n  {title}: mean cost(a) - cost(b), 95% interval, runs where a cheaper / dearer / equal")
    for x, n, mean, half, lo, hi, eq in paired(runs, a, b):
        verdict = "differs" if n > 1 and abs(mean) > half else "no clear difference"
        print(f"    x={x:<6g} n={n:<4d} {mean:+10.1f} +/- {half:8.1f}   {lo}/{hi}/{eq}   {verdict}")


def loglog_slope(x, y):
    """Empirical growth exponent: slope of log(y) against log(x)."""
    slope, _ = np.polyfit(np.log(np.asarray(x, dtype=float)), np.log(np.asarray(y, dtype=float)), 1)
    return slope


def main():
    for exp in "ABCDEFG":
        summary = pd.read_csv(RESULTS / f"{exp}_summary.csv")
        runs = pd.read_csv(RESULTS / f"{exp}_runs.csv")
        print(f"\n===== Experiment {exp} =====")
        cols = ["x", "label", "runs_completed", "runs_skipped", "cost_mean", "cost_sd", "unmet_mean", "fulfilment_mean", "runtime_ms_mean", "regret_percent_mean", "wins"]
        with pd.option_context("display.width", 250, "display.max_columns", 30, "display.float_format", lambda v: f"{v:,.2f}"):
            print(summary[cols].to_string(index=False))
        if exp in "BCDE":
            show_paired(runs, "greedy", "min_cost_flow", "greedy vs re-routing only")
            show_paired(runs, "bottleneck_aware", "greedy", "bottleneck-aware (lambda=1) vs greedy")
            show_paired(runs, "bottleneck_aware", "bottleneck_aware_lambda0", "bottleneck-aware (lambda=1) vs lambda=0")
            show_paired(runs, "greedy", "exact", "greedy vs exact (only runs where exact was feasible)")
            show_paired(runs, "dijkstra_dispatch", "min_cost_flow", "dispatch vs min-cost flow")
        if exp == "F":
            for lam in ["0.25", "0.5", "1", "2", "5"]:
                show_paired(runs, f"bottleneck_aware(lambda={lam})", "bottleneck_aware(lambda=0)", f"lambda={lam} vs lambda=0")
            show_paired(runs, "bottleneck_aware(lambda=0)", "greedy", "lambda=0 vs plain greedy")
            identical = 0
            lam0 = runs[runs.label == "bottleneck_aware(lambda=0)"].set_index("run")["total_cost"]
            for lam in ["0.25", "0.5", "1", "2", "5"]:
                other = runs[runs.label == f"bottleneck_aware(lambda={lam})"].set_index("run")["total_cost"]
                identical += int((other == lam0).sum())
            total = 5 * len(lam0)
            print(f"\n  runs in which a lambda>0 variant chose a plan of EXACTLY the lambda=0 cost: {identical}/{total} ({100 * identical / total:.0f}%)")
        if exp == "G":
            show_paired(runs, "bottleneck_aware", "greedy", "bottleneck-aware vs greedy")
            show_paired(runs, "greedy", "min_cost_flow", "greedy vs re-routing only")
            show_paired(runs, "greedy", "exact", "greedy vs exact")
        if exp == "A":
            print("\n  mean runtime (ms) and peak heap (KB) by network size:")
            piv = summary.pivot(index="mean_nodes", columns="label", values="runtime_ms_mean")
            print(piv.to_string(float_format=lambda v: f"{v:,.2f}"))
            mem = summary.pivot(index="mean_nodes", columns="label", values="peak_bytes_mean") / 1024.0
            print(mem.to_string(float_format=lambda v: f"{v:,.1f}"))
            print("\n  empirical growth exponent (slope of log runtime vs log nodes, 6 sizes, 5 runs each):")
            for label in piv.columns:
                print(f"    runtime {label:<28s} n^{loglog_slope(piv.index, piv[label]):.2f}     heap n^{loglog_slope(mem.index, mem[label]):.2f}")

    # Why is exact cheaper than greedy even when nothing was disrupted? Look at the plans, not at a guess.
    b = pd.read_csv(RESULTS / "B_runs.csv")
    calm = b[b.x == 0]
    g = calm[calm.label == "greedy"].set_index("run")
    e = calm[calm.label == "exact"].set_index("run")
    better = e[(e.total_cost < g.total_cost.reindex(e.index))]
    print("\n===== Check: exact beating greedy with NO disruption (B, x=0) =====")
    print(f"  runs where exact is cheaper: {len(better)}; of those, greedy took {int((g.reindex(better.index).actions > 0).sum())} actions "
          f"and exact took {better.actions.tolist()} actions; unmet demand greedy={g.reindex(better.index).unmet_demand.tolist()}")


if __name__ == "__main__":
    main()
