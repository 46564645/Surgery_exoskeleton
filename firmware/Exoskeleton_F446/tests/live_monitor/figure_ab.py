"""Two-panel paper figure: (a) gravity-compensation law, (b) a live excerpt.

(a) assist torque against joint angle with the ideal sin model, so the control
    law and its saturation are visible at a glance.
(b) a short excerpt of the same session: joint angles and cuff force over time.

English labels only, vector output, curves collapsed into one SVG layer.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from svg_layers import single_curve_layer  # noqa: E402

TEXT = {"angle": "Joint angle (deg)", "time": "Time (s)", "torque": "Assist torque (N·m)",
        "force": "Cuff force (N)", "shoulder": "Shoulder", "elbow": "Elbow",
        "model": "ideal sin model", "shoulder_angle": "Shoulder joint angle (deg)",
        "forearm_angle": "Forearm angle, shoulder + elbow (deg)"}


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


def load(directory):
    samples = [r for r in read_csv(directory / "samples.csv") if r.get("phase") == "4"]
    force = read_csv(directory / "force.csv")
    return samples, force


def force_series(samples, force):
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
        out.append((t, value))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--amplitude", type=float, default=1.64, help="肩部理论幅值 N·m")
    parser.add_argument("--elbow-amplitude", type=float, default=0.28, help="肘部理论幅值 N·m")
    parser.add_argument("--excerpt", type=float, default=15.0, help="(b) 片段长度 s")
    parser.add_argument("--start", type=float, default=None, help="(b) 片段起点 s")
    parser.add_argument("--width", type=float, default=7.0)
    parser.add_argument("--height", type=float, default=4.4)
    parser.add_argument("--dpi", type=int, default=300)
    args = parser.parse_args()

    samples, force = load(args.directory)
    if not samples:
        raise SystemExit("缺少 RUNNING 样本")
    rows = []
    for row in samples:
        q_sh = number(row, "q_sh_deg")
        q_el = number(row, "q_el_deg")
        tau_sh = number(row, "sh_joint_Nm")
        tau_el = number(row, "el_Nm")
        if None in (q_sh, q_el, tau_sh, tau_el):
            continue
        rows.append((float(row["t_s"]), q_sh, q_el, tau_sh, tau_el))

    plt.rcParams.update({"font.family": "sans-serif", "font.size": 8,
                         "axes.linewidth": 0.8, "xtick.direction": "in",
                         "ytick.direction": "in"})
    fig, axes = plt.subplots(2, 2, figsize=(args.width, args.height),
                             gridspec_kw={"width_ratios": [1.0, 1.15]})
    fig.subplots_adjust(left=0.085, right=0.975, top=0.86, bottom=0.10,
                        hspace=0.42, wspace=0.34)
    ax_sh, ax_el = axes[0][0], axes[1][0]
    ax_angle, ax_force = axes[0][1], axes[1][1]

    for axis, key_x, key_y, amp, xlabel in (
            (ax_sh, 1, 3, args.amplitude, TEXT["shoulder_angle"]),
            (ax_el, 2, 4, args.elbow_amplitude, TEXT["forearm_angle"])):
        xs = [r[key_x] if key_x != 2 else r[1] + r[2] for r in rows]
        ys = [r[key_y] for r in rows]
        axis.plot(xs, ys, color="#1f6fb2" if axis is ax_sh else "#d9822b",
                  lw=1.0, label=TEXT["shoulder"] if axis is ax_sh else TEXT["elbow"])
        lo, hi = int(min(xs)) - 3, int(max(xs)) + 3
        grid = [math.radians(v) for v in range(lo, hi + 1)]
        axis.plot([math.degrees(a) for a in grid], [amp * math.sin(a) for a in grid],
                  color="#888888", lw=1.0, ls="--", label=TEXT["model"])
        axis.set_xlabel(xlabel)
        axis.set_ylabel(TEXT["torque"])
        axis.grid(True, color="#dddddd", lw=0.5)
        axis.legend(loc="lower right", fontsize=7, frameon=False)
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)

    t0 = args.start if args.start is not None else rows[0][0]
    t1 = t0 + args.excerpt
    part = [r for r in rows if t0 <= r[0] <= t1]
    ax_angle.plot([r[0] for r in part], [r[1] for r in part], color="#1f6fb2",
                  lw=1.0, label=TEXT["shoulder"])
    ax_angle.plot([r[0] for r in part], [r[2] for r in part], color="#d9822b",
                  lw=1.0, label=TEXT["elbow"])
    ax_angle.set_xlabel(TEXT["time"])
    ax_angle.set_ylabel(TEXT["angle"])
    ax_angle.grid(True, color="#dddddd", lw=0.5)
    ax_angle.legend(loc="upper right", fontsize=7, frameon=False, ncol=1)
    ax_angle.spines["top"].set_visible(False)
    ax_angle.spines["right"].set_visible(False)

    fs = [(t, v) for t, v in force_series(samples, force) if t0 <= t <= t1]
    if fs:
        ax_force.plot([t for t, _ in fs], [v for _, v in fs], color="#b7791f", lw=1.0,
                      label="HX711")
    ax_force.set_xlabel(TEXT["time"])
    ax_force.set_ylabel(TEXT["force"])
    ax_force.grid(True, color="#dddddd", lw=0.5)
    ax_force.legend(loc="upper right", fontsize=7, frameon=False)
    ax_force.spines["top"].set_visible(False)
    ax_force.spines["right"].set_visible(False)

    fig.text(0.085, 0.925, "(a) Gravity compensation law", fontsize=9)
    fig.text(0.545, 0.925, "(b) Live excerpt", fontsize=9)

    args.out.mkdir(parents=True, exist_ok=True)
    outputs = []
    for suffix in ("pdf", "svg", "png"):
        path = args.out / f"figure_ab.{suffix}"
        fig.savefig(path, dpi=args.dpi if suffix == "png" else None)
        if suffix == "svg":
            single_curve_layer(path)
        outputs.append(path)
    plt.close(fig)
    meta = {"session": str(args.directory), "excerpt_s": [t0, t1],
            "amplitude_nm": args.amplitude, "elbow_amplitude_nm": args.elbow_amplitude,
            "outputs": [str(p) for p in outputs]}
    (args.out / "figure_ab.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n",
                                            encoding="utf-8")
    print(json.dumps(meta["excerpt_s"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
