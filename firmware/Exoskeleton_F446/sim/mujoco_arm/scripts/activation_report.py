#!/usr/bin/env python3
"""M6: muscle activation as a second effort metric, from a finished run.

Reads ``samples_*.csv`` written by ``run_experiment.py`` and, for every
recorded sample, solves MyoArm's static muscle optimisation for the joint
torque the human applied there. Writes ``activation_<task>.json`` and one
figure comparing the conditions.

The activations are an estimate from the ideal joint torques, not a muscle
simulation: the dynamic runs still use joint torque actuators.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\activation_report.py \\
        --run build\\sim-runs\\final2-20261027
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT))
sys.path.insert(0, str(SIM_ROOT / "model"))

from sim.muscles import MuscleEffort  # noqa: E402

COLOURS = {"C0": "tab:grey", "C1": "tab:orange", "C2": "tab:blue"}
LABELS = {"C0": "C0  bare arm", "C1": "C1  worn, unpowered", "C2": "C2  worn + firmware assist"}


def load_samples(path: Path) -> dict[str, dict[str, np.ndarray]]:
    rows: dict[str, list[list[float]]] = {}
    with path.open(encoding="utf-8") as handle:
        reader = csv.reader(handle)
        header = next(reader)
        for row in reader:
            rows.setdefault(row[0], []).append([float(v) for v in row[1:]])
    names = header[1:]
    return {cond: {n: np.array([r[i] for r in data]) for i, n in enumerate(names)}
            for cond, data in rows.items()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", required=True)
    parser.add_argument("--stride", type=int, default=10,
                        help="evaluate every Nth sample (default 10 = every 50 ms)")
    args = parser.parse_args()
    run_dir = Path(args.run)
    if not run_dir.is_absolute():
        run_dir = FW_ROOT / run_dir
    out_dir = run_dir / "report"
    out_dir.mkdir(parents=True, exist_ok=True)

    effort = MuscleEffort()
    print(f"muscle model: {len(effort.muscles)} muscles")
    tasks = sorted(p.stem.replace("samples_", "") for p in run_dir.glob("samples_*.csv"))
    results: dict[str, dict] = {}
    series: dict[str, dict[str, tuple[np.ndarray, np.ndarray]]] = {}

    for task in tasks:
        samples = load_samples(run_dir / f"samples_{task}.csv")
        per_condition = {}
        for cond, data in samples.items():
            idx = range(0, len(data["t_s"]), args.stride)
            sums, peaks, residual = [], [], 0.0
            for k in idx:
                a, res = effort.activations(float(data["q_sh_rad"][k]), float(data["q_el_rad"][k]),
                                            float(data["human_sh_nm"][k]),
                                            float(data["human_el_nm"][k]))
                sums.append(float(a.sum()))
                peaks.append(float(a.max()))
                residual = max(residual, res)
            per_condition[cond] = {
                "sum_activation_mean": float(np.mean(sums)),
                "peak_activation_mean": float(np.mean(peaks)),
                "peak_activation_max": float(np.max(peaks)),
                "max_constraint_residual_nm": residual,
                "samples": len(sums),
            }
            series.setdefault(task, {})[cond] = (np.array(data["t_s"])[list(idx)],
                                                 np.array(sums))
        base = per_condition["C0"]["sum_activation_mean"]
        for cond in per_condition:
            per_condition[cond]["vs_bare_pct"] = 100.0 * (
                per_condition[cond]["sum_activation_mean"] / base - 1.0)
        results[task] = per_condition
        print(f"\n{task}: sum of activation (all muscles, lower is better)")
        for cond, values in per_condition.items():
            print(f"  {cond}: {values['sum_activation_mean']:5.3f} "
                  f"({values['vs_bare_pct']:+5.1f}% vs bare), "
                  f"peak {values['peak_activation_mean']:.3f}, "
                  f"residual {values['max_constraint_residual_nm']:.3f} N·m")

    (out_dir / "activation.json").write_text(json.dumps(
        {"run": str(run_dir), "stride": args.stride,
         "note": "static optimisation on the recorded joint torques; model estimate",
         "tasks": results}, indent=2), encoding="utf-8")

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), constrained_layout=True)
    x = np.arange(len(tasks))
    width = 0.26
    for k, cond in enumerate(("C0", "C1", "C2")):
        values = [results[t][cond]["sum_activation_mean"] for t in tasks]
        axes[0].bar(x + (k - 1) * width, values, width, color=COLOURS[cond], label=LABELS[cond])
    axes[0].set_xticks(x, tasks)
    axes[0].set_ylabel("sum of muscle activation")
    axes[0].set_title("total muscle activation per task (lower = less effort)")
    axes[0].legend(fontsize=8)
    axes[0].grid(alpha=0.3, axis="y")

    task = tasks[0] if tasks else None
    if task:
        for cond, (t, sums) in series[task].items():
            axes[1].plot(t, sums, color=COLOURS[cond], lw=1.3, label=LABELS[cond])
        axes[1].set_xlabel("time  s")
        axes[1].set_ylabel("sum of muscle activation")
        axes[1].set_title(f"{task}: activation over time")
        axes[1].legend(fontsize=8)
        axes[1].grid(alpha=0.3)
    fig.savefig(out_dir / "report_activation.png", dpi=150)
    plt.close(fig)
    print(f"\nwrote {out_dir / 'activation.json'} and report_activation.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
