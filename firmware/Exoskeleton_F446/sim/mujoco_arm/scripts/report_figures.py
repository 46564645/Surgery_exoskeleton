#!/usr/bin/env python3
"""Collect the simulation result figures from one run directory.

Reads ``summary_*.json`` and ``samples_*.csv`` written by ``run_experiment.py``
and produces three presentation figures plus scene stills:

  report_timeseries.png  human effort over time for the dynamic lift (T2slow)
  report_bars.png        total effort per task and condition, with the
                         reduction against the worn-but-unpowered case
  report_scenes.png      the model at four representative poses
  lift_anim.gif          optional animation of the lift cycle (--anim)

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\report_figures.py \\
        --run build\\sim-runs\\final-20261027
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
import mujoco  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT / "model"))
import build_model as bm  # noqa: E402

COLOURS = {"C0": "tab:grey", "C1": "tab:orange", "C2": "tab:blue"}
LABELS = {"C0": "C0  bare arm", "C1": "C1  worn, unpowered", "C2": "C2  worn + firmware assist"}
POSES = ((5, 90), (25, 90), (45, 90), (45, 60))


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


def timeseries(run_dir: Path, out: Path, task: str = "T2slow") -> Path:
    samples = load_samples(run_dir / f"samples_{task}.csv")
    fig, axes = plt.subplots(3, 1, figsize=(9, 8), sharex=True, constrained_layout=True)
    for cond in ("C0", "C1", "C2"):
        if cond not in samples:
            continue
        a = samples[cond]
        axes[0].plot(a["t_s"], a["human_sh_nm"], color=COLOURS[cond], lw=1.4, label=LABELS[cond])
        axes[1].plot(a["t_s"], a["human_el_nm"], color=COLOURS[cond], lw=1.4, label=LABELS[cond])
        if cond != "C0":
            axes[2].plot(a["t_s"], a["strap_upper_n"], color=COLOURS[cond], lw=1.1,
                         label=f"{LABELS[cond]} - upper band")
    ref = samples["C0"]
    axes[0].plot(ref["t_s"], np.degrees(ref["ref_sh_rad"]) / 10.0, "k--", lw=0.8,
                 label="reference shoulder / 10 (deg)")
    axes[0].set_ylabel("human shoulder torque  N·m")
    axes[1].set_ylabel("human elbow torque  N·m")
    axes[2].set_ylabel("upper band force  N")
    axes[2].set_xlabel("time  s")
    for ax in axes:
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8, loc="upper right")
    axes[0].set_title(f"{task}: lift/lower 5-45 deg with the elbow held at 90 deg "
                      "(model estimates, MA = 10 A)")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


def bars(run_dir: Path, out: Path) -> Path:
    tasks = sorted(p.stem.replace("summary_", "") for p in run_dir.glob("summary_*.json")
                   if p.stem != "summary")
    data = {t: json.loads((run_dir / f"summary_{t}.json").read_text(encoding="utf-8"))
            for t in tasks}
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), constrained_layout=True,
                             gridspec_kw={"width_ratios": [1.6, 1]})
    x = np.arange(len(tasks))
    width = 0.26
    for k, cond in enumerate(("C0", "C1", "C2")):
        values = []
        for t in tasks:
            summary = next((s for s in data[t]["summaries"] if s["condition"] == cond), None)
            values.append(summary["effort_nm_mean"] if summary else np.nan)
        axes[0].bar(x + (k - 1) * width, values, width, color=COLOURS[cond], label=LABELS[cond])
    axes[0].set_xticks(x, [f"{t}\n{data[t]['duration_s']:.0f} s" for t in tasks])
    axes[0].set_ylabel("mean human effort  N·m  (|torque| integral / time)")
    axes[0].set_title("total human effort per task and condition")
    axes[0].legend(fontsize=8)
    axes[0].grid(alpha=0.3, axis="y")

    labels, worn, assisted = [], [], []
    for t in tasks:
        summaries = {s["condition"]: s for s in data[t]["summaries"]}
        base = summaries["C0"]["effort_nm_mean"]
        labels.append(t)
        # Cost of wearing the unpowered device (positive = heavier than bare)
        worn.append(100.0 * (summaries["C1"]["effort_nm_mean"] / base - 1.0))
        # Gain from the firmware assist (positive = lighter than C1)
        assisted.append(100.0 * (1.0 - summaries["C2"]["effort_nm_mean"]
                                 / summaries["C1"]["effort_nm_mean"]))
    axes[1].bar(np.arange(len(tasks)) - 0.18, worn, 0.36, color=COLOURS["C1"],
                label="wearing it, unpowered (cost)")
    axes[1].bar(np.arange(len(tasks)) + 0.18, assisted, 0.36, color=COLOURS["C2"],
                label="with firmware assist (gain)")
    axes[1].axhline(0.0, color="k", lw=0.8)
    axes[1].set_xticks(np.arange(len(tasks)), labels)
    axes[1].set_ylabel("change vs C1  %")
    axes[1].set_title("what the exoskeleton costs and gives back")
    axes[1].legend(fontsize=8)
    axes[1].grid(alpha=0.3, axis="y")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


def scenes(out: Path, width: int = 620, height: int = 470) -> Path:
    """One row per view (right side, oblique), one column per pose."""
    model, _ = bm.build(write=False)
    data = mujoco.MjData(model)
    renderer = mujoco.Renderer(model, height, width)
    camera = mujoco.MjvCamera()
    views = ((0, -6, "right side"), (40, -14, "oblique"))
    sheet = Image.new("RGB", (width * len(POSES), (height + 24) * len(views)), "white")
    draw = ImageDraw.Draw(sheet)
    for k, (sh_deg, el_deg) in enumerate(POSES):
        data.qpos[:] = 0.0
        for joint, deg in ((bm.HUMAN_SHOULDER, sh_deg), (bm.HUMAN_ELBOW, el_deg),
                           (bm.EXO_SHOULDER, sh_deg), (bm.EXO_ELBOW, el_deg)):
            data.qpos[bm.joint_qpos(model, joint)] = np.radians(deg)
        mujoco.mj_forward(model, data)
        for j, (azimuth, elevation, view) in enumerate(views):
            camera.lookat[:] = [-0.24, 0.0, 1.15]
            camera.distance = 1.5
            camera.azimuth, camera.elevation = azimuth, elevation
            renderer.update_scene(data, camera)
            sheet.paste(Image.fromarray(renderer.render()),
                        (k * width, j * (height + 24) + 24))
            draw.text((k * width + 10, j * (height + 24) + 6),
                      f"shoulder {sh_deg} / elbow {el_deg} deg - {view}", fill="black")
    renderer.close()
    sheet.save(out)
    return out


def animation(run_dir: Path, out: Path, task: str = "T2slow", width: int = 400,
              height: int = 320, fps: int = 12) -> Path:
    """Replay the reference of one task and save an animated GIF."""
    samples = load_samples(run_dir / f"samples_{task}.csv")
    ref = samples["C2"]
    model, _ = bm.build(write=False)
    data = mujoco.MjData(model)
    renderer = mujoco.Renderer(model, height, width)
    camera = mujoco.MjvCamera()
    camera.lookat[:] = [-0.24, 0.0, 1.15]
    camera.distance = 1.5
    camera.azimuth, camera.elevation = 25, -10
    step = max(1, int(round(1.0 / fps / 0.005)))
    frames = []
    for k in range(0, len(ref["t_s"]), step):
        data.qpos[:] = 0.0
        for joint, rad in ((bm.HUMAN_SHOULDER, ref["ref_sh_rad"][k]),
                           (bm.HUMAN_ELBOW, ref["ref_el_rad"][k]),
                           (bm.EXO_SHOULDER, ref["ref_sh_rad"][k]),
                           (bm.EXO_ELBOW, ref["ref_el_rad"][k])):
            data.qpos[bm.joint_qpos(model, joint)] = rad
        mujoco.mj_forward(model, data)
        renderer.update_scene(data, camera)
        frames.append(Image.fromarray(renderer.render()))
    renderer.close()
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=int(1000 / fps),
                   loop=0)
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", required=True, help="run directory from run_experiment.py")
    parser.add_argument("--anim", action="store_true", help="also write the lift GIF")
    args = parser.parse_args()
    run_dir = Path(args.run)
    if not run_dir.is_absolute():
        run_dir = FW_ROOT / run_dir
    out_dir = run_dir / "report"
    out_dir.mkdir(parents=True, exist_ok=True)

    for path in (timeseries(run_dir, out_dir / "report_timeseries.png"),
                 bars(run_dir, out_dir / "report_bars.png"),
                 scenes(out_dir / "report_scenes.png")):
        print(f"wrote {path}")
    if args.anim:
        print(f"wrote {animation(run_dir, out_dir / 'lift_anim.gif')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
