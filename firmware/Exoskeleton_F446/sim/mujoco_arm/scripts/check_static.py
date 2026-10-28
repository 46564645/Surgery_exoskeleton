#!/usr/bin/env python3
"""M1b check: MyoArm + Fusion OrthoEase + table, "roughly right" level.

Acceptance follows the sim AGENTS rule (main goal roughly confirmed, no
percentage gates): the scene relations are the ones discussed with the user,
gravity torques have a sensible size, and the straps carry the exoskeleton and
let the elbow bend.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\check_static.py
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import mujoco
import numpy as np

SIM_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_ROOT / "model"))
import build_model as bm  # noqa: E402

# Firmware reference only (s5_site.c .shoulder_gravity.amplitude_nm): the
# number the controller assumes for the exoskeleton self weight.
FIRMWARE_SHOULDER_PEAK_NM = 1.64
FAILURES: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    print(f"  [{'ok' if ok else 'FAIL'}] {name}{(' - ' + detail) if detail else ''}")
    if not ok:
        FAILURES.append(name)


def pose(model, data, **deg) -> None:
    data.qpos[:] = 0.0
    data.qvel[:] = 0.0
    for joint, value in deg.items():
        data.qpos[bm.joint_qpos(model, joint)] = math.radians(value)
    mujoco.mj_forward(model, data)


def holding(model, data, joints) -> list[float]:
    data.qvel[:] = 0.0
    data.qacc[:] = 0.0
    mujoco.mj_inverse(model, data)
    return [float(data.qfrc_inverse[bm.joint_dof(model, j)]) for j in joints]


def strap_force(model, data, name: str) -> float:
    """Translational force (N) carried by one weld band."""
    eq = model.equality(name).id
    rows = [i for i in range(data.nefc)
            if data.efc_type[i] == mujoco.mjtConstraint.mjCNSTR_EQUALITY and data.efc_id[i] == eq]
    return float(np.linalg.norm(data.efc_force[rows[:3]])) if rows else 0.0


def run_pd(model, data, targets_deg, steps, flags=0, extra_ctrl=None):
    """Hold the two human joints with a PD; returns the final human torques."""
    model.opt.disableflags = flags
    acts = [model.actuator(bm.ACT_HUMAN_SHOULDER).id, model.actuator(bm.ACT_HUMAN_ELBOW).id]
    joints = (bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW)
    gains = ((400.0, 25.0), (150.0, 10.0))
    for _ in range(steps):
        data.ctrl[:] = 0.0
        if extra_ctrl:
            for name, value in extra_ctrl.items():
                data.ctrl[model.actuator(name).id] = value
        for act, joint, target, (kp, kd) in zip(acts, joints, targets_deg, gains):
            err = math.radians(target) - data.qpos[bm.joint_qpos(model, joint)]
            data.ctrl[act] = kp * err - kd * data.qvel[bm.joint_dof(model, joint)]
        mujoco.mj_step(model, data)
    model.opt.disableflags = 0
    return [float(data.actuator_force[a]) for a in acts]


def main() -> int:
    model, info = bm.build()
    data = mujoco.MjData(model)
    land = info["human"]
    print(f"MuJoCo {mujoco.__version__}, model {info['xml']}")
    print(f"  nq={model.nq} nu={model.nu} neq={model.neq} nbody={model.nbody}")

    print("\n[1] scene relations")
    pose(model, data)
    sh = land["shoulder"]
    exo_sh = data.xpos[model.body(bm.EXO_UPPER_BODY).id]
    exo_el = data.xpos[model.body(bm.EXO_FOREARM_BODY).id]
    axis_gap = math.hypot(exo_sh[1] - sh[1], exo_sh[2] - sh[2])
    check("exoskeleton shoulder axis passes through the human shoulder centre",
          axis_gap < 0.005, f"in-plane gap {axis_gap * 1000:.1f} mm, lateral offset "
          f"{(sh[0] - exo_sh[0]) * 1000:.0f} mm (exoskeleton on the right/-x side)")
    el = land["elbow"]
    el_gap = math.hypot(exo_el[1] - el[1], exo_el[2] - el[2])
    check("exoskeleton elbow axis meets the human elbow (rail extended)", el_gap < 0.005,
          f"in-plane gap {el_gap * 1000:.1f} mm, upper arm {info['exo_upper_len_after'] * 1000:.1f} mm")
    check("exoskeleton is lateral to the right arm", exo_sh[0] < sh[0] and exo_el[0] < el[0])
    d4 = info["seat_top_z"] - info["table_top_z"] + info["pole_length"]
    print(f"  seat + pole {d4:.3f} m, table top to shoulder axis {sh[2] - info['table_top_z']:.3f} m")
    hand_ys = [data.xpos[b][1] for b in range(model.nbody)
               if model.body(b).name.endswith("_r") and data.xpos[b][2] < info["table_top_z"]]
    if hand_ys:
        check("hanging arm stays outside the table edge", min(hand_ys) > info["table_edge_y"],
              f"arm parts below the table top reach y={min(hand_ys):.3f} m, edge y={info['table_edge_y']:.3f} m")
    else:
        check("hanging arm stays outside the table edge", True,
              "the hanging hand is above the table top")
    pole_xy = np.array(info["pole_xy"])
    link_xy = np.array([exo_sh[0], exo_sh[1]])
    check("pole runs parallel behind the hanging links with a gap",
          np.linalg.norm(pole_xy - link_xy) > 0.05,
          f"pole to shoulder-axis point {np.linalg.norm(pole_xy - link_xy) * 1000:.0f} mm horizontally")
    bore_xy = np.array(info["clamp_bore_world"])
    check("pole axis goes through the 01B clamp bore", np.linalg.norm(pole_xy - bore_xy) < 1e-6,
          f"bore at ({bore_xy[0]:.4f}, {bore_xy[1]:.4f}) m, pole on the same axis")
    check("pole fits the bore", info["pole_radius"] < info["clamp_bore_radius"],
          f"pole dia {2 * info['pole_radius'] * 1000:.1f} mm in a "
          f"{2 * info['clamp_bore_radius'] * 1000:.1f} mm bore")
    print(f"  cuff inner surface to bone axis: upper {info['upper_cuff_offset'] * 1000:.1f} mm "
          f"(section 56), forearm {info['forearm_cuff_offset'] * 1000:.1f} mm (section 37)")

    print("\n[1b] band force at a prescribed elbow bend (the bands must not fight the arm)")
    for deg in (30, 60, 90):
        pose(model, data, **{bm.HUMAN_ELBOW: deg, bm.EXO_ELBOW: deg})
        upper = strap_force(model, data, bm.STRAP_UPPER)
        forearm = strap_force(model, data, bm.STRAP_FOREARM)
        print(f"  elbow {deg:2d} deg: upper band {upper:5.1f} N, forearm band {forearm:5.1f} N")
        if deg == 90:
            check("band force at 90 deg elbow stays in the tens of newtons",
                  upper < 60.0 and forearm < 60.0, f"{upper:.0f}/{forearm:.0f} N")

    print("\n[2] masses")
    for name, mass in info["exo_body_mass"].items():
        print(f"  {name:24s} {mass:.3f} kg")
    print(f"  human arm {land['upper_mass']:.3f} kg (forearm + hand {land['forearm_mass']:.3f} kg)")
    check("exoskeleton moving mass is the STL estimate, not the package placeholder",
          info["exo_body_mass"][bm.EXO_UPPER_BODY] < 1.0 and info["exo_body_mass"][bm.EXO_FOREARM_BODY] < 0.5)

    print("\n[3] gravity torques, straps and contacts off")
    model.opt.disableflags = int(mujoco.mjtDisableBit.mjDSBL_EQUALITY) | int(mujoco.mjtDisableBit.mjDSBL_CONTACT)
    print("  shoulder/elbow deg | exo shoulder, exo elbow | human shoulder, human elbow (N*m)")
    for s_deg, e_deg in ((0, 0), (30, 0), (60, 0), (90, 0), (60, 60), (90, 90)):
        pose(model, data, **{bm.EXO_SHOULDER: s_deg, bm.EXO_ELBOW: e_deg,
                             bm.HUMAN_SHOULDER: s_deg, bm.HUMAN_ELBOW: e_deg})
        es, ee = holding(model, data, (bm.EXO_SHOULDER, bm.EXO_ELBOW))
        hs, he = holding(model, data, (bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW))
        print(f"  {s_deg:3d}/{e_deg:3d}             | {es:+7.3f} {ee:+7.3f}        | {hs:+7.3f} {he:+7.3f}")
    pose(model, data, **{bm.EXO_SHOULDER: 90})
    peak, _ = holding(model, data, (bm.EXO_SHOULDER, bm.EXO_ELBOW))
    pose(model, data, **{bm.HUMAN_SHOULDER: 90})
    human_peak, _ = holding(model, data, (bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW))
    model.opt.disableflags = 0
    print(f"  exoskeleton self weight at horizontal {peak:.3f} N*m (firmware model "
          f"{FIRMWARE_SHOULDER_PEAK_NM:.2f}); human arm at horizontal {human_peak:.3f} N*m")
    check("exoskeleton self-weight torque has a sensible size", 0.5 < peak < 3.0, f"{peak:.3f} N*m")
    check("human arm torque has a sensible size", 5.0 < human_peak < 20.0, f"{human_peak:.3f} N*m")

    print("\n[4] straps on, passive 2 s from the hanging pose")
    mujoco.mj_resetData(model, data)
    for _ in range(2000):
        mujoco.mj_step(model, data)
    yaw = math.degrees(data.qpos[bm.joint_qpos(model, bm.EXO_YAW)])
    check("passive model stays put", float(np.max(np.abs(data.qvel))) < 0.05,
          f"max|qvel| {np.max(np.abs(data.qvel)):.2e} rad/s, turntable {yaw:+.2f} deg")

    print("\n[5] worn and unpowered vs bare arm, PD hold 60 deg shoulder / 0 deg elbow, 6 s")
    mujoco.mj_resetData(model, data)
    pose(model, data, **{bm.HUMAN_SHOULDER: 60, bm.EXO_SHOULDER: 60})
    c1 = run_pd(model, data, (60, 0), 6000)
    mujoco.mj_resetData(model, data)
    pose(model, data, **{bm.HUMAN_SHOULDER: 60})
    c0 = run_pd(model, data, (60, 0), 6000, flags=int(mujoco.mjtDisableBit.mjDSBL_EQUALITY))
    print(f"  human shoulder {c0[0]:.3f} -> {c1[0]:.3f} N*m (+{c1[0] - c0[0]:.3f}), "
          f"elbow {c0[1]:.3f} -> {c1[1]:.3f} N*m")
    check("worn and unpowered is heavier than the bare arm", c1[0] > c0[0] + 0.3,
          f"extra {c1[0] - c0[0]:.3f} N*m vs exoskeleton {peak * math.sin(math.radians(60)):.3f} N*m")

    print("\n[6] coupling through the straps, exoskeleton unpowered (contacts off: "
          "only the bands are tested)")
    model.opt.disableflags = int(mujoco.mjtDisableBit.mjDSBL_CONTACT)
    mujoco.mj_resetData(model, data)
    data.ctrl[:] = 0.0
    data.ctrl[model.actuator(bm.ACT_HUMAN_SHOULDER).id] = 12.0
    for _ in range(1000):
        mujoco.mj_step(model, data)
    h = math.degrees(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)])
    e = math.degrees(data.qpos[bm.joint_qpos(model, bm.EXO_SHOULDER)])
    print(f"  12 N*m human shoulder, 1 s: human {h:.1f} deg, exoskeleton {e:.1f} deg, "
          f"upper strap {strap_force(model, data, bm.STRAP_UPPER):.1f} N")
    check("exoskeleton follows the arm", h > 15.0 and abs(h - e) < 5.0, f"difference {abs(h - e):.1f} deg")
    mujoco.mj_resetData(model, data)
    data.ctrl[:] = 0.0
    data.ctrl[model.actuator(bm.ACT_HUMAN_ELBOW).id] = 3.0
    for _ in range(1000):
        mujoco.mj_step(model, data)
    h = math.degrees(data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)])
    e = math.degrees(data.qpos[bm.joint_qpos(model, bm.EXO_ELBOW)])
    print(f"  3 N*m human elbow, 1 s: human {h:.1f} deg, exoskeleton {e:.1f} deg, "
          f"forearm strap {strap_force(model, data, bm.STRAP_FOREARM):.1f} N")
    check("elbow bends freely with the exoskeleton", h > 20.0 and abs(h - e) < 5.0,
          f"difference {abs(h - e):.1f} deg")
    model.opt.disableflags = 0

    print()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)} check(s): {', '.join(FAILURES)}")
        return 1
    print("all M1b checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
