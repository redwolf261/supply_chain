"""Draws charts from the measured experiment files (experiments/results/*.csv). Nothing is typed in by hand.

Usage:  python experiments/scripts/plot_results.py [results_dir]
Writes PNG files to <results_dir>/plots/.

Line charts show the mean with a shaded 95% confidence band (Student t, same table as the C++ code). Strategy colors are
fixed per strategy in every chart (validated categorical palette, fixed order).
"""
import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze import paired, t95  # noqa: E402  (shares the exact t table and pairing code)

RESULTS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "results"
OUT = RESULTS / "plots"
OUT.mkdir(parents=True, exist_ok=True)

COLORS = {"min_cost_flow": "#2a78d6", "greedy": "#eb6834", "bottleneck_aware": "#1baf7a",
          "bottleneck_aware_lambda0": "#6250d6", "dijkstra_dispatch": "#e87ba4", "exact": "#008300"}
NAMES = {"min_cost_flow": "Min-cost flow (no actions)", "greedy": "Greedy", "bottleneck_aware": "Bottleneck-aware (λ=1)",
         "bottleneck_aware_lambda0": "Bottleneck-aware (λ=0)", "dijkstra_dispatch": "Dijkstra dispatch", "exact": "Exact (exhaustive)"}
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e6e4de"

plt.rcParams.update({"figure.facecolor": "white", "axes.facecolor": "white", "axes.edgecolor": "#c9c7bf", "axes.labelcolor": INK,
                     "text.color": INK, "xtick.color": MUTED, "ytick.color": MUTED, "axes.grid": True, "grid.color": GRID,
                     "axes.spines.top": False, "axes.spines.right": False, "font.size": 10, "axes.titleweight": "bold"})


def band(summary, label, value, sd):
    s = summary[summary.label == label].sort_values("x")
    # A setting where a strategy was skipped in some runs (exact search with too many candidate actions) averages over a
    # DIFFERENT, easier subset of scenarios, so it is not comparable with the others: leave those points out.
    s = s[s["runs_skipped"] == 0]
    n = s["runs_completed"].to_numpy()
    half = np.array([t95(int(k) - 1) * sd_ / math.sqrt(k) if k > 1 else 0.0 for k, sd_ in zip(n, s[sd])])
    return s["x"].to_numpy(), s[value].to_numpy(), half


# Strategies whose curves nearly coincide get different line widths and markers, drawn widest first, so every one
# stays visible even when they overlap (which is itself a result).
STYLE = {"exact": dict(lw=7, alpha=0.35, marker="o", ms=10, zorder=1),
         "greedy": dict(lw=2.6, marker="o", ms=7, zorder=3),
         "bottleneck_aware": dict(lw=1.6, ls="--", marker="s", ms=4, zorder=4),
         "bottleneck_aware_lambda0": dict(lw=1.6, ls=":", marker="^", ms=4, zorder=4),
         "min_cost_flow": dict(lw=2, marker="o", ms=5, zorder=2),
         "dijkstra_dispatch": dict(lw=2, marker="v", ms=5, zorder=2)}


def line_chart(exp, summary, labels, value, sd, ylabel, title, xlabel, filename, logx=False, logy=False, bands=True):
    fig, ax = plt.subplots(figsize=(7.4, 4.4))
    for label in ["exact"] + [l for l in labels if l != "exact"]:
        if label not in labels or label not in set(summary.label):
            continue
        x, y, half = band(summary, label, value, sd)
        style = dict(STYLE.get(label, dict(lw=2, marker="o", ms=5, zorder=2)))
        z = style.pop("zorder")
        ax.plot(x, y, color=COLORS.get(label, "#555"), label=NAMES.get(label, label), markeredgecolor="white", markeredgewidth=1.0, zorder=z, **style)
        if bands and label != "exact":
            ax.fill_between(x, y - half, y + half, color=COLORS.get(label, "#555"), alpha=0.12, linewidth=0, zorder=0)
    if logx: ax.set_xscale("log")
    if logy: ax.set_yscale("log")
    ax.set_xlabel(xlabel); ax.set_ylabel(ylabel); ax.set_title(title, loc="left")
    skipped = summary[(summary.label == "exact") & (summary.runs_skipped > 0)]
    if "exact" in labels and len(skipped):
        ax.annotate("exact omitted where it could not run on every scenario", xy=(0.99, 0.02), xycoords="axes fraction", ha="right", fontsize=8, color=MUTED)
    ax.legend(frameon=False, fontsize=9)
    fig.tight_layout()
    fig.savefig(OUT / filename, dpi=140)
    plt.close(fig)


def main():
    q = {"A": "How running time and heap grow with network size", "B": "Disruption severity", "C": "Supplier failures", "D": "Route bottlenecks", "E": "Demand spikes"}
    sweep_labels = ["dijkstra_dispatch", "min_cost_flow", "greedy", "bottleneck_aware", "exact"]
    xlabels = {"B": "disruption severity", "C": "number of failed suppliers (of 6)", "D": "capacity lost on the busiest quarter of routes (%)", "E": "demand increase (%)"}

    # A: runtime and memory against network size (log-log so power laws are straight lines)
    a = pd.read_csv(RESULTS / "A_summary.csv")
    a["x"] = a["mean_nodes"]
    a["peak_kb"] = a["peak_bytes_mean"] / 1024.0
    a["peak_kb_sd"] = 0.0
    line_chart("A", a, ["dijkstra_dispatch", "min_cost_flow", "greedy", "bottleneck_aware"], "runtime_ms_mean", "runtime_ms_sd", "runtime per scenario (ms, log scale)",
               "A. Running time vs network size (mean of 5 runs per size)", "nodes (log scale)", "A_runtime.png", logx=True, logy=True, bands=False)
    line_chart("A", a, ["dijkstra_dispatch", "min_cost_flow", "greedy", "bottleneck_aware"], "peak_kb", "peak_kb_sd", "peak heap per scenario (KB, log scale)",
               "A. Peak heap vs network size (mean of 5 runs per size)", "nodes (log scale)", "A_memory.png", logx=True, logy=True, bands=False)

    for exp in "BCDE":
        s = pd.read_csv(RESULTS / f"{exp}_summary.csv")
        line_chart(exp, s, sweep_labels, "cost_mean", "cost_sd", "total cost (mean, 95% band)", f"{exp}. Total cost: {q[exp].lower()}", xlabels[exp], f"{exp}_cost.png")
        s = s.assign(unmet_pct=100 * (1 - s["fulfilment_mean"]), unmet_pct_sd=100 * s["fulfilment_sd"])
        line_chart(exp, s, sweep_labels, "unmet_pct", "unmet_pct_sd", "unmet demand (% of total, mean, 95% band)", f"{exp}. Unmet demand: {q[exp].lower()}", xlabels[exp], f"{exp}_unmet.png")

    # F: paired difference of each lambda against lambda=0, with 95% intervals
    runs = pd.read_csv(RESULTS / "F_runs.csv")
    rows = [(f"λ={lam}", *paired(runs, f"bottleneck_aware(lambda={lam})", "bottleneck_aware(lambda=0)")[0][1:4]) for lam in ["0.25", "0.5", "1", "2", "5"]]
    fig, ax = plt.subplots(figsize=(7.4, 4.2))
    names = [r[0] for r in rows]
    means = [r[2] for r in rows]
    halves = [r[3] for r in rows]
    ax.bar(names, means, yerr=halves, capsize=5, color="#6250d6", alpha=0.85, edgecolor="white", linewidth=2, ecolor=INK)
    ax.axhline(0, color=INK, lw=1)
    n = rows[0][1]
    ax.set_ylabel("mean cost(λ) − cost(λ=0)   (negative = cheaper)")
    ax.set_title(f"F. Bottleneck-risk term vs none ({n} paired runs, 95% CI)", loc="left", fontsize=10)
    fig.tight_layout(); fig.savefig(OUT / "F_ablation.png", dpi=140); plt.close(fig)

    # G: distribution of per-run total cost
    g = pd.read_csv(RESULTS / "G_runs.csv")
    order = [l for l in ["dijkstra_dispatch", "min_cost_flow", "greedy", "bottleneck_aware", "bottleneck_aware_lambda0", "exact"] if l in set(g.label)]
    fig, ax = plt.subplots(figsize=(8.4, 4.6))
    data = [g[(g.label == l) & (g.skipped == 0)]["total_cost"].to_numpy() for l in order]
    box = ax.boxplot(data, patch_artist=True, widths=0.55, medianprops={"color": INK, "lw": 1.5}, flierprops={"marker": "o", "ms": 3, "alpha": 0.5})
    for patch, l in zip(box["boxes"], order):
        patch.set_facecolor(COLORS[l]); patch.set_alpha(0.8); patch.set_edgecolor("white")
    ax.set_xticks(range(1, len(order) + 1))
    ax.set_xticklabels([NAMES[l].replace(" (", "\n(") for l in order], fontsize=8)
    ax.set_ylabel("total cost per scenario")
    ax.set_title(f"G. {len(data[0])} random scenarios at severity 0.4: spread of total cost", loc="left")
    fig.tight_layout(); fig.savefig(OUT / "G_distribution.png", dpi=140); plt.close(fig)
    print("wrote", ", ".join(sorted(p.name for p in OUT.glob("*.png"))))


if __name__ == "__main__":
    main()
