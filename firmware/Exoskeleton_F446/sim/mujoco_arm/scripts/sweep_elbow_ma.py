#!/usr/bin/env python3
"""Sweep the firmware elbow amplitude (S5_R5_ELBOW_SUPPORT_MA) in simulation.

The firmware's elbow channel is ``MA x 0.02 N*m x max(0, sin(q_sh + q_el))``.
The M4 runs showed it recovers almost nothing because the forearm link of the
exoskeleton (0.177 kg) needs about 0.15-0.2 N*m at the elbow. This script
rebuilds the control library for each MA, runs the same reference motion with
the exoskeleton worn and driven (C2), and reports the effort against the worn
but unpowered case (C1) computed once.

Nothing here changes the firmware sources: the amplitude is a build option.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\sweep_elbow_ma.py
    ... --task T1 --ma 2000,6000,10000,14000,20000
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_ROOT))
sys.path.insert(0, str(SIM_ROOT / "model"))

import build_model as bm  # noqa: E402
from sim import metrics, tasks  # noqa: E402
from sim.controller import Controller  # noqa: E402
from sim.runner import run  # noqa: E402

FW_ROOT = SIM_ROOT.parents[1]
BUILD = SIM_ROOT / "scripts" / "build_bridge.py"
OUT_ROOT = FW_ROOT / "build" / "sim-runs"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--task", default="T1")
    parser.add_argument("--ma", default="2000,6000,10000,14000,20000",
                        help="elbow amplitude in mA, 1..20000 (20 A = 0.4 N*m)")
    parser.add_argument("--gain", default="1.0")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    task = tasks.get(args.task)
    values = [int(v) for v in args.ma.split(",") if v.strip()]
    out_dir = Path(args.out) if args.out else OUT_ROOT / datetime.now().strftime("sweep-%Y%m%d-%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    model, _ = bm.build(write=False)
    print(f"task {task.name}: {task.description} ({task.duration_s:.0f} s)")

    baseline = metrics.summarize(run(model, task, "C1"))
    bare = metrics.summarize(run(model, task, "C0"))
    print(f"  C0 bare arm        effort {bare['effort_nm_mean']:5.2f} N*m "
          f"(shoulder {bare['shoulder']['rms']:.2f}, elbow {bare['elbow']['rms']:.2f} RMS)")
    print(f"  C1 worn, unpowered effort {baseline['effort_nm_mean']:5.2f} N*m "
          f"(shoulder {baseline['shoulder']['rms']:.2f}, elbow {baseline['elbow']['rms']:.2f} RMS)")

    rows = []
    for ma in values:
        lib = out_dir / f"sim_bridge_ma{ma}.dll"
        subprocess.run([sys.executable, str(BUILD), "--ma", str(ma), "--gain", args.gain,
                        "--sign", "1", "--out", str(lib)], check=True,
                       stdout=subprocess.DEVNULL)
        summary = metrics.summarize(run(model, task, "C2", controller=Controller(lib)))
        reduction = metrics.compare([baseline, summary], baseline="C1")["reduction"]["C2"]
        rows.append({"ma": ma, "elbow_amplitude_nm": 0.02 * ma / 1000.0,
                     "effort_nm_mean": summary["effort_nm_mean"],
                     "shoulder_rms": summary["shoulder"]["rms"],
                     "elbow_rms": summary["elbow"]["rms"],
                     "exo_elbow_rms": summary["exo"]["elbow"]["rms"],
                     **reduction})
        print(f"  MA {ma:5d} mA ({0.02 * ma / 1000.0:.3f} N*m): effort {summary['effort_nm_mean']:5.2f} N*m, "
              f"reduction {reduction['effort_nm_mean_pct']:+5.1f}% "
              f"(shoulder {reduction['shoulder_rms_pct']:+5.1f}%, elbow {reduction['elbow_rms_pct']:+5.1f}%)")

    # Plain text chart: reduction vs MA, so the result is readable without a plot.
    print("\nreduction vs MA (total effort, shoulder, elbow):")
    for row in rows:
        bar = "#" * max(0, int(round(row["effort_nm_mean_pct"])))
        print(f"  {row['ma']:5d} mA {row['effort_nm_mean_pct']:+6.1f}% |{bar}")

    payload = {"task": task.name, "gain": float(args.gain), "bare": bare,
               "unpowered": baseline, "sweep": rows,
               "note": "model estimates; the firmware amplitude is a build option, nothing was flashed"}
    (out_dir / f"sweep_{task.name}.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"\nwrote {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
