"""Generate README perf charts from the latest bench CSVs.

Reads the most recent x64 results from tests/bench/results/ and writes
both light and dark variants of each chart:
  docs/perf/comparison{,-dark}.png   - this lib vs HotKeyIt/Yaml
  docs/perf/throughput{,-dark}.png   - this lib's MB/s by case

README uses <picture> with prefers-color-scheme to switch.

Run from the repo root: `python docs/perf/plot.py`
"""

from __future__ import annotations

import csv
import glob
import os
import sys
from collections import defaultdict

import matplotlib.pyplot as plt
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RESULTS = os.path.join(ROOT, "tests", "bench", "results")
OUT = os.path.join(ROOT, "docs", "perf")

# Tuned against GitHub's light (#ffffff) and dark (#0d1117) page bgs.
THEMES = {
    "light": {
        "bg":         "#ffffff",
        "fg":         "#24292f",
        "muted":      "#57606a",
        "grid":       "#d0d7de",
        "ours":       "#2f9e44",
        "hkit":       "#868e96",
        "parse":      "#1c7ed6",
        "dump":       "#e8590c",
        "err":        "#a61e4d",
    },
    "dark": {
        "bg":         "#0d1117",
        "fg":         "#c9d1d9",
        "muted":      "#8b949e",
        "grid":       "#30363d",
        "ours":       "#3fb950",
        "hkit":       "#6e7681",
        "parse":      "#58a6ff",
        "dump":       "#ff7b50",
        "err":        "#f778ba",
    },
}


def apply_theme(fig, axes, t):
    fig.patch.set_facecolor(t["bg"])
    for ax in axes:
        ax.set_facecolor(t["bg"])
        for spine in ax.spines.values():
            spine.set_color(t["muted"])
        ax.tick_params(colors=t["fg"], which="both")
        ax.xaxis.label.set_color(t["fg"])
        ax.yaxis.label.set_color(t["fg"])
        ax.title.set_color(t["fg"])
        for gl in ax.get_xgridlines() + ax.get_ygridlines():
            gl.set_color(t["grid"])


def latest(pattern: str) -> str:
    matches = sorted(glob.glob(os.path.join(RESULTS, pattern)))
    if not matches:
        sys.exit(f"No files matching {pattern} under {RESULTS}")
    return matches[-1]


def read_csv(path: str) -> list[dict]:
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def plot_comparison(t: dict, suffix: str):
    path = latest("compare-*-x64.csv")
    rows = read_csv(path)

    by_case: dict[str, dict[str, dict[str, float | None]]] = defaultdict(
        lambda: {"Parse": {"ours": None, "hkit": None},
                 "Dump":  {"ours": None, "hkit": None}}
    )
    for r in rows:
        op = r["op"]
        if op not in ("Parse", "Dump"):
            continue
        case = r["case"]
        if r["ours_status"] == "ok" and r["ours_ms"]:
            by_case[case][op]["ours"] = float(r["ours_ms"])
        if r["hkit_status"] == "ok" and r["hkit_ms"]:
            by_case[case][op]["hkit"] = float(r["hkit_ms"])

    cases = list(by_case.keys())
    n = len(cases)
    y = np.arange(n)
    bar_h = 0.38

    fig, axes = plt.subplots(1, 2, figsize=(11, max(4.5, 0.42 * n + 1.5)),
                             sharey=True)

    for ax, op in zip(axes, ("Parse", "Dump")):
        ours = [by_case[c][op]["ours"] or np.nan for c in cases]
        hkit = [by_case[c][op]["hkit"] or np.nan for c in cases]

        ax.barh(y - bar_h/2, ours, bar_h, color=t["ours"],
                label="YAML.ahk (this lib)")
        ax.barh(y + bar_h/2, hkit, bar_h, color=t["hkit"],
                label="HotKeyIt/Yaml")

        for i, (o, h) in enumerate(zip(ours, hkit)):
            if o and h and not (np.isnan(o) or np.isnan(h)):
                ratio = h / o
                ax.text(max(o, h) * 1.15, i, f"{ratio:.1f}x",
                        va="center", fontsize=8, color=t["fg"])
            elif h is None or (isinstance(h, float) and np.isnan(h)):
                ax.text(o * 1.15 if o else 0.01, i, "hkit: error",
                        va="center", fontsize=8, color=t["err"])

        ax.set_xscale("log")
        ax.set_title(f"{op} (mean ms, log scale)")
        ax.set_xlabel("milliseconds")
        ax.grid(True, axis="x", which="both", alpha=0.5)
        ax.set_axisbelow(True)

    axes[0].set_yticks(y)
    axes[0].set_yticklabels(cases, fontsize=9)
    axes[0].invert_yaxis()

    apply_theme(fig, axes, t)

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=2,
               bbox_to_anchor=(0.5, 1.02), frameon=False,
               labelcolor=t["fg"])

    fig.suptitle("Parse / Dump time vs HotKeyIt/Yaml - lower is better",
                 y=1.06, fontsize=11, color=t["fg"])
    fig.tight_layout()
    out = os.path.join(OUT, f"comparison{suffix}.png")
    fig.savefig(out, dpi=140, bbox_inches="tight", facecolor=t["bg"])
    plt.close(fig)
    print(f"wrote {out}")


def plot_throughput(t: dict, suffix: str):
    path = latest("bench-*-x64.csv")
    rows = read_csv(path)

    by_case: dict[str, dict[str, float]] = defaultdict(dict)
    for r in rows:
        if r["op"] not in ("Parse", "Dump"):
            continue
        if not r.get("mb_per_s"):
            continue
        by_case[r["case"]][r["op"]] = float(r["mb_per_s"])

    cases = list(by_case.keys())
    n = len(cases)
    y = np.arange(n)
    bar_h = 0.38

    parse = [by_case[c].get("Parse", 0.0) for c in cases]
    dump  = [by_case[c].get("Dump",  0.0) for c in cases]

    fig, ax = plt.subplots(figsize=(9, max(4.5, 0.42 * n + 1.2)))
    ax.barh(y - bar_h/2, parse, bar_h, color=t["parse"], label="Parse")
    ax.barh(y + bar_h/2, dump,  bar_h, color=t["dump"],  label="Dump")

    for i, (p, d) in enumerate(zip(parse, dump)):
        if p:
            ax.text(p + 0.6, i - bar_h/2, f"{p:.1f}",
                    va="center", fontsize=8, color=t["parse"])
        if d:
            ax.text(d + 0.6, i + bar_h/2, f"{d:.1f}",
                    va="center", fontsize=8, color=t["dump"])

    ax.set_yticks(y)
    ax.set_yticklabels(cases, fontsize=9)
    ax.invert_yaxis()
    ax.set_xlabel("MB/s (higher is better)")
    ax.set_title("Throughput by corpus case (x64)")
    ax.grid(True, axis="x", which="both", alpha=0.5)
    ax.set_axisbelow(True)

    apply_theme(fig, [ax], t)

    ax.legend(loc="lower right", frameon=False, labelcolor=t["fg"])

    fig.tight_layout()
    out = os.path.join(OUT, f"throughput{suffix}.png")
    fig.savefig(out, dpi=140, bbox_inches="tight", facecolor=t["bg"])
    plt.close(fig)
    print(f"wrote {out}")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, theme in THEMES.items():
        suffix = "" if name == "light" else f"-{name}"
        plot_comparison(theme, suffix)
        plot_throughput(theme, suffix)
