"""Charts for the multi-day experiments, drawn from experiments/results/multiday/*.csv. Nothing is typed in by hand.

Usage:  python experiments/scripts/plot_multiday.py [results_dir]
Writes PNG files to <results_dir>/plots/.

Same palette and style as plot_results.py (validated categorical palette, fixed colour per policy). Lines show the mean
with a shaded 95% confidence band; every chart has a legend and policies also differ by marker and line style, so no
identity depends on colour alone.
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
from analyze import paired, t95  # noqa: E402

RESULTS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "results" / "multiday"
OUT = RESULTS / "plots"
OUT.mkdir(parents=True, exist_ok=True)

ORDER = ["none", "greedy", "bottleneck_aware", "bottleneck_aware_lambda0", "lookahead", "rolling_exact"]
COLORS = {"none": "#2a78d6", "greedy": "#eb6834", "bottleneck_aware": "#1baf7a", "bottleneck_aware_lambda0": "#6250d6",
          "lookahead": "#e87ba4", "rolling_exact": "#008300"}
NAMES = {"none": "No actions (re-plan only)", "greedy": "Greedy", "bottleneck_aware": "Bottleneck-aware (λ=1)",
         "bottleneck_aware_lambda0": "Bottleneck-aware (λ=0)", "lookahead": "Look-ahead (5 days)", "rolling_exact": "Rolling exact"}
STYLE = {"rolling_exact": dict(lw=7, alpha=0.35, marker="o", ms=10, zorder=1),
         "greedy": dict(lw=2.6, marker="o", ms=7, zorder=3),
         "bottleneck_aware": dict(lw=1.6, ls="--", marker="s", ms=4, zorder=4),
         "bottleneck_aware_lambda0": dict(lw=1.6, ls=":", marker="^", ms=4, zorder=4),
         "none": dict(lw=2, marker="o", ms=5, zorder=2),
         "lookahead": dict(lw=2, marker="v", ms=6, zorder=2)}
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e6e4de"

plt.rcParams.update({"figure.facecolor": "white", "axes.facecolor": "white", "axes.edgecolor": "#c9c7bf", "axes.labelcolor": INK,
                     "text.color": INK, "xtick.color": MUTED, "ytick.color": MUTED, "axes.grid": True, "grid.color": GRID,
                     "axes.spines.top": False, "axes.spines.right": False, "font.size": 10, "axes.titleweight": "bold"})

TITLES = {"M1": "failed regular suppliers (of 4)", "M2": "failed routes", "M4": "capacity factor", "M5": "transit days on every route",
          "M6": "demand amplitude", "M7": "shortage-penalty scale"}


def style(label):
    s = dict(STYLE.get(label, dict(lw=2, marker="o", ms=5, zorder=2)))
    return s.pop("zorder"), s


def cost_chart(exp):
    s = pd.read_csv(RESULTS / f"{exp}_summary.csv")
    fig, ax = plt.subplots(figsize=(7.4, 4.4))
    for label in ORDER:
        g = s[(s.label == label) & (s.runs_skipped == 0)].sort_values("x")
        if g.empty:
            continue
        z, st = style(label)
        x, y, half = g.x.to_numpy(), g.cost_mean.to_numpy(), g.cost_ci95.to_numpy()
        ax.plot(x, y, color=COLORS[label], label=NAMES[label], markeredgecolor="white", markeredgewidth=1.0, zorder=z, **st)
        if label != "rolling_exact":
            ax.fill_between(x, y - half, y + half, color=COLORS[label], alpha=0.12, linewidth=0, zorder=0)
    n = int(s.runs_completed.max())
    ax.set_xticks(sorted(s.x.unique()))
    ax.set_xlabel(TITLES[exp]); ax.set_ylabel("total cost over 30 days (mean, unpaired 95% band)")
    ax.set_title(f"{exp}. Total cost vs {TITLES[exp]} ({n} seeds per point)", loc="left", fontsize=10)
    ax.legend(frameon=False, fontsize=8.5)
    ax.annotate("same seeds for every policy: see analysis.txt for paired differences", xy=(0.99, 0.01), xycoords="axes fraction", ha="right", fontsize=7.5, color=MUTED)
    fig.tight_layout(); fig.savefig(OUT / f"{exp}_cost.png", dpi=140); plt.close(fig)


def lookahead_chart():
    s = pd.read_csv(RESULTS / "M8_summary.csv")
    fig, axes = plt.subplots(1, 2, figsize=(10.4, 4.2))
    for label in ["none", "greedy", "lookahead"]:
        g = s[s.label == label].sort_values("x")
        z, st = style(label)
        axes[0].plot(g.x, g.cost_mean, color=COLORS[label], label=NAMES[label].replace(" (5 days)", ""), zorder=z, markeredgecolor="white", **st)
        axes[0].fill_between(g.x, g.cost_mean - g.cost_ci95, g.cost_mean + g.cost_ci95, color=COLORS[label], alpha=0.12, linewidth=0)
    axes[0].set_xticks(sorted(s.x.unique())); axes[1].set_xticks(sorted(s.x.unique()))
    axes[0].set_xlabel("look-ahead horizon (days)"); axes[0].set_ylabel("total cost (mean, 95% band)")
    axes[0].set_title("M8. Cost vs look-ahead horizon", loc="left", fontsize=10)
    axes[0].legend(frameon=False, fontsize=8.5)
    for label in ["greedy", "lookahead"]:
        g = s[s.label == label].sort_values("x")
        z, st = style(label)
        axes[1].plot(g.x, g.decision_ms_mean, color=COLORS[label], label=NAMES[label].replace(" (5 days)", ""), zorder=z, markeredgecolor="white", **st)
    axes[1].set_xlabel("look-ahead horizon (days)"); axes[1].set_ylabel("decision time per 30-day run (ms)")
    axes[1].set_title("M8. Decision time vs look-ahead horizon", loc="left", fontsize=10)
    axes[1].legend(frameon=False, fontsize=8.5)
    fig.tight_layout(); fig.savefig(OUT / "M8_lookahead.png", dpi=140); plt.close(fig)


def ablation_chart():
    runs = pd.read_csv(RESULTS / "M9_runs.csv")
    rows = paired(runs, "bottleneck_aware", "bottleneck_aware_lambda0")
    fig, ax = plt.subplots(figsize=(7.4, 4.2))
    names = [f"λ={x:g}" for x, *_ in rows]
    means = [m for _, _, m, *_ in rows]
    halves = [h for _, _, _, h, *_ in rows]
    ax.bar(names, means, yerr=halves, capsize=5, color=COLORS["bottleneck_aware_lambda0"], alpha=0.85, edgecolor="white", linewidth=2, ecolor=INK)
    ax.axhline(0, color=INK, lw=1)
    for i, (_, n, m, h, lo, hi, eq) in enumerate(rows):
        ax.annotate(f"{lo} cheaper / {hi} dearer / {eq} equal", xy=(i, 0), xytext=(0, -14), textcoords="offset points", ha="center", fontsize=8, color=MUTED)
    ax.set_ylabel("mean cost(λ) − cost(λ=0)   (negative = cheaper)")
    ax.set_title(f"M9. Bottleneck-risk term vs none ({rows[0][1]} paired runs, 95% CI)", loc="left", fontsize=10)
    fig.tight_layout(); fig.savefig(OUT / "M9_ablation.png", dpi=140); plt.close(fig)


def forecast_chart():
    s = pd.read_csv(RESULTS / "M10_summary.csv")
    labels = [l for l in ORDER if l in set(s.label)]
    fig, ax = plt.subplots(figsize=(8.4, 4.2))
    width = 0.38
    for j, (x, name, alpha) in enumerate([(0, "persistence forecast", 0.55), (1, "perfect forecast", 1.0)]):
        g = s[s.x == x].set_index("label").loc[labels]
        pos = np.arange(len(labels)) + (j - 0.5) * width
        ax.bar(pos, g.cost_mean, width=width - 0.04, yerr=g.cost_ci95, capsize=3, color=[COLORS[l] for l in labels], alpha=alpha,
               edgecolor="white", linewidth=2, ecolor=INK, label=name, hatch=None if j else "//")
    ax.set_xticks(np.arange(len(labels)))
    ax.set_xticklabels([NAMES[l].replace(" (", "\n(") for l in labels], fontsize=8)
    ax.set_ylabel("total cost (mean, 95% CI)")
    ax.set_title("M10. Value of perfect knowledge of the schedule (multiple disruptions)", loc="left", fontsize=10)
    ax.legend(frameon=False, fontsize=8.5)
    fig.tight_layout(); fig.savefig(OUT / "M10_forecast.png", dpi=140); plt.close(fig)


def timeline_chart():
    d = pd.read_csv(RESULTS / "M3_daily.csv")
    d = d[d.run == 0]
    fig, axes = plt.subplots(3, 1, figsize=(8.4, 9.0), sharex=True)
    base = d[d.label == "none"].sort_values("day")
    axes[0].plot(base.day, base.normal_service, color=MUTED, lw=1.5, ls="--", label="normal service (no disruption)", zorder=5)
    for label in ORDER:
        g = d[d.label == label].sort_values("day")
        if g.empty:
            continue
        z, st = style(label)
        st = {k: v for k, v in st.items() if k not in ("marker", "ms")}
        axes[0].plot(g.day, g.service_level, color=COLORS[label], label=NAMES[label], zorder=z, **st)
        axes[1].plot(g.day, g.backlog, color=COLORS[label], zorder=z, **st)
        axes[2].plot(g.day, g.cumulative_cost / 1000.0, color=COLORS[label], zorder=z, **st)
    disrupted = base[base.active_disruptions > 0].day
    for ax in axes:
        if len(disrupted):
            ax.axvspan(disrupted.min() - 0.5, disrupted.max() + 0.5, color="#c9c7bf", alpha=0.25, linewidth=0)
    axes[0].set_ylabel("daily service level"); axes[1].set_ylabel("backlog (units)"); axes[2].set_ylabel("cumulative cost (thousands)")
    axes[2].set_xlabel("day (shaded: at least one supplier or route down)")
    axes[0].set_title("M3, seed 5000: one 30-day run of every policy on the same scenario", loc="left", fontsize=10)
    axes[0].legend(frameon=False, fontsize=8, ncol=2)
    fig.tight_layout(); fig.savefig(OUT / "M3_timeline.png", dpi=140); plt.close(fig)


def risk_chart():
    rows = []
    for exp in ["M1", "M2", "M3", "M4", "M5", "M6", "M7", "M10"]:
        d = pd.read_csv(RESULTS / f"{exp}_daily.csv")
        d = d[d.label == "none"].sort_values(["x", "run", "day"])
        for _, g in d.groupby(["x", "run"]):
            nb = g.new_backorders.to_numpy()
            f = g.planned_saturated_share.to_numpy()
            for i in range(len(g) - 3):
                rows.append((f[i], nb[i + 1:i + 4].sum()))
    r = pd.DataFrame(rows, columns=["f", "future"])
    bins = np.linspace(0, 1, 11)
    r["bin"] = pd.cut(r.f, bins, include_lowest=True)
    g = r.groupby("bin", observed=True).future.agg(["mean", "std", "count"]).reset_index()
    g = g[g["count"] >= 5]
    centers = [b.mid for b in g["bin"]]
    half = [t95(int(n) - 1) * sd / math.sqrt(n) if n > 1 else 0 for sd, n in zip(g["std"], g["count"])]
    fig, ax = plt.subplots(figsize=(7.4, 4.2))
    ax.bar(centers, g["mean"], width=0.085, yerr=half, capsize=3, color=COLORS["bottleneck_aware"], alpha=0.85, edgecolor="white", linewidth=2, ecolor=INK)
    for c, m, n in zip(centers, g["mean"], g["count"]):
        ax.annotate(f"n={int(n)}", xy=(c, 0), xytext=(0, 3), textcoords="offset points", ha="center", fontsize=7, color="white")
    ax.set_xlabel("planned saturated share on day t (the bottleneck-risk signal f)")
    ax.set_ylabel("new backorders on days t+1..t+3 (mean, 95% CI)")
    ax.set_title("Step 5. Risk signal vs later shortage (policy 'none', all settings)", loc="left", fontsize=10)
    fig.tight_layout(); fig.savefig(OUT / "risk_signal.png", dpi=140); plt.close(fig)


def main():
    for exp in TITLES:
        cost_chart(exp)
    lookahead_chart()
    ablation_chart()
    forecast_chart()
    timeline_chart()
    risk_chart()
    print(f"plots written to {OUT}")


if __name__ == "__main__":
    main()
