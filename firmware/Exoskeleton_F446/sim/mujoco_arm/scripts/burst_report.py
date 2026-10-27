#!/usr/bin/env python3
"""Stage G: burst movements (mallet strike, rapid lift) in three conditions.

Runs ``sim/burst.py`` for C0/C1/C2, prints the explosive-strength metrics
(peak velocity, peak torque, RTD 0-50/100 ms, time to peak torque, movement
time, overshoot) and writes a figure with one burst cycle and a bar summary.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\burst_report.py
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT))
sys.path.insert(0, str(SIM_ROOT / "model"))

import build_model as bm  # noqa: E402
from sim import burst as burst_mod  # noqa: E402
from sim.controller import Controller  # noqa: E402

COLOURS = {"C0": "tab:grey", "C1": "tab:orange", "C2": "tab:blue"}
LABELS = {"C0": "C0  bare arm", "C1": "C1  worn, unpowered", "C2": "C2  worn + firmware assist"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--burst", default="G1,G2")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    names = [b.strip() for b in args.burst.split(",") if b.strip()]
    out_dir = Path(args.out) if args.out else FW_ROOT / "build" / "sim-runs" / \
        datetime.now().strftime("burst-%Y%m%d-%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    model, _ = bm.build(write=False)
    controller = Controller()
    results, runs = {}, {}
    for name in names:
        task = burst_mod.BURSTS[name]
        print(f"\n{name}: {task.description} ({task.duration_s:.1f} s, {task.repeats} repeats)")
        per_condition = {}
        for condition in ("C0", "C1", "C2"):
            run = burst_mod.run_burst(model, task, condition, controller=controller)
            metrics = burst_mod.burst_metrics(run, task)
            per_condition[condition] = metrics
            runs[(name, condition)] = run
            print(f"  {condition}: peak vel {metrics['peak_velocity']:5.2f} rad/s, "
                  f"peak torque {metrics['peak_torque']:5.2f} N*m, "
                  f"RTD50 {metrics['rtd50']:5.2f}, RTD100 {metrics['rtd100']:5.2f} N*m, "
                  f"t_peak {metrics['time_to_peak_torque_ms']:5.0f} ms, "
                  f"move {metrics['movement_time_ms']:5.0f} ms, "
                  f"overshoot {metrics['overshoot_pct']:4.1f}%, "
                  f"settling {metrics['settling_s']:.2f} s, "
                  f"steady err {metrics['steady_error_deg']:.2f} deg")
        results[name] = per_condition

    (out_dir / "burst.json").write_text(json.dumps(
        {"note": "model estimates; 1 ms sampling", "bursts": results}, indent=2), encoding="utf-8")

    fig, axes = plt.subplots(2, len(names), figsize=(5.2 * len(names), 7), constrained_layout=True,
                             squeeze=False)
    for column, name in enumerate(names):
        task = burst_mod.BURSTS[name]
        for condition in ("C0", "C1", "C2"):
            a = runs[(name, condition)].arrays()
            mask = a["time"] <= task.move_s + 0.5
            axes[0][column].plot(a["time"][mask], a["human_nm"][mask], color=COLOURS[condition],
                                 lw=1.2, label=LABELS[condition])
            axes[1][column].plot(a["time"][mask], a["q_dot"][mask], color=COLOURS[condition], lw=1.2)
        axes[0][column].set(title=f"{name}: human joint torque in the first burst",
                            xlabel="time  s", ylabel="N·m")
        axes[1][column].set(xlabel="time  s", ylabel="rad/s")
        axes[0][column].grid(alpha=0.3)
        axes[1][column].grid(alpha=0.3)
        axes[0][column].legend(fontsize=7)
    fig.savefig(out_dir / "burst_curves.png", dpi=150)
    plt.close(fig)

    fig, axes = plt.subplots(1, 3, figsize=(13, 3.6), constrained_layout=True)
    for ax, key, title in ((axes[0], "peak_torque", "peak human torque  N·m"),
                           (axes[1], "rtd50", "RTD 0-50 ms  N·m"),
                           (axes[2], "movement_time_ms", "movement time  ms")):
        x = np.arange(len(names))
        for k, condition in enumerate(("C0", "C1", "C2")):
            values = [results[n][condition][key] for n in names]
            ax.bar(x + (k - 1) * 0.26, values, 0.26, color=COLOURS[condition],
                   label=LABELS[condition])
        ax.set_xticks(x, names)
        ax.set_title(title)
        ax.grid(alpha=0.3, axis="y")
    axes[0].legend(fontsize=7)
    fig.savefig(out_dir / "burst_bars.png", dpi=150)
    plt.close(fig)
    print(f"\nwrote {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
