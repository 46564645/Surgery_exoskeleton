"""Muscle-driven burst simulation (M6 continuation, 2026-10-27).

Instead of an ideal torque at the shoulder and elbow, the human arm is driven
by MyoArm's 63 muscles: a PD plus gravity feedforward produces the torque the
task needs, the static optimisation turns it into activations, and MuJoCo's
own muscle activation dynamics then produce the force. That makes the burst
behaviour realistic in the two respects the ideal torque misses: torque cannot
rise instantly, and only muscles can pull.

The exoskeleton side is unchanged: the firmware control library is called at
its own send interval and its output is applied to the two exoskeleton
actuators.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import mujoco
import numpy as np

import build_model as bm
from sim.burst import HOLD_S, TORQUE_LAG_S, Burst, reference
from sim.controller import Controller
from sim.muscles import MuscleEffort

# PD that asks for the torque; the muscles then have to deliver it.
# The integral term is available but off: at the shoulder it made the response
# noisier without curing the sag (see the log, 2026-10-27), so the shoulder
# burst is still a known limitation of this simplified controller.
PD_GAINS = ((60.0, 8.0), (30.0, 3.0))
KI_GAINS = (0.0, 0.0)
INTEGRAL_LIMIT_NM = 25.0
CONTROL_PERIOD_MS = 10.0


@dataclass
class MuscleRun:
    condition: str
    burst: str
    time: list[float] = field(default_factory=list)
    q: list[float] = field(default_factory=list)
    q_dot: list[float] = field(default_factory=list)
    human_nm: list[float] = field(default_factory=list)
    activation_sum: list[float] = field(default_factory=list)
    exo_nm: list[float] = field(default_factory=list)

    def arrays(self) -> dict[str, np.ndarray]:
        return {name: np.asarray(getattr(self, name), dtype=float)
                for name in ("time", "q", "q_dot", "human_nm", "activation_sum", "exo_nm")}


def run_muscle_burst(burst: Burst, condition: str, controller: Controller | None = None,
                     viewer=None, record_stride: int = 5,
                     exo_period_ms: float | None = None) -> MuscleRun:
    """One burst run.

    ``exo_period_ms`` overrides the firmware send interval, so the effect of
    the update rate can be compared with the same control law.
    """
    model, _ = bm.build(write=False, keep_muscles=True)
    data = mujoco.MjData(model)
    effort = MuscleEffort(model=model)
    # Muscle actuators are the ones with an activation state; the two
    # exoskeleton motors have none.
    muscle_ids = [i for i in range(model.nu) if model.actuator_actadr[i] >= 0]
    muscle_act = [int(model.actuator_actadr[i]) for i in muscle_ids]
    exo_sh = model.actuator(bm.ACT_EXO_SHOULDER).id
    exo_el = model.actuator(bm.ACT_EXO_ELBOW).id

    if condition == "C0":
        model.opt.disableflags |= int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
    else:
        model.opt.disableflags &= ~int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)

    if condition == "C2":
        controller = controller or Controller()
        controller.reset(0)
        rotor_per_joint = controller.site["shoulder_rotor_per_joint"]
        kt = controller.site["elbow_motor_kt_nm_per_a"]
        period_ms = (float(exo_period_ms) if exo_period_ms is not None
                     else float(controller.site["send_interval_ms"]))
    else:
        rotor_per_joint = kt = 0.0
        period_ms = 10.0

    sh0, el0 = reference(burst, 0.0)
    data.qpos[:] = 0.0
    for joint, q in ((bm.HUMAN_SHOULDER, sh0), (bm.HUMAN_ELBOW, el0),
                     (bm.EXO_SHOULDER, sh0), (bm.EXO_ELBOW, el0)):
        data.qpos[bm.joint_qpos(model, joint)] = q
    mujoco.mj_forward(model, data)

    result = MuscleRun(condition=condition, burst=burst.name)
    exo = (0.0, 0.0)
    next_control_ms = 0.0
    next_exo_ms = 0.0
    alpha = model.opt.timestep / (TORQUE_LAG_S + model.opt.timestep)
    applied = [0.0, 0.0]
    integral = [0.0, 0.0]
    activations = np.zeros(len(muscle_ids))
    steps = int(burst.duration_s / model.opt.timestep)
    for step in range(steps):
        ref_sh, ref_el = reference(burst, data.time)
        now_ms = data.time * 1000.0

        if now_ms + 1e-9 >= next_control_ms:
            # Computed-muscle-control style demand: set the desired joint
            # acceleration and let mj_inverse give the generalized force that
            # produces it, so the mass matrix, the bias, the passive muscle
            # forces and the strap constraint are all included. This is the
            # approach MyoSuite documents (tutorials 3.3/3.4) and the one the
            # maintainers recommend for muscle models.
            desired = []
            for joint, target, (kp, kd) in zip((bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW),
                                               (ref_sh, ref_el), PD_GAINS):
                error = target - data.qpos[bm.joint_qpos(model, joint)]
                desired.append(kp * error - kd * data.qvel[bm.joint_dof(model, joint)])
            # tau = M(q) a_des + bias - passive - constraint. mj_inverse is not
            # used: in this MuJoCo version it leaves qfrc_inverse at zero
            # unless the discrete flag is set, and that changes the meaning.
            mass = np.zeros((model.nv, model.nv))
            mujoco.mj_fullM(model, data, mass)
            acceleration = np.zeros(model.nv)
            acceleration[bm.joint_dof(model, bm.HUMAN_SHOULDER)] = desired[0]
            acceleration[bm.joint_dof(model, bm.HUMAN_ELBOW)] = desired[1]
            required = (mass @ acceleration + data.qfrc_bias - data.qfrc_passive
                        - data.qfrc_constraint)
            demand = [float(required[bm.joint_dof(model, bm.HUMAN_SHOULDER)]),
                      float(required[bm.joint_dof(model, bm.HUMAN_ELBOW)])]
            every_actuator, _ = effort.activations(
                float(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]),
                float(data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)]),
                demand[0], demand[1])
            # effort returns one value per actuator; keep the muscles only.
            activations = every_actuator[muscle_ids]
            next_control_ms += CONTROL_PERIOD_MS

        data.ctrl[muscle_ids] = activations
        if condition == "C2" and now_ms + 1e-9 >= next_exo_ms:
            command = controller.step(
                shoulder_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]),
                shoulder_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_SHOULDER)]),
                elbow_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)]),
                elbow_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_ELBOW)]),
                now_ms=int(now_ms))
            exo = ((command.shoulder_rotor_nm * rotor_per_joint,
                    command.elbow_current_a * kt) if command.ok else (0.0, 0.0))
            next_exo_ms += period_ms
        data.ctrl[exo_sh], data.ctrl[exo_el] = exo

        mujoco.mj_step(model, data)
        if viewer is not None and step % 5 == 0:
            viewer.sync()

        if step % record_stride == 0:
            joint = burst.joint
            dof = bm.joint_dof(model, joint)
            result.time.append(data.time)
            result.q.append(float(data.qpos[bm.joint_qpos(model, joint)]))
            result.q_dot.append(float(data.qvel[dof]))
            result.human_nm.append(float(data.qfrc_actuator[dof]))
            result.activation_sum.append(float(np.sum(data.act[muscle_act])))
            result.exo_nm.append(float(data.actuator_force[exo_sh if joint == bm.HUMAN_SHOULDER
                                                           else exo_el]))
    model.opt.disableflags = 0
    return result
