"""Stage G: burst (explosive) movements, recorded at 1 ms.

Three tasks modelled on orthopaedic actions:

  G1 mallet strike   elbow 100 -> 45 deg in 180 ms, hold 2 s, ten repeats
  G2 rapid lift      shoulder 5 -> 45 deg in 250 ms, hold 2 s, ten repeats
  G3 burst and stop  the same motions, judged by overshoot and settling time

The human tracks the burst with a stiffer PD than the static tasks. Metrics
follow the explosive-strength literature: peak joint velocity, peak torque,
rate of torque development over 0-50 and 0-100 ms from onset, time to peak
torque, movement time (10-90 % of the displacement) and, for the stop phase,
overshoot and settling time.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import mujoco
import numpy as np

import build_model as bm
from sim.controller import Controller
from sim.runner import GAINS

# Burst control: moderate gains plus a first-order torque lag, because a
# person cannot step the torque. The 45 ms lag stands for muscle activation
# rise; with it the peak torques land in the range a strike or a quick lift
# would need instead of the PD's saturated initial kick.
BURST_GAINS = ((60.0, 8.0), (30.0, 3.0))
TORQUE_LAG_S = 0.045
HOLD_S = 2.0


@dataclass(frozen=True)
class Burst:
    name: str
    joint: str                      # joint that makes the movement
    start_deg: float
    end_deg: float
    move_s: float
    repeats: int
    description: str

    @property
    def period_s(self) -> float:
        """Burst, hold at the target, then a return stroke to the start."""
        return 2.0 * self.move_s + HOLD_S

    @property
    def duration_s(self) -> float:
        return self.repeats * self.period_s


BURSTS = {
    "G1": Burst("G1", bm.HUMAN_ELBOW, 100.0, 45.0, 0.18, 10,
                "mallet strike: elbow extension 100->45 deg in 180 ms"),
    "G2": Burst("G2", bm.HUMAN_SHOULDER, 5.0, 45.0, 0.25, 10,
                "rapid lift: shoulder flexion 5->45 deg in 250 ms"),
}


def reference(burst: Burst, t: float) -> tuple[float, float]:
    """(shoulder, elbow) reference in radians at time t.

    One period is: burst (start -> end in move_s), hold at the target, then a
    return stroke to the start over the same move_s, so the reference is
    continuous from one repeat to the next.
    """
    period = burst.period_s
    k = min(int(t // period), burst.repeats - 1)
    local = t - k * period
    start = math.radians(burst.start_deg)
    end = math.radians(burst.end_deg)

    def smooth(u: float) -> float:
        u = min(max(u, 0.0), 1.0)
        return 10 * u ** 3 - 15 * u ** 4 + 6 * u ** 5

    if local <= burst.move_s:
        value = start + (end - start) * smooth(local / burst.move_s)
    elif local <= burst.move_s + HOLD_S:
        value = end
    else:
        value = end + (start - end) * smooth((local - burst.move_s - HOLD_S) / burst.move_s)
    if burst.joint == bm.HUMAN_SHOULDER:
        return value, math.radians(90.0)
    return math.radians(20.0), value


@dataclass
class BurstRun:
    condition: str
    burst: str
    time: list[float] = field(default_factory=list)
    q: list[float] = field(default_factory=list)
    q_dot: list[float] = field(default_factory=list)
    ref: list[float] = field(default_factory=list)
    human_nm: list[float] = field(default_factory=list)
    exo_nm: list[float] = field(default_factory=list)

    def arrays(self) -> dict[str, np.ndarray]:
        return {name: np.asarray(getattr(self, name), dtype=float)
                for name in ("time", "q", "q_dot", "ref", "human_nm", "exo_nm")}


def run_burst(model: mujoco.MjModel, burst: Burst, condition: str,
              controller: Controller | None = None) -> BurstRun:
    data = mujoco.MjData(model)
    act_h = (model.actuator(bm.ACT_HUMAN_SHOULDER).id, model.actuator(bm.ACT_HUMAN_ELBOW).id)
    act_e = (model.actuator(bm.ACT_EXO_SHOULDER).id, model.actuator(bm.ACT_EXO_ELBOW).id)
    if condition == "C0":
        model.opt.disableflags |= int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)
    else:
        model.opt.disableflags &= ~int(mujoco.mjtDisableBit.mjDSBL_EQUALITY)

    if condition == "C2":
        controller = controller or Controller()
        controller.reset(0)
        rotor_per_joint = controller.site["shoulder_rotor_per_joint"]
        kt = controller.site["elbow_motor_kt_nm_per_a"]
        period_ms = float(controller.site["send_interval_ms"])
    else:
        rotor_per_joint = kt = 0.0
        period_ms = 50.0

    sh0, el0 = reference(burst, 0.0)
    data.qpos[:] = 0.0
    for joint, q in ((bm.HUMAN_SHOULDER, sh0), (bm.HUMAN_ELBOW, el0),
                     (bm.EXO_SHOULDER, sh0), (bm.EXO_ELBOW, el0)):
        data.qpos[bm.joint_qpos(model, joint)] = q
    mujoco.mj_forward(model, data)

    target_dof = bm.joint_dof(model, burst.joint)
    target_qpos = bm.joint_qpos(model, burst.joint)
    result = BurstRun(condition=condition, burst=burst.name)
    exo = (0.0, 0.0)
    next_ms = 0.0
    steps = int(burst.duration_s / model.opt.timestep)
    alpha = model.opt.timestep / (TORQUE_LAG_S + model.opt.timestep)
    applied = [0.0, 0.0]
    scratch = mujoco.MjData(model)
    feedforward = [0.0, 0.0]
    next_ff_ms = -1.0

    def gravity_feedforward(ref_sh: float, ref_el: float) -> list[float]:
        """Torque that holds the reference pose against gravity (bare arm)."""
        scratch.qpos[:] = 0.0
        scratch.qvel[:] = 0.0
        scratch.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)] = ref_sh
        scratch.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)] = ref_el
        mujoco.mj_forward(model, scratch)
        return [float(scratch.qfrc_bias[bm.joint_dof(model, bm.HUMAN_SHOULDER)]),
                float(scratch.qfrc_bias[bm.joint_dof(model, bm.HUMAN_ELBOW)])]
    for _ in range(steps):
        ref_sh, ref_el = reference(burst, data.time)
        if data.time * 1000.0 >= next_ff_ms:
            feedforward = gravity_feedforward(ref_sh, ref_el)
            next_ff_ms = data.time * 1000.0 + 20.0
        data.ctrl[:] = 0.0
        for index, (act, joint, target, (kp, kd)) in enumerate(
                zip(act_h, (bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW), (ref_sh, ref_el), BURST_GAINS)):
            error = target - data.qpos[bm.joint_qpos(model, joint)]
            wanted = feedforward[index] + kp * error - kd * data.qvel[bm.joint_dof(model, joint)]
            applied[index] += alpha * (wanted - applied[index])
            data.ctrl[act] = applied[index]
        now_ms = data.time * 1000.0
        if condition == "C2" and now_ms + 1e-9 >= next_ms:
            command = controller.step(
                shoulder_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]),
                shoulder_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_SHOULDER)]),
                elbow_rad=float(data.qpos[bm.joint_qpos(model, bm.HUMAN_ELBOW)]),
                elbow_vel=float(data.qvel[bm.joint_dof(model, bm.HUMAN_ELBOW)]),
                now_ms=int(now_ms))
            exo = ((command.shoulder_rotor_nm * rotor_per_joint,
                    command.elbow_current_a * kt) if command.ok else (0.0, 0.0))
            next_ms += period_ms
        data.ctrl[act_e[0]], data.ctrl[act_e[1]] = exo
        mujoco.mj_step(model, data)
        result.time.append(data.time)
        result.q.append(float(data.qpos[target_qpos]))
        result.q_dot.append(float(data.qvel[target_dof]))
        result.ref.append(ref_sh if burst.joint == bm.HUMAN_SHOULDER else ref_el)
        result.human_nm.append(float(data.actuator_force[act_h[0] if burst.joint == bm.HUMAN_SHOULDER else act_h[1]]))
        result.exo_nm.append(float(data.actuator_force[act_e[0] if burst.joint == bm.HUMAN_SHOULDER else act_e[1]]))
    model.opt.disableflags = 0
    return result


def burst_metrics(run: BurstRun, burst: Burst) -> dict:
    a = run.arrays()
    t, q, v, tau, ref = a["time"], a["q"], a["q_dot"], a["human_nm"], a["ref"]
    dt = float(np.median(np.diff(t)))
    start, end = math.radians(burst.start_deg), math.radians(burst.end_deg)
    sign = 1.0 if end > start else -1.0
    travel = abs(end - start)
    periods = []
    for k in range(burst.repeats):
        t0 = k * burst.period_s
        mask = (t >= t0) & (t < t0 + burst.move_s + 0.6)
        if not mask.any():
            continue
        seg_t, seg_q, seg_v, seg_tau = t[mask], q[mask], v[mask], tau[mask]
        # onset: the torque or velocity first moves towards the target
        moving = np.where(sign * (seg_q - start) > 0.02 * travel)[0]
        onset = moving[0] if moving.size else 0
        rel_t = seg_t - seg_t[onset]
        rtd50 = float(np.max(np.abs(seg_tau[:max(1, int(0.05 / dt))])))
        rtd100 = float(np.max(np.abs(seg_tau[:max(1, int(0.10 / dt))])))
        # movement time 10-90 % of travel
        ten = np.where(sign * (seg_q - start) > 0.10 * travel)[0]
        ninety = np.where(sign * (seg_q - start) > 0.90 * travel)[0]
        move_time = float(seg_t[ninety[0]] - seg_t[ten[0]]) if ten.size and ninety.size else float("nan")
        overshoot = float(max(0.0, sign * (seg_q.max() - end)) / travel * 100.0)
        periods.append({
            "peak_velocity": float(np.max(sign * seg_v)),
            "peak_torque": float(np.max(np.abs(seg_tau))),
            "rtd50": rtd50, "rtd100": rtd100,
            "time_to_peak_torque_ms": float(rel_t[int(np.argmax(np.abs(seg_tau)))] * 1000.0),
            "movement_time_ms": move_time * 1000.0,
            "overshoot_pct": overshoot,
        })
    keys = periods[0].keys() if periods else []
    mean = {k: float(np.nanmean([p[k] for p in periods])) for k in keys}
    # Settling inside +/-5 % of the travel, and the steady-state error.
    settle, steady = [], []
    for k in range(burst.repeats):
        t0 = k * burst.period_s
        mask = (t >= t0 + burst.move_s) & (t < t0 + burst.move_s + HOLD_S)
        if not mask.any():
            continue
        inside = np.abs(q[mask] - end) < 0.05 * travel
        if inside.all():
            settle.append(0.0)
        elif inside.any():
            first = np.argmax(inside)
            settle.append(float(t[mask][first] - (t0 + burst.move_s)))
        else:
            settle.append(float("nan"))
        steady.append(float(np.degrees(np.mean(np.abs(q[mask] - end)))))
    mean["settling_s"] = float(np.nanmean(settle)) if settle else float("nan")
    mean["steady_error_deg"] = float(np.nanmean(steady)) if steady else float("nan")
    mean["condition"] = run.condition
    mean["repeats"] = len(periods)
    return mean
