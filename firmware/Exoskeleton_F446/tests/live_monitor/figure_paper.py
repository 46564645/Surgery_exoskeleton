"""Paper-style grouped bar figure (IROS 2024 Fig. 8 layout, English only).

Splits each session into motion phases from the elbow angular velocity and
plots mean +/- SD of the cuff force per phase and per condition. All text is
English so the output can go straight into a manuscript.

Usage:
    figure_paper.py "Assist off=build/live-a" "Assist on=build/live-b" \
        --out build/paper_figure --title "Cuff force by motion phase"
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

PHASES = ("Rise", "Hold", "Lower")
COLORS = ("#d62728", "#1f4fd6", "#2ca02c", "#9467bd")
RISE_DEG_S = 10.0
HOLD_DEG_S = 5.0


def read_csv(path):
    if not path.is_file():
        return []
    with path.open(encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def number(row, key):
    value = row.get(key, "")
    if value in ("", None, "na"):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def phase_rows(directory):
    """(phase, force_N, elbow torque Nm) per RUNNING sample with a force value."""
    samples = [r for r in read_csv(directory / "samples.csv") if r.get("phase") == "4"]
    force = read_csv(directory / "force.csv")
    if not samples or not force:
        return []
    times = [float(r["t_s"]) for r in samples]
    out, index = [], 0
    for row in force:
        t = number(row, "t_s")
        value = number(row, "force_N")
        if t is None or value is None or row.get("valid") == "0":
            continue
        while index + 1 < len(times) and abs(times[index + 1] - t) <= abs(times[index] - t):
            index += 1
        if abs(times[index] - t) > 0.2:
            continue
        angle = number(samples[index], "q_el_deg")
        torque = number(samples[index], "el_Nm") or 0.0
        if angle is None:
            continue
        out.append((t, angle, value, torque,
                    samples[index].get("force_active") == "1"))
    rows = []
    for i, (t, angle, value, torque, active) in enumerate(out):
        j = max(0, i - 3)
        dt = t - out[j][0]
        rate = (angle - out[j][1]) / dt if dt > 0.05 else 0.0
        if rate > RISE_DEG_S:
            phase = "Rise"
        elif rate < -RISE_DEG_S:
            phase = "Lower"
        elif abs(rate) <= HOLD_DEG_S:
            phase = "Hold"
        else:
            phase = "Hold"
        rows.append((phase, value, torque, active))
    return rows


def stats(values):
    if not values:
        return None, None, 0
    mean = statistics.fmean(values)
    sd = statistics.pstdev(values) if len(values) > 1 else 0.0
    return mean, sd, len(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("series", nargs="+", metavar="LABEL=DIR",
                        help="一个或多个条件，例如 \"Assist on=build/live-20261022-220349\"")
    parser.add_argument("--out", type=Path, required=True, help="输出目录")
    parser.add_argument("--title", default="Cuff force by motion phase")
    parser.add_argument("--ylabel", default="Cuff force (N)")
    parser.add_argument("--width", type=float, default=3.5)
    parser.add_argument("--height", type=float, default=2.6)
    parser.add_argument("--dpi", type=int, default=300)
    parser.add_argument("--split-force", action="store_true",
                        help="单个会话内按力触发开关拆成两组（boost / no boost）")
    args = parser.parse_args()

    plt.rcParams.update({"font.family": "sans-serif", "font.size": 8,
                         "axes.linewidth": 0.8, "xtick.direction": "in",
                         "ytick.direction": "in", "axes.grid": False})

    conditions = []
    for item in args.series:
        if "=" not in item:
            raise SystemExit(f"需要 LABEL=DIR 形式：{item}")
        label, directory = item.split("=", 1)
        rows = phase_rows(Path(directory))
        if not rows:
            raise SystemExit(f"{directory} 缺少 RUNNING 力数据")
        if args.split_force:
            for suffix, flag in ((" (no boost)", False), (" (boost)", True)):
                part = [r for r in rows if r[3] is flag]
                if part:
                    conditions.append((label + suffix, part))
        else:
            conditions.append((label, rows))

    fig, ax = plt.subplots(figsize=(args.width, args.height))
    positions = range(len(PHASES))
    width = 0.8 / max(1, len(conditions))
    summary = {}
    for k, (label, rows) in enumerate(conditions):
        means, errors, counts = [], [], []
        for phase in PHASES:
            values = [r[1] for r in rows if r[0] == phase]
            mean, sd, count = stats(values)
            means.append(0.0 if mean is None else mean)
            errors.append(0.0 if sd is None else sd)
            counts.append(count)
        offsets = [p - 0.4 + width * (k + 0.5) for p in positions]
        ax.bar(offsets, means, width=width, yerr=errors, capsize=2,
               color=COLORS[k % len(COLORS)], edgecolor="black", linewidth=0.5,
               error_kw={"elinewidth": 0.7, "ecolor": "black"}, label=label)
        summary[label] = {phase: {"mean_N": means[i], "sd_N": errors[i], "n": counts[i]}
                          for i, phase in enumerate(PHASES)}

    ax.set_xticks(list(positions))
    ax.set_xticklabels(PHASES)
    ax.set_ylabel(args.ylabel)
    ax.set_title(args.title, fontsize=8, pad=6)
    ax.axhline(0.0, color="black", linewidth=0.6)
    for spine in ax.spines.values():
        spine.set_visible(True)
    ax.legend(loc="upper right", fontsize=7, frameon=False)
    fig.tight_layout()

    args.out.mkdir(parents=True, exist_ok=True)
    outputs = []
    for suffix in ("pdf", "svg", "png"):
        path = args.out / f"figure_paper.{suffix}"
        fig.savefig(path, dpi=args.dpi if suffix == "png" else None)
        outputs.append(path)
    plt.close(fig)
    meta = {"title": args.title, "ylabel": args.ylabel, "phases": list(PHASES),
            "conditions": summary, "outputs": [str(p) for p in outputs]}
    (args.out / "figure_paper.json").write_text(
        json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in summary.items()}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
