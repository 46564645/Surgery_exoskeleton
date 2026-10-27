#!/usr/bin/env python3
"""Muscle-driven burst runs (G1 mallet, G2 rapid lift) in three conditions.

The human arm is driven by MyoArm's 63 muscles: a PD plus gravity feedforward
asks for a torque, the static optimisation turns it into activations, and
MuJoCo's activation dynamics produce the force. The exoskeleton is still
driven by the firmware control library at its own send interval.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\muscle_burst.py
    ... --viewer 0        # open the MuJoCo window while the last run executes
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
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

from sim import burst as burst_mod  # noqa: E402
from sim.controller import Controller  # noqa: E402
from sim.muscle_drive import run_muscle_burst  # noqa: E402

COLOURS = {"C0": "tab:grey", "C1": "tab:orange", "C2": "tab:blue"}
LABELS = {"C0": "C0  bare arm", "C1": "C1  worn, unpowered", "C2": "C2  worn + firmware assist"}


def metrics(run, burst) -> dict:
    a = run.arrays()
    start = math.radians(burst.start_deg)
    end = math.radians(burst.end_deg)
    travel = abs(end - start)
    q, t = a["q"], a["time"]
    ten = np.where(np.abs(q - start) > 0.10 * travel)[0]
    ninety = np.where(np.abs(q - start) > 0.90 * travel)[0]
    move_ms = float((t[ninety[0]] - t[ten[0]]) * 1000.0) if ten.size and ninety.size else float("nan")
    # Steady error is measured while the reference holds the target pose (the
    # end of the run is the return stroke, so the last samples are at the start).
    hold = (t >= burst.move_s + 0.6) & (t <= burst.move_s + 1.9)
    steady = float(np.degrees(np.abs(q[hold] - end).mean())) if hold.any() else float("nan")
    # Work is the right metric for a burst: the peak torque is dominated by
    # inertia (gravity is only ~9 % of it at 250 ms), so peak torque cannot
    # show a gravity-compensation benefit. Positive power only.
    dt = float(np.median(np.diff(t))) if t.size > 1 else 0.0
    human_work = float(np.sum(np.clip(a["human_nm"] * a["q_dot"], 0, None)) * dt)
    exo_work = float(np.sum(np.clip(a["exo_nm"] * a["q_dot"], 0, None)) * dt)
    return {
        "condition": run.condition,
        "peak_torque_nm": float(np.abs(a["human_nm"]).max()),
        "peak_velocity_rad_s": float(np.abs(a["q_dot"]).max()),
        "activation_sum_mean": float(a["activation_sum"].mean()),
        "activation_sum_peak": float(a["activation_sum"].max()),
        "movement_time_ms": move_ms,
        "steady_error_deg": steady,
        "human_positive_work_j": human_work,
        "exo_positive_work_j": exo_work,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--burst", default="G1,G2")
    parser.add_argument("--out", default=None)
    parser.add_argument("--viewer", type=float, default=-1.0,
                        help="if >= 0, open the MuJoCo window during the C2 run")
    args = parser.parse_args()
    names = [b.strip() for b in args.burst.split(",") if b.strip()]
    out_dir = Path(args.out) if args.out else FW_ROOT / "build" / "sim-runs" / \
        datetime.now().strftime("muscle-burst-%Y%m%d-%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    controller = Controller()
    print(f"firmware send interval {controller.site['send_interval_ms']} ms, "
          f"elbow amplitude {controller.site['elbow_gravity_amplitude_nm']:.3f} N*m")
    results, runs = {}, {}
    for name in names:
        task = burst_mod.BURSTS[name]
        print(f"\n{name}: {task.description} (muscle driven, {task.duration_s:.1f} s)")
        per_condition = {}
        for condition in ("C0", "C1", "C2"):
            viewer = None
            opened = None
            if args.viewer >= 0 and condition == "C2":
                import mujoco.viewer
                import build_model as bm
                model, _ = bm.build(write=False, keep_muscles=True)
                opened = mujoco.viewer.launch_passive(model, __import__("mujoco").MjData(model))
                viewer = opened
                print("  viewer window opened for the C2 run")
            start = time.perf_counter()
            run = run_muscle_burst(task, condition, controller=controller, viewer=viewer)
            elapsed = time.perf_counter() - start
            if opened is not None:
                opened.close()
            per_condition[condition] = metrics(run, task)
            per_condition[condition]["wall_s"] = elapsed
            runs[(name, condition)] = run
            m = per_condition[condition]
            print(f"  {condition}: peak torque {m['peak_torque_nm']:5.2f} N*m, "
                  f"peak vel {m['peak_velocity_rad_s']:5.2f} rad/s, "
                  f"act sum {m['activation_sum_mean']:5.2f} (peak {m['activation_sum_peak']:5.2f}), "
                  f"move {m['movement_time_ms']:5.0f} ms, "
                  f"steady err {m['steady_error_deg']:4.1f} deg, "
                  f"work {m['human_positive_work_j']:6.2f} J (exo {m['exo_positive_work_j']:5.2f} J), "
                  f"{elapsed:4.1f} s wall")
        results[name] = per_condition

    (out_dir / "muscle_burst.json").write_text(json.dumps(
        {"note": "muscle-driven human, model estimates, 1 ms physics",
         "send_interval_ms": controller.site["send_interval_ms"],
         "bursts": results}, indent=2), encoding="utf-8")

    fig, axes = plt.subplots(2, len(names), figsize=(5.4 * len(names), 7),
                             constrained_layout=True, squeeze=False)
    for column, name in enumerate(names):
        task = burst_mod.BURSTS[name]
        for condition in ("C0", "C1", "C2"):
            a = runs[(name, condition)].arrays()
            mask = a["time"] <= task.move_s + 0.6
            axes[0][column].plot(a["time"][mask], a["activation_sum"][mask],
                                 color=COLOURS[condition], lw=1.2, label=LABELS[condition])
            axes[1][column].plot(a["time"][mask], a["q_dot"][mask], color=COLOURS[condition], lw=1.2)
        axes[0][column].set(title=f"{name}: sum of muscle activation, first burst",
                            xlabel="time  s", ylabel="sum activation")
        axes[1][column].set(xlabel="time  s", ylabel="rad/s")
        for ax in (axes[0][column], axes[1][column]):
            ax.grid(alpha=0.3)
        axes[0][column].legend(fontsize=7)
    fig.savefig(out_dir / "muscle_burst.png", dpi=150)
    plt.close(fig)
    print(f"\nwrote {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
