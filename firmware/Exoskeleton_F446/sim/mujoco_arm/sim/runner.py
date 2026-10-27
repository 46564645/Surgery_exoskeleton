"""Simulation loop for one experiment condition (M4).

The human tracks the reference with a PD on the two joints; its actuator
torque is the measured "human effort". The exoskeleton is either detached
(C0), attached but unpowered (C1), or attached and driven by the firmware
control library at the firmware's own send interval (C2).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from pathlib import Path

import mujoco
import numpy as np

import build_model as bm
from sim.controller import Controller
from sim.tasks import Task

# PD gains for the human joints (shoulder, elbow): the same in every
# condition so the comparison is like for like. Unmeasured; the tracking error
# is reported next to the effort.
GAINS = ((500.0, 30.0), (150.0, 10.0))
SETTLE_S = 0.5
SAMPLE_EVERY = 5  # record every 5 physics steps (5 ms at 1 ms timestep)


@dataclass
class Run:
    condition: str
    task: str
    time: list[float] = field(default_factory=list)
    q_sh: list[float] = field(default_factory=list)
    q_el: list[float] = field(default_factory=list)
    ref_sh: list[float] = field(default_factory=list)
    ref_el: list[float] = field(default_factory=list)
    human_sh_nm: list[float] = field(default_factory=list)
    human_el_nm: list[float] = field(default_factory=list)
    exo_sh_nm: list[float] = field(default_factory=list)
    exo_el_nm: list[float] = field(default_factory=list)
    strap_upper_n: list[float] = field(default_factory=list)
    strap_forearm_n: list[float] = field(default_factory=list)
    table_contacts: int = 0
    notes: list[str] = field(default_factory=list)

    def arrays(self) -> dict[str, np.ndarray]:
        out = {}
        for name in ("time", "q_sh", "q_el", "ref_sh", "ref_el", "human_sh_nm",
                     "human_el_nm", "exo_sh_nm", "exo_el_nm", "strap_upper_n",
                     "strap_forearm_n"):
            out[name] = np.asarray(getattr(self, name), dtype=float)
        return out


def _strap_force(model: mujoco.MjModel, data: mujoco.MjData, name: str) -> float:
    eq = model.equality(name).id
    rows = [i for i in range(data.nefc)
            if data.efc_type[i] == mujoco.mjtConstraint.mjCNSTR_EQUALITY and data.efc_id[i] == eq]
    return float(np.linalg.norm(data.efc_force[rows[:3]])) if rows else 0.0


def _human_torque(model: mujoco.MjModel, data: mujoco.MjData, target_sh: float,
                  target_el: float) -> tuple[float, float]:
    """PD torque that is written to the human actuators."""
    out = []
    for joint, target, (kp, kd) in zip((bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW),
                                       (target_sh, target_el), GAINS):
        q = data.qpos[bm.joint_qpos(model, joint)]
        v = data.qvel[bm.joint_dof(model, joint)]
        out.append(kp * (target - q) - kd * v)
    return out[0], out[1]


def run(model: mujoco.MjModel, task: Task, condition: str,
        controller: Controller | None = None, duration_s: float | None = None,
        lib: Path | str | None = None) -> Run:
    data = mujoco.MjData(model)
    table_geoms = {model.geom("table_top").id, model.geom("table_pedestal").id}
    act_h_sh = model.actuator(bm.ACT_HUMAN_SHOULDER).id
    act_h_el = model.actuator(bm.ACT_HUMAN_ELBOW).id
    act_e_sh = model.actuator(bm.ACT_EXO_SHOULDER).id
    act_e_el = model.actuator(bm.ACT_EXO_ELBOW).id

    if condition == "C0":
        # Bare arm: the exoskeleton is not strapped on, so it cannot load the
        # arm. It stays in the world (no collision) and is simply ignored.
        model.opt.disableflags |= int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
    else:
        model.opt.disableflags &= ~int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)

    if condition == "C2":
        if controller is None:
            controller = Controller(lib) if lib else Controller()
        controller.reset(0)
        period_ms = controller.site["send_interval_ms"]
        rotor_per_joint = controller.site["shoulder_rotor_per_joint"]
        kt = controller.site["elbow_motor_kt_nm_per_a"]
    else:
        period_ms = 50
        rotor_per_joint, kt = 0.0, 0.0

    # Start from the first reference pose so the run does not begin with a
    # step; the first SETTLE_S seconds are excluded from the metrics.
    sh0, el0, _, _ = task.sample(0.0)
    data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)] = sh0
    data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)] = el0
    data.qpos[bm.joint_qpos(model, bm.EXO_SHOULDER)] = sh0
    data.qpos[bm.joint_qpos(model, bm.EXO_ELBOW)] = el0
    mujoco.mj_forward(model, data)

    result = Run(condition=condition, task=task.name)
    exo_sh, exo_el = 0.0, 0.0
    duration = duration_s if duration_s is not None else task.duration_s
    steps = int(duration / model.opt.timestep)
    next_control_ms = 0.0
    for step in range(steps):
        t = data.time
        ref_sh, ref_el, _, _ = task.sample(t)
        human_sh, human_el = _human_torque(model, data, ref_sh, ref_el)
        data.ctrl[act_h_sh] = human_sh
        data.ctrl[act_h_el] = human_el

        now_ms = t * 1000.0
        if condition == "C2" and now_ms + 1e-9 >= next_control_ms:
            command = controller.step(
                shoulder_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]),
                shoulder_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_SHOULDER)]),
                elbow_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)]),
                elbow_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_ELBOW)]),
                now_ms=int(now_ms))
            if command.ok:
                exo_sh = command.shoulder_rotor_nm * rotor_per_joint
                exo_el = command.elbow_current_a * kt
            else:
                exo_sh = exo_el = 0.0
            next_control_ms += period_ms
        elif condition != "C2":
            exo_sh = exo_el = 0.0
        data.ctrl[act_e_sh] = exo_sh
        data.ctrl[act_e_el] = exo_el

        mujoco.mj_step(model, data)

        if step % SAMPLE_EVERY == 0 and t >= SETTLE_S:
            result.time.append(t)
            result.q_sh.append(float(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]))
            result.q_el.append(float(data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)]))
            result.ref_sh.append(ref_sh)
            result.ref_el.append(ref_el)
            result.human_sh_nm.append(float(data.actuator_force[act_h_sh]))
            result.human_el_nm.append(float(data.actuator_force[act_h_el]))
            result.exo_sh_nm.append(float(data.actuator_force[act_e_sh]))
            result.exo_el_nm.append(float(data.actuator_force[act_e_el]))
            if condition == "C0":
                result.strap_upper_n.append(0.0)
                result.strap_forearm_n.append(0.0)
            else:
                result.strap_upper_n.append(_strap_force(model, data, bm.STRAP_UPPER))
                result.strap_forearm_n.append(_strap_force(model, data, bm.STRAP_FOREARM))
        if condition != "C0" and any(c.geom1 in table_geoms or c.geom2 in table_geoms
                                     for c in data.contact[:data.ncon]):
            result.table_contacts += 1

    if condition == "C0":
        model.opt.disableflags &= ~int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
    return result
