#!/usr/bin/env python3
"""Stage A: assistance map over the sagittal workspace.

For every grid point (shoulder 5-60 deg x elbow 30-110 deg) the model is held
at that pose and the human joint torque is read from inverse dynamics in three
conditions:

  C0  bare arm            (straps off)
  C1  worn, unpowered     (straps on, exoskeleton torque 0)
  C2  worn + firmware     (straps on, exoskeleton torque = the DLL command)

The muscle activation for each condition comes from the same static
optimisation as ``activation_report.py``. Writes a CSV, a JSON summary and a
three-panel heat map to ``build/sim-runs/workspace-<timestamp>/``.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\workspace_scan.py
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from datetime import datetime
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import mujoco  # noqa: E402
import numpy as np  # noqa: E402

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT))
sys.path.insert(0, str(SIM_ROOT / "model"))

import build_model as bm  # noqa: E402
from sim.controller import Controller  # noqa: E402
from sim.muscles import MuscleEffort  # noqa: E402


SETTLE_STEPS = 1000
AVERAGE_STEPS = 200


def hold_torque(model, data, condition: str, controller, q_sh: float, q_el: float,
                rotor_per_joint: float, kt: float) -> tuple[float, float]:
    """Steady human torque while a PD holds the pose (same as the dynamic runs).

    Inverse dynamics at a prescribed pose is not usable here: with the straps
    on, the grounded exoskeleton forms a closed loop that can "carry" the arm
    in the inverse solution even though the forward dynamics does not do that.
    A PD hold with forward dynamics gives the torque a person would really
    apply, which is what the C0/C1/C2 comparison means.
    """
    data.qpos[:] = 0.0
    data.qvel[:] = 0.0
    for joint, q in ((bm.HUMAN_SHOULDER, q_sh), (bm.HUMAN_ELBOW, q_el),
                     (bm.EXO_SHOULDER, q_sh), (bm.EXO_ELBOW, q_el)):
        data.qpos[bm.joint_qpos(model, joint)] = q
    model.opt.disableflags = (int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
                              if condition == "C0" else 0)
    mujoco.mj_forward(model, data)

    act_h = (model.actuator(bm.ACT_HUMAN_SHOULDER).id, model.actuator(bm.ACT_HUMAN_ELBOW).id)
    act_e = (model.actuator(bm.ACT_EXO_SHOULDER).id, model.actuator(bm.ACT_EXO_ELBOW).id)
    gains = ((500.0, 30.0), (150.0, 10.0))
    exo = (0.0, 0.0)
    next_ms = 0.0
    samples = []
    for step in range(SETTLE_STEPS):
        data.ctrl[:] = 0.0
        for act, joint, target, (kp, kd) in zip(act_h, (bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW),
                                                (q_sh, q_el), gains):
            error = target - data.qpos[bm.joint_qpos(model, joint)]
            data.ctrl[act] = kp * error - kd * data.qvel[bm.joint_dof(model, joint)]
        if condition == "C2":
            now_ms = data.time * 1000.0
            if now_ms + 1e-9 >= next_ms:
                command = controller.step(
                    shoulder_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]),
                    shoulder_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_SHOULDER)]),
                    elbow_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)]),
                    elbow_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_ELBOW)]),
                    now_ms=int(now_ms))
                exo = ((command.shoulder_rotor_nm * rotor_per_joint,
                        command.elbow_current_a * kt) if command.ok else (0.0, 0.0))
                next_ms += 50.0
        data.ctrl[act_e[0]], data.ctrl[act_e[1]] = exo
        mujoco.mj_step(model, data)
        if step >= SETTLE_STEPS - AVERAGE_STEPS:
            samples.append((float(data.actuator_force[act_h[0]]),
                            float(data.actuator_force[act_h[1]])))
    model.opt.disableflags = 0
    mean = np.mean(np.array(samples), axis=0)
    return float(mean[0]), float(mean[1])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shoulder", default="5,60,12", help="start,end,count in degrees")
    parser.add_argument("--elbow", default="30,110,9", help="start,end,count in degrees")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    sh0, sh1, shn = (float(v) for v in args.shoulder.split(","))
    el0, el1, eln = (float(v) for v in args.elbow.split(","))
    shoulders = np.linspace(sh0, sh1, int(shn))
    elbows = np.linspace(el0, el1, int(eln))

    out_dir = Path(args.out) if args.out else FW_ROOT / "build" / "sim-runs" / \
        datetime.now().strftime("workspace-%Y%m%d-%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    model, info = bm.build(write=False)
    data = mujoco.MjData(model)
    controller = Controller()
    rotor_per_joint = controller.site["shoulder_rotor_per_joint"]
    kt = controller.site["elbow_motor_kt_nm_per_a"]
    effort = MuscleEffort()
    print(f"model built in memory, DLL elbow amplitude "
          f"{controller.site['elbow_gravity_amplitude_nm']:.3f} N*m "
          f"({controller.site['elbow_gravity_amplitude_nm'] / kt * 1000:.0f} mA), "
          f"grid {len(shoulders)}x{len(elbows)}")

    rows = []
    for sh_deg in shoulders:
        for el_deg in elbows:
            q_sh, q_el = math.radians(sh_deg), math.radians(el_deg)
            mujoco.mj_resetData(model, data)
            tau0 = hold_torque(model, data, "C0", controller, q_sh, q_el, rotor_per_joint, kt)
            mujoco.mj_resetData(model, data)
            tau1 = hold_torque(model, data, "C1", controller, q_sh, q_el, rotor_per_joint, kt)
            mujoco.mj_resetData(model, data)
            tau2 = hold_torque(model, data, "C2", controller, q_sh, q_el, rotor_per_joint, kt)
            command = controller.step(shoulder_rad=q_sh, shoulder_vel=0.0,
                                      elbow_rad=q_el, elbow_vel=0.0, now_ms=0)

            acts = {}
            for name, tau in (("C0", tau0), ("C1", tau1), ("C2", tau2)):
                a, _ = effort.activations(q_sh, q_el, tau[0], tau[1])
                acts[name] = float(a.sum())

            rows.append({
                "shoulder_deg": float(sh_deg), "elbow_deg": float(el_deg),
                "torque_C0_sh": tau0[0], "torque_C0_el": tau0[1],
                "torque_C1_sh": tau1[0], "torque_C1_el": tau1[1],
                "torque_C2_sh": tau2[0], "torque_C2_el": tau2[1],
                "exo_sh_nm": command.shoulder_rotor_nm * rotor_per_joint,
                "exo_el_nm": command.elbow_current_a * kt,
                "act_C0": acts["C0"], "act_C1": acts["C1"], "act_C2": acts["C2"],
            })

    def grid(key: str) -> np.ndarray:
        values = np.array([r[key] for r in rows])
        return values.reshape(len(shoulders), len(elbows))

    t0 = np.hypot(grid("torque_C0_sh"), grid("torque_C0_el"))
    t1 = np.hypot(grid("torque_C1_sh"), grid("torque_C1_el"))
    t2 = np.hypot(grid("torque_C2_sh"), grid("torque_C2_el"))
    cost = 100.0 * (t1 / t0 - 1.0)
    benefit = 100.0 * (1.0 - t2 / t1)
    act_cost = 100.0 * (grid("act_C1") / grid("act_C0") - 1.0)
    act_benefit = 100.0 * (1.0 - grid("act_C2") / grid("act_C1"))
    # A percentage against a small baseline is misleading: at shoulder 15 /
    # elbow 40 the unpowered activation is 0.042, so 1 - C2/C1 reads -85 %
    # although the assisted case simply returns to the bare-arm level. Cells
    # whose unpowered activation is below 0.2 are left blank (n/a).
    act_benefit = np.where(grid("act_C1") >= 0.2, act_benefit, np.nan)

    with (out_dir / "workspace.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    fig, axes = plt.subplots(1, 3, figsize=(16, 4.8), constrained_layout=True)
    panels = (
        (cost, "wearing it, unpowered\n(+% effort vs bare arm)"),
        (benefit, "firmware assist\n(% effort back vs unpowered)"),
        (act_benefit, "assist, muscle activation\n(% back vs unpowered; n/a = baseline < 0.2)"),
    )
    for ax, (values, title) in zip(axes, panels):
        # pcolormesh keeps every sample at its own grid point, so the printed
        # numbers sit in the middle of their cells (imshow's extent put them on
        # the cell borders). Colours span this panel's own range, otherwise the
        # cost map (10-19 %) saturates against a symmetric +/-15 scale.
        finite = values[np.isfinite(values)]
        low, high = (float(np.percentile(finite, 2)), float(np.percentile(finite, 98))
                     if finite.size else (0.0, 1.0))
        if low < 0.0 < high:
            limit = max(abs(low), abs(high))
            low, high = -limit, limit
        cmap = plt.get_cmap("RdYlGn").copy()
        cmap.set_bad("0.85")
        mesh = ax.pcolormesh(elbows, shoulders, values, cmap=cmap,
                             vmin=low, vmax=high, shading="nearest")
        ax.set(xlabel="elbow  deg", ylabel="shoulder  deg", title=title)
        ax.set_xticks(elbows[::2])
        ax.set_yticks(shoulders[::2])
        fig.colorbar(mesh, ax=ax, label="%")
        for row, sh_deg in enumerate(shoulders):
            for column, el_deg in enumerate(elbows):
                value = values[row, column]
                text = "n/a" if not np.isfinite(value) else f"{value:+.0f}"
                ax.text(el_deg, sh_deg, text, ha="center", va="center",
                        fontsize=6.2, color="black" if np.isfinite(value) else "0.35")
    fig.savefig(out_dir / "workspace_map.png", dpi=150)
    plt.close(fig)

    summary = {
        "grid": {"shoulder_deg": shoulders.tolist(), "elbow_deg": elbows.tolist()},
        "cost_pct": {"min": float(cost.min()), "max": float(cost.max()),
                     "mean": float(cost.mean())},
        "benefit_pct": {"min": float(benefit.min()), "max": float(benefit.max()),
                        "mean": float(benefit.mean())},
        "activation_benefit_pct": {"min": float(np.nanmin(act_benefit)),
                                   "max": float(np.nanmax(act_benefit)),
                                   "mean": float(np.nanmean(act_benefit)),
                                   "blank_cells": int(np.isnan(act_benefit).sum())},
        "note": "model estimates; C0 bare, C1 worn unpowered, C2 worn + firmware",
    }
    (out_dir / "workspace.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(f"wearing cost   {cost.min():+.1f} .. {cost.max():+.1f} % (mean {cost.mean():+.1f})")
    print(f"assist benefit {benefit.min():+.1f} .. {benefit.max():+.1f} % (mean {benefit.mean():+.1f})")
    print(f"activation     {np.nanmin(act_benefit):+.1f} .. {np.nanmax(act_benefit):+.1f} % "
          f"(mean {np.nanmean(act_benefit):+.1f}; {int(np.isnan(act_benefit).sum())} cells blank)")
    print(f"wrote {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
