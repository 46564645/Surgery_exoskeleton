#!/usr/bin/env python3
"""M4 experiment runner: one command for a task x condition comparison.

Example (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\run_experiment.py --task T2slow

Runs the bare arm (C0), the worn but unpowered exoskeleton (C1) and the worn
exoskeleton driven by the firmware control library (C2) on the same reference
motion, then writes samples.csv, summary.json and a figure to
build/sim-runs/<timestamp>/. The numbers are model estimates: the physical
parameters and the PD gains are assumptions (see model/params.json).
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from datetime import datetime
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(SIM_ROOT))
sys.path.insert(0, str(SIM_ROOT / "model"))

import build_model as bm  # noqa: E402
from sim import metrics, plots, tasks  # noqa: E402
from sim.runner import run  # noqa: E402

FW_ROOT = SIM_ROOT.parents[1]
OUT_ROOT = FW_ROOT / "build" / "sim-runs"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--task", default="T2slow", help=", ".join(tasks.TASKS) + ", or a comma list")
    parser.add_argument("--cond", default="C0,C1,C2", help="conditions, e.g. C0,C1,C2")
    parser.add_argument("--duration", type=float, default=None, help="override task duration, s")
    parser.add_argument("--out", default=None, help="output directory")
    args = parser.parse_args()

    conditions = [c.strip().upper() for c in args.cond.split(",") if c.strip()]
    unknown = [c for c in conditions if c not in ("C0", "C1", "C2")]
    if unknown:
        raise SystemExit(f"unknown condition(s) {unknown}; use C0, C1, C2")
    task_names = [t.strip() for t in args.task.split(",") if t.strip()]

    out_dir = Path(args.out) if args.out else OUT_ROOT / datetime.now().strftime("run-%Y%m%d-%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    all_summaries, all_runs = [], []
    for name in task_names:
        task = tasks.get(name)
        model, info = bm.build(write=False)
        runs = [run(model, task, condition, duration_s=args.duration) for condition in conditions]
        summaries = [metrics.summarize(r) for r in runs]
        comparison = metrics.compare(summaries, baseline="C1")
        figure_path = plots.figure(runs, summaries, out_dir / f"figure_{name}.png",
                                  f"{name}: {task.description}")
        with (out_dir / f"samples_{name}.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["condition", "t_s", "q_sh_rad", "q_el_rad", "ref_sh_rad", "ref_el_rad",
                             "human_sh_nm", "human_el_nm", "exo_sh_nm", "exo_el_nm",
                             "strap_upper_n", "strap_forearm_n"])
            for r in runs:
                a = r.arrays()
                for k in range(len(a["time"])):
                    writer.writerow([r.condition] + [f"{a[c][k]:.6f}" for c in
                                                     ("time", "q_sh", "q_el", "ref_sh", "ref_el",
                                                      "human_sh_nm", "human_el_nm", "exo_sh_nm",
                                                      "exo_el_nm", "strap_upper_n", "strap_forearm_n")])
        payload = {"task": name, "description": task.description,
                   "duration_s": args.duration or task.duration_s,
                   "model": "built in memory (model/build_model.py)", "figure": str(figure_path),
                   "summaries": summaries, "comparison": comparison,
                   "note": "model estimates: physical parameters and PD gains are assumptions"}
        (out_dir / f"summary_{name}.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")
        all_summaries.extend(summaries)
        all_runs.extend(runs)

        print(f"\n=== {name}: {task.description} ({payload['duration_s']:.0f} s) ===")
        for s in summaries:
            print(f"  {s['condition']}: shoulder RMS {s['shoulder']['rms']:5.2f} N·m "
                  f"peak {s['shoulder']['peak']:5.2f} | elbow RMS {s['elbow']['rms']:5.2f} "
                  f"peak {s['elbow']['peak']:5.2f} | mean effort {s['effort_nm_mean']:5.2f} N·m | "
                  f"tracking err {s['tracking_rms_deg']['shoulder']:.2f}/"
                  f"{s['tracking_rms_deg']['elbow']:.2f} deg | table steps {s['table_contact_steps']}")
        if comparison:
            for cond, red in comparison["reduction"].items():
                print(f"  reduction vs {comparison['baseline']}: {cond} total {red['effort_nm_mean_pct']:+.1f}% "
                      f"(shoulder RMS {red['shoulder_rms_pct']:+.1f}%, elbow RMS {red['elbow_rms_pct']:+.1f}%)")

    (out_dir / "summary.json").write_text(json.dumps(
        {"runs": all_summaries, "note": "model estimates, see per-task summary_*.json"},
        indent=2), encoding="utf-8")
    print(f"\nwrote {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
