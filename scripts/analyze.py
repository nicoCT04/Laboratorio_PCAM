#!/usr/bin/env python3
import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent.parent
CSV_PATH = ROOT / "results.csv"
OUT_DIR = ROOT / "results"

SURFACE = "#fcfcfb"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID = "#e4e3df"
NEUTRAL = "#9a9893"
COLORS = {"parallel": "#2a78d6", "optimized": "#eb6834", "atomic": "#1baf7a"}
LABELS = {
    "sequential": "A secuencial",
    "parallel": "B directa",
    "optimized": "C pares (privados)",
    "atomic": "C pares (atomic)",
}
SCHEDULE_ORDER = [
    ("static", "default"),
    ("static", "8"),
    ("static", "64"),
    ("dynamic", "default"),
    ("dynamic", "8"),
    ("dynamic", "64"),
    ("guided", "default"),
    ("guided", "8"),
    ("guided", "64"),
]


def load_rows(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for row in rows:
        row["threads"] = int(row["threads"])
        row["avg_time"] = float(row["avg_time"])
        row["speedup"] = float(row["speedup"])
        row["efficiency"] = float(row["efficiency"])
    return rows


def find(rows, version, threads, schedule, chunk):
    for row in rows:
        if (
            row["version"] == version
            and row["threads"] == threads
            and row["schedule"] == schedule
            and row["chunk"] == chunk
        ):
            return row
    return None


def style_axes(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9, length=0)
    ax.grid(color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def new_figure(width, height, ncols=1):
    fig, axes = plt.subplots(1, ncols, figsize=(width, height), sharex=ncols > 1)
    fig.patch.set_facecolor(SURFACE)
    return fig, axes


def plot_speedup(rows, n, steps):
    fig, ax = new_figure(7.2, 4.2)
    style_axes(ax)
    all_threads = sorted({r["threads"] for r in rows if r["version"] in ("parallel", "optimized")})
    ax.plot(all_threads, all_threads, color=NEUTRAL, linewidth=1.2, linestyle="--", zorder=1)
    ax.annotate("ideal lineal", (all_threads[-2], all_threads[-2]), xytext=(6, -2),
                textcoords="offset points", color=TEXT_SECONDARY, fontsize=9)
    for version in ("parallel", "optimized"):
        points = sorted(
            (r["threads"], r["speedup"])
            for r in rows
            if r["version"] == version and r["schedule"] == "static" and r["chunk"] == "default"
        )
        xs, ys = zip(*points)
        ax.plot(xs, ys, color=COLORS[version], linewidth=2, marker="o", markersize=7,
                markeredgecolor=SURFACE, markeredgewidth=2, label=LABELS[version], zorder=3)
        ax.annotate(f"{ys[-1]:.1f}x", (xs[-1], ys[-1]), xytext=(8, 0), textcoords="offset points",
                    color=TEXT_PRIMARY, fontsize=9, va="center")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=2)
    ax.set_xticks(all_threads, [str(t) for t in all_threads])
    ticks = [0.5, 1, 2, 4, 8, 16, 32]
    ax.set_yticks(ticks, [f"{t:g}" for t in ticks])
    ax.set_xlabel("Threads", color=TEXT_SECONDARY)
    ax.set_ylabel("Speedup  S(p) = T1 / Tp", color=TEXT_SECONDARY)
    ax.set_title(f"Speedup frente a la secuencial (schedule static default, N={n}, {steps} pasos)",
                 color=TEXT_PRIMARY, fontsize=11, loc="left")
    ax.legend(frameon=False, labelcolor=TEXT_PRIMARY, fontsize=9, loc="upper left")
    fig.tight_layout()
    fig.savefig(OUT_DIR / "speedup_vs_threads.png", dpi=160)
    plt.close(fig)


def plot_schedules(rows, n, steps):
    fig, axes = new_figure(10, 4.4, ncols=2)
    labels = [f"{s} {c}" for s, c in SCHEDULE_ORDER]
    for ax, version in zip(axes, ("parallel", "optimized")):
        style_axes(ax)
        ax.grid(axis="y", visible=False)
        times = [find(rows, version, 8, s, c) for s, c in SCHEDULE_ORDER]
        values = [r["avg_time"] if r else 0 for r in times]
        best = min(v for v in values if v > 0)
        ys = range(len(labels))
        ax.barh(ys, values, height=0.68, color=COLORS[version], edgecolor=SURFACE, linewidth=2)
        for y, v in zip(ys, values):
            weight = "bold" if v == best else "normal"
            ax.text(v, y, f" {v:.3f} s", va="center", fontsize=8.5, color=TEXT_PRIMARY,
                    fontweight=weight)
        ax.set_yticks(list(ys), labels)
        ax.invert_yaxis()
        ax.set_title(LABELS[version], color=TEXT_PRIMARY, fontsize=10.5, loc="left")
        ax.set_xlabel("Tiempo promedio (s)", color=TEXT_SECONDARY)
    max_value = max(r["avg_time"] for r in rows if r["threads"] == 8 and r["version"] != "atomic")
    axes[0].set_xlim(0, max_value * 1.25)
    fig.suptitle(f"Schedule y chunk con 8 threads (N={n}, {steps} pasos)", color=TEXT_PRIMARY,
                 fontsize=11, x=0.01, ha="left")
    fig.tight_layout()
    fig.savefig(OUT_DIR / "schedules_8_threads.png", dpi=160)
    plt.close(fig)


def plot_versions(rows, n, steps):
    fig, ax = new_figure(7.2, 4.1)
    style_axes(ax)
    ax.grid(axis="y", visible=False)
    picks = [
        ("sequential", 1, "sequential", "-"),
        ("parallel", 1, "static", "default"),
        ("optimized", 1, "static", "default"),
        ("parallel", 8, "static", "default"),
        ("optimized", 8, "static", "default"),
        ("optimized", 8, "dynamic", "64"),
        ("atomic", 8, "static", "default"),
        ("atomic", 8, "dynamic", "64"),
    ]
    entries = [find(rows, *p) for p in picks]
    entries = [e for e in entries if e]
    labels = []
    for e in entries:
        config = "" if e["version"] == "sequential" else f" · {e['threads']}T {e['schedule']} {e['chunk']}"
        labels.append(LABELS[e["version"]] + config)
    values = [e["avg_time"] for e in entries]
    colors = [COLORS.get(e["version"], NEUTRAL) for e in entries]
    ys = range(len(entries))
    ax.barh(ys, values, height=0.68, color=colors, edgecolor=SURFACE, linewidth=2)
    for y, v in zip(ys, values):
        ax.text(v, y, f" {v:.3f} s", va="center", fontsize=8.5, color=TEXT_PRIMARY)
    ax.set_yticks(list(ys), labels)
    ax.invert_yaxis()
    ax.set_xlim(0, max(values) * 1.18)
    ax.set_xlabel("Tiempo promedio (s)", color=TEXT_SECONDARY)
    ax.set_title(f"Directa (B) frente a pares (C) y atomic (N={n}, {steps} pasos)",
                 color=TEXT_PRIMARY, fontsize=11, loc="left")
    fig.tight_layout()
    fig.savefig(OUT_DIR / "parallel_vs_optimized.png", dpi=160)
    plt.close(fig)


def read_problem_size():
    env_path = OUT_DIR / "environment.txt"
    if env_path.exists():
        for line in env_path.read_text().splitlines():
            if line.startswith("N:"):
                parts = line.replace("|", "").split()
                return parts[1], parts[3]
    return "?", "?"


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else CSV_PATH
    rows = load_rows(path)
    n, steps = read_problem_size()
    plot_speedup(rows, n, steps)
    plot_schedules(rows, n, steps)
    plot_versions(rows, n, steps)
    print("Gráficas en results/: speedup_vs_threads.png, schedules_8_threads.png, "
          "parallel_vs_optimized.png")


if __name__ == "__main__":
    main()
