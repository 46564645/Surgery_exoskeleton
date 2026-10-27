#!/usr/bin/env python3
"""Muscle-driven static holds at the three surgical postures.

Same muscle pipeline as ``muscle_burst.py`` (computed-torque demand, static
optimisation, MuJoCo activation dynamics) but the reference is a fixed pose,
so the numbers can be compared with the ideal-torque stage B runs.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\muscle_static.py
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
from sim.burst import Burst  # noqa: E402
from sim.controller import Controller  # noqa: E402
from sim.muscle_drive import run_muscle_burst  # noqa: E402

COLOURS = {"C0": "tab:grey", "C1": "tab:orange", "C2": "tab:blue"}
LABELS = {"C0": "C0  bare arm", "C1": "C1  worn, unpowered", "C2": "C2  worn + firmware assist"}
POSTURES = {
    "P1": (20.0, 100.0),
    "P4": (30.0, 110.0),
    "P5": (60.0, 70.0),
}
HOLD_REPEATS = 20      # 20 x (0.18*2 + 2) s ~ 47 s of hold


def hold_task(name: str, shoulder: float, elbow: float) -> Burst:
    """A fixed-pose reference (both joints held, no movement)."""
    return Burst(f"HOLD_{name}", bm.HUMAN_ELBOW, elbow, elbow, 0.18, HOLD_REPEATS,
                 f"{name} hold shoulder {shoulder} / elbow {elbow}",
                 fixed_shoulder_deg=shoulder, fixed_elbow_deg=elbow)


def metrics(run) -> dict:
    a = run.arrays()
    dt = float(np.median(np.diff(a["time"])))
    shoulder = np.abs(a["human_sh_nm"])
    elbow = np.abs(a["human_el_nm"])
    return {
        "condition": run.condition,
        "activation_sum_mean": float(a["activation_sum"].mean()),
        "activation_sum_peak": float(a["activation_sum"].max()),
        "shoulder_torque_mean_nm": float(shoulder.mean()),
        "elbow_torque_mean_nm": float(elbow.mean()),
        "human_torque_mean_nm": float(shoulder.mean() + elbow.mean()),
        "exo_torque_mean_nm": float(np.abs(a["exo_nm"]).mean()),
        "exo_work_j": float(np.sum(np.clip(a["exo_nm"] * a["q_dot"], 0, None)) * dt),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    out_dir = Path(args.out) if args.out else FW_ROOT / "build" / "sim-runs" / \
        datetime.now().strftime("muscle-static-%Y%m%d-%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    controller = Controller()
    print(f"firmware: send {controller.site['send_interval_ms']} ms, "
          f"shoulder cap {controller.site['shoulder_cap_rotor_nm'] * 1000:.0f} mNm, "
          f"gain {controller.site['shoulder_gain']:.1f}, "
          f"elbow amplitude {controller.site['elbow_gravity_amplitude_nm']:.3f} N*m")

    results = {}
    for name, (shoulder, elbow) in POSTURES.items():
        task = hold_task(name, shoulder, elbow)
        print(f"\n{name}: shoulder {shoulder} / elbow {elbow} deg, muscle driven")
        per_condition = {}
        for condition in ("C0", "C1", "C2"):
            run = run_muscle_burst(task, condition, controller=controller)
            m = metrics(run)
            per_condition[condition] = m
            print(f"  {condition}: activation sum {m['activation_sum_mean']:5.2f}, "
                  f"human shoulder {m['shoulder_torque_mean_nm']:5.2f} / "
                  f"elbow {m['elbow_torque_mean_nm']:5.2f} N*m, "
                  f"exo torque {m['exo_torque_mean_nm']:5.2f} N*m")
        base = per_condition["C0"]["activation_sum_mean"]
        for cond in per_condition:
            per_condition[cond]["activation_vs_bare_pct"] = 100.0 * (
                per_condition[cond]["activation_sum_mean"] / base - 1.0)
        results[name] = per_condition

    (out_dir / "muscle_static.json").write_text(json.dumps(
        {"note": "muscle driven static holds, model estimates", "postures": results},
        indent=2), encoding="utf-8")

    fig, axes = plt.subplots(1, 2, figsize=(11, 4), constrained_layout=True)
    names = list(results)
    x = np.arange(len(names))
    for k, cond in enumerate(("C0", "C1", "C2")):
        axes[0].bar(x + (k - 1) * 0.26,
                    [results[n][cond]["activation_sum_mean"] for n in names], 0.26,
                    color=COLOURS[cond], label=LABELS[cond])
        axes[1].bar(x + (k - 1) * 0.26,
                    [results[n][cond]["human_torque_mean_nm"] for n in names], 0.26,
                    color=COLOURS[cond], label=LABELS[cond])
    axes[0].set_xticks(x, names)
    axes[0].set_ylabel("sum of muscle activation")
    axes[0].set_title("static holds: muscle activation (lower is better)")
    axes[1].set_xticks(x, names)
    axes[1].set_ylabel("mean |human torque|  N·m")
    axes[1].set_title("static holds: human joint torque")
    for ax in axes:
        ax.grid(alpha=0.3, axis="y")
        ax.legend(fontsize=8)
    fig.savefig(out_dir / "muscle_static.png", dpi=150)
    plt.close(fig)
    print(f"\nwrote {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
