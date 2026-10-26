#!/usr/bin/env python3
"""M1 static check for the table + exoskeleton + human arm model.

Rebuilds the MJCF from params.json, then checks that the scene is the one the
parameters describe and that gravity torques match the hand calculation:

  1. structure, masses and strap attachment points,
  2. gravity torque of each chain alone (straps disabled) against the analytic
     two-link formula, at several poses,
  3. the exoskeleton shoulder peak against the firmware 1.64 Nm partial model
     (s5_site.c), which is the number the controller assumes,
  4. passive stability with the straps on,
  5. the two "worn" effects: the straps make the human carry the exoskeleton
     weight (C1 heavier than C0), and a human torque drags the unpowered
     exoskeleton along.

Headless only: this machine has no working OpenGL (GPUs report Code 43), so
there is no viewer and no offscreen render. Numbers stand in for "looks right".

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
import build_model  # noqa: E402

# Firmware reference only: s5_site.c .shoulder_gravity.amplitude_nm. The
# simulation never uses it as a control value; it is the number to reproduce.
FIRMWARE_SHOULDER_PEAK_NM = 1.64

FAILURES: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    print(f"  [{'ok' if ok else 'FAIL'}] {name}{(' - ' + detail) if detail else ''}")
    if not ok:
        FAILURES.append(name)


def joint_id(model: mujoco.MjModel, name: str) -> int:
    jid = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, name)
    if jid < 0:
        raise ValueError(f"joint {name} not found")
    return jid


def dof_adr(model: mujoco.MjModel, name: str) -> int:
    return int(model.jnt_dofadr[joint_id(model, name)])


def qpos_adr(model: mujoco.MjModel, name: str) -> int:
    return int(model.jnt_qposadr[joint_id(model, name)])


def set_pose(model: mujoco.MjModel, data: mujoco.MjData, **angles_rad: float) -> None:
    data.qpos[:] = 0.0
    for name, value in angles_rad.items():
        data.qpos[qpos_adr(model, name)] = value


def holding_torques(model: mujoco.MjModel, data: mujoco.MjData,
                    shoulder: str, elbow: str) -> tuple[float, float]:
    """Gravity holding torque at (shoulder, elbow) with zero velocity."""
    data.qvel[:] = 0.0
    data.qacc[:] = 0.0
    mujoco.mj_inverse(model, data)
    return (float(data.qfrc_inverse[dof_adr(model, shoulder)]),
            float(data.qfrc_inverse[dof_adr(model, elbow)]))


def analytic_chain(segments: list[tuple[float, float, float]],
                   q1: float, q2: float, gravity: float) -> tuple[float, float]:
    """Holding torque of a serial chain in the sagittal plane.

    segments: (mass_kg, distance_from_shoulder_m, distance_from_elbow_m).
    The upper link has distance_from_elbow 0, so (d_s - d_e) is the proximal
    link length for every segment. q1 is the absolute shoulder angle and q2 the
    relative elbow angle, both from the hanging pose. Positive torque lifts.
    """
    sin1, sin12 = math.sin(q1), math.sin(q1 + q2)
    shoulder = sum(m * ((d_s - d_e) * sin1 + d_e * sin12)
                   for m, d_s, d_e in segments)
    elbow = sum(m * d_e * sin12 for m, d_s, d_e in segments)
    return gravity * shoulder, gravity * elbow


def strap_load(model: mujoco.MjModel, data: mujoco.MjData, eq_index: int) -> float:
    """Load carried by one band constraint.

    The bands are joint-angle couplings, so this is a torque in Nm, not a
    force: it is the torque the band passes from the exoskeleton to the arm.
    """
    rows = [i for i in range(data.nefc)
            if data.efc_type[i] == mujoco.mjtConstraint.mjCNSTR_EQUALITY
            and data.efc_id[i] == eq_index]
    if not rows:
        return 0.0
    return float(np.linalg.norm(data.efc_force[rows]))


def main() -> int:
    params = build_model.load_params()
    d = build_model.derive(params)
    path = build_model.write_model(d)
    model = mujoco.MjModel.from_xml_path(str(path))
    data = mujoco.MjData(model)

    hu, hf, hh = d["human_upper"], d["human_fore"], d["human_hand"]
    eu, ef, em = d["exo_upper"], d["exo_fore"], d["exo_motor"]
    gravity = d["gravity"]

    print(f"MuJoCo {mujoco.__version__}, model {path}")
    print(f"  nq={model.nq} neq={model.neq} nu={model.nu} nsensor={model.nsensor}")

    print("\n[1] scene and strap attachment")
    mujoco.mj_forward(model, data)
    table_top = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, "table_top")
    top_z = float(data.geom_xpos[table_top][2] + model.geom_size[table_top][2])
    shoulder = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "exo_upper")
    sx, sy, sz = (float(v) for v in data.body("exo_upper").xpos)
    print(f"  table top z={top_z:.3f} m, exoskeleton shoulder axis "
          f"({sx:.3f}, {sy:.3f}, {sz:.3f}) m")
    elbow_z = float(data.body("human_fore").xpos[2])
    hand_z = float(data.body("human_hand").xpos[2])
    print(f"  hanging pose: human elbow z={elbow_z:.3f} m, wrist z={hand_z:.3f} m")
    check("hanging arm clears the desk front face",
          sx + hu["radius"] < d["edge_x"],
          f"upper arm surface at x={sx + hu['radius']:.3f} m, desk edge "
          f"{d['edge_x']:.3f} m")
    check("table top at params height", abs(top_z - d["table_height"]) < 1e-6,
          f"{top_z:.4f} vs {d['table_height']:.4f}")
    check("shoulder axis height = table + column",
          abs(sz - (d["table_height"] + d["column_height"])) < 1e-6,
          f"{sz:.4f} m")

    # Cuffs are compliant welds with an explicit assembly pose, so at q = 0 the
    # constraint must be satisfied (zero violation) rather than the two marker
    # sites coinciding: the exoskeleton plane sits outside the arm.
    equality_rows = [i for i in range(data.nefc)
                     if data.efc_type[i] == mujoco.mjtConstraint.mjCNSTR_EQUALITY]
    violation = float(np.max(np.abs(data.efc_pos[equality_rows]))) if equality_rows else 0.0
    check("cuff constraints start satisfied at q=0", violation < 1e-6,
          f"max |efc_pos| {violation:.2e} over {len(equality_rows)} rows")

    exo_y = float(data.body("exo_upper").xpos[1])
    human_y = float(data.body("human_upper").xpos[1])
    clearance = abs(exo_y - human_y) - (hu["radius"] + eu["radius"])
    check("exoskeleton plane clears the arm", clearance > 0.0,
          f"lateral gap {clearance * 1e3:.0f} mm (offset {abs(exo_y - human_y) * 1e3:.0f} mm "
          f"- radii {1000 * (hu['radius'] + eu['radius']):.0f} mm)")

    exo_mass = sum(float(model.body_mass[mujoco.mj_name2id(
        model, mujoco.mjtObj.mjOBJ_BODY, n)]) for n in ("exo_upper", "exo_elbow", "exo_fore"))
    human_mass = sum(float(model.body_mass[mujoco.mj_name2id(
        model, mujoco.mjtObj.mjOBJ_BODY, n)]) for n in ("human_upper", "human_fore", "human_hand"))
    check("exoskeleton moving mass", abs(exo_mass - (eu["mass"] + ef["mass"] + em["mass"])) < 1e-6,
          f"{exo_mass:.3f} kg")
    check("human arm mass", abs(human_mass - (hu["mass"] + hf["mass"] + hh["mass"])) < 1e-6,
          f"{human_mass:.3f} kg")

    exo_segments = [
        (eu["mass"], eu["com"], 0.0),
        (em["mass"], eu["length"], 0.0),
        (ef["mass"], eu["length"] + ef["com"], ef["com"]),
    ]
    human_segments = [
        (hu["mass"], hu["com"], 0.0),
        (hf["mass"], hu["length"] + hf["com"], hf["com"]),
        (hh["mass"], hu["length"] + hf["length"] + hh["com"], hf["length"] + hh["com"]),
    ]

    print("\n[2] gravity torque, straps off (each chain on its own)")
    # Contacts are disabled as well: the pose set sweeps over the desk volume,
    # and a contact would put its reaction into qfrc_inverse. Pure gravity is
    # what this section compares against the formula.
    model.opt.disableflags |= int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
    model.opt.disableflags |= int(mujoco.mjtDisableBit.mjDSBL_CONTACT)
    print("  pose shoulder/elbow deg | exo shoulder, exo elbow | human shoulder, human elbow")
    worst = 0.0
    for sh_deg, el_deg in ((0, 0), (30, 0), (60, 0), (90, 0), (0, 90), (60, 90)):
        q1, q2 = math.radians(sh_deg), math.radians(el_deg)
        set_pose(model, data, exo_shoulder=q1, exo_elbow=q2,
                 human_shoulder=q1, human_elbow=q2)
        exo_sh, exo_el = holding_torques(model, data, "exo_shoulder", "exo_elbow")
        hum_sh, hum_el = holding_torques(model, data, "human_shoulder", "human_elbow")
        a_exo_sh, a_exo_el = analytic_chain(exo_segments, q1, q2, gravity)
        a_hum_sh, a_hum_el = analytic_chain(human_segments, q1, q2, gravity)
        row_diff = max(abs(exo_sh - a_exo_sh), abs(exo_el - a_exo_el),
                       abs(hum_sh - a_hum_sh), abs(hum_el - a_hum_el))
        worst = max(worst, row_diff)
        print(f"  {sh_deg:3d}/{el_deg:3d}            | "
              f"{exo_sh:+7.3f} {a_exo_sh:+7.3f} {exo_el:+7.3f} {a_exo_el:+7.3f} | "
              f"{hum_sh:+7.3f} {a_hum_sh:+7.3f} {hum_el:+7.3f} {a_hum_el:+7.3f}")
    check("mj_inverse matches the two-link formula", worst < 0.01,
          f"max deviation {worst:.2e} Nm")

    set_pose(model, data, exo_shoulder=math.pi / 2, exo_elbow=0.0,
             human_shoulder=0.0, human_elbow=0.0)
    exo_peak, _ = holding_torques(model, data, "exo_shoulder", "exo_elbow")
    print(f"\n[3] exoskeleton shoulder peak at horizontal: {exo_peak:.3f} Nm "
          f"(firmware s5_site.c model {FIRMWARE_SHOULDER_PEAK_NM:.2f} Nm)")
    check("firmware 1.64 Nm is the exoskeleton self weight only",
          abs(exo_peak - FIRMWARE_SHOULDER_PEAK_NM) < 0.05,
          f"deviation {abs(exo_peak - FIRMWARE_SHOULDER_PEAK_NM):.3f} Nm")

    model.opt.disableflags &= ~int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
    model.opt.disableflags &= ~int(mujoco.mjtDisableBit.mjDSBL_CONTACT)

    print("\n[4] straps on, passive 2 s from the hanging pose")
    mujoco.mj_resetData(model, data)
    for _ in range(2000):
        mujoco.mj_step(model, data)
    max_vel = float(np.max(np.abs(data.qvel)))
    max_angle = float(np.max(np.abs(data.qpos)))
    check("passive model stays put", max_vel < 0.02 and max_angle < math.radians(2.0),
          f"max|qvel|={max_vel:.2e} rad/s, max|q|={math.degrees(max_angle):.3f} deg")

    print("\n[5] worn effects at 60 deg shoulder, 0 deg elbow (forward dynamics)")
    # Inverse dynamics cannot show this: with the pose prescribed on both
    # chains the straps need no force. The exoskeleton joints are passive, so
    # its weight only reaches the arm if it is allowed to hang. Both human
    # joints are held by a stiff PD (the elbow must be held too, otherwise the
    # forearm just hangs vertically); the steady actuator torque is the effort.
    hold = (math.radians(60.0), 0.0)
    human_motors = [mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_ACTUATOR, name)
                    for name in ("human_shoulder_motor", "human_elbow_motor")]
    human_joints = ("human_shoulder", "human_elbow")
    gains = ((400.0, 20.0), (100.0, 8.0))

    def hold_torques(straps: bool) -> tuple[float, float, float]:
        flags = int(mujoco.mjtDisableBit.mjDSBL_CONTACT)
        if not straps:
            flags |= int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
        model.opt.disableflags = flags
        mujoco.mj_resetData(model, data)
        data.qpos[qpos_adr(model, "human_shoulder")] = hold[0]
        for _ in range(8000):
            data.ctrl[:] = 0.0
            for index, (joint, target, (kp, kd)) in enumerate(
                    zip(human_joints, hold, gains)):
                torque = (kp * (target - data.qpos[qpos_adr(model, joint)])
                          - kd * data.qvel[dof_adr(model, joint)])
                data.ctrl[human_motors[index]] = max(-40.0, min(40.0, torque))
            mujoco.mj_step(model, data)
        shoulder = math.degrees(data.qpos[qpos_adr(model, "human_shoulder")])
        return (float(data.ctrl[human_motors[0]]),
                float(data.ctrl[human_motors[1]]), shoulder)

    c1_sh, c1_el, c1_angle = hold_torques(straps=True)
    c0_sh, c0_el, c0_angle = hold_torques(straps=False)
    model.opt.disableflags = 0
    expected_extra = 1.638 * math.sin(math.radians(c1_angle))
    print(f"  hold {c0_angle:.2f} deg (bare) / {c1_angle:.2f} deg (worn):")
    print(f"    shoulder {c0_sh:.3f} -> {c1_sh:.3f} Nm "
          f"(+{100.0 * (c1_sh / c0_sh - 1.0):.1f}%), "
          f"elbow {c0_el:.3f} -> {c1_el:.3f} Nm")
    check("worn and unpowered is heavier than bare arm",
          c1_sh > c0_sh * 1.05, f"{c1_sh:.3f} > {c0_sh:.3f} Nm")
    check("worn extra shoulder effort is the exoskeleton weight",
          abs((c1_sh - c0_sh) - expected_extra) < 0.15,
          f"extra {c1_sh - c0_sh:.3f} Nm vs exoskeleton {expected_extra:.3f} Nm")

    print("\n[6] coupling: 8 Nm on the human shoulder, exoskeleton unpowered, 1 s")
    mujoco.mj_resetData(model, data)
    data.ctrl[:] = 0.0
    data.ctrl[mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_ACTUATOR,
                               "human_shoulder_motor")] = 8.0
    for _ in range(1000):
        mujoco.mj_step(model, data)
    h_sh = math.degrees(data.qpos[qpos_adr(model, "human_shoulder")])
    e_sh = math.degrees(data.qpos[qpos_adr(model, "exo_shoulder")])
    gap = abs(h_sh - e_sh)
    print(f"  human shoulder {h_sh:.2f} deg, exoskeleton shoulder {e_sh:.2f} deg, "
          f"band torque {strap_load(model, data, 0):.2f} Nm")
    check("human arm lifted against gravity", h_sh > 20.0, f"{h_sh:.1f} deg")
    check("exoskeleton follows the arm through the straps", gap < 2.0,
          f"difference {gap:.2f} deg")

    print("\n[7] coupling: 2 Nm on the human elbow, exoskeleton unpowered, 1 s")
    mujoco.mj_resetData(model, data)
    data.ctrl[:] = 0.0
    data.ctrl[mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_ACTUATOR,
                               "human_elbow_motor")] = 2.0
    for _ in range(1000):
        mujoco.mj_step(model, data)
    h_el = math.degrees(data.qpos[qpos_adr(model, "human_elbow")])
    e_el = math.degrees(data.qpos[qpos_adr(model, "exo_elbow")])
    print(f"  human elbow {h_el:.2f} deg, exoskeleton elbow {e_el:.2f} deg, "
          f"band torque {strap_load(model, data, 1):.2f} Nm")
    check("human elbow bent", h_el > 5.0, f"{h_el:.1f} deg")
    check("exoskeleton elbow follows", e_el > 5.0, f"{e_el:.1f} deg")

    print()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)} check(s): {', '.join(FAILURES)}")
        return 1
    print("all M1 static checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
