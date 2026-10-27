"""M6: muscle activation as a second effort metric.

The dynamic runs use an ideal torque at the shoulder and elbow. This module
answers "how hard would the muscles have to work for that torque": it keeps
MyoArm's 63 muscles, reads their moment arms and active force at the recorded
pose, and solves the standard static optimisation

    minimise  sum(a_i^2)          (a = activation, 0..1)
    subject to  R (F_active * a) = tau_required - R * F_passive

at every recorded sample. Lower mean activation means the muscles are less
loaded, which is the usual second indicator next to joint torque.

Nothing here changes the dynamics; it is a post-processing metric.
"""

from __future__ import annotations

import numpy as np

import build_model as bm

EPS = 1e-4


class MuscleEffort:
    def __init__(self, params: dict | None = None) -> None:
        self.params = params or bm.load_params()
        self.model, _ = bm.build(self.params, write=False, keep_muscles=True)
        self.data = __import__("mujoco").MjData(self.model)
        self.muscles = [self.model.actuator(i).name for i in range(self.model.nu)]
        self.shoulder_dof = bm.joint_dof(self.model, bm.HUMAN_SHOULDER)
        self.elbow_dof = bm.joint_dof(self.model, bm.HUMAN_ELBOW)
        self.shoulder_q = bm.joint_qpos(self.model, bm.HUMAN_SHOULDER)
        self.elbow_q = bm.joint_qpos(self.model, bm.HUMAN_ELBOW)

    def _lengths(self) -> np.ndarray:
        mujoco = __import__("mujoco")
        mujoco.mj_forward(self.model, self.data)
        return self.data.actuator_length.copy()

    def _tension(self, activation: float) -> np.ndarray:
        """Muscle tension (N, >= 0).

        MuJoCo reports a muscle's actuator force as negative along the muscle
        length direction; the physical tension is its magnitude.
        """
        mujoco = __import__("mujoco")
        self.data.act[:] = activation
        mujoco.mj_forward(self.model, self.data)
        return np.abs(self.data.actuator_force.copy())

    def _set_pose(self, q_sh: float, q_el: float) -> None:
        self.data.qpos[:] = 0.0
        self.data.qvel[:] = 0.0
        self.data.qpos[self.shoulder_q] = q_sh
        self.data.qpos[self.elbow_q] = q_el

    def activations(self, q_sh: float, q_el: float, tau_sh: float,
                    tau_el: float) -> tuple[np.ndarray, float]:
        """Activation vector (63,) and the constraint residual in N*m."""
        self._set_pose(q_sh, q_el)
        self._lengths()
        passive = self._tension(0.0)
        active = np.maximum(0.0, self._tension(1.0) - passive)

        # Moment arms by central differences of the muscle lengths.
        moment = np.zeros((len(self.muscles), 2))
        for column, (adr, sign) in enumerate(((self.shoulder_q, 1.0), (self.elbow_q, 1.0))):
            self._set_pose(q_sh, q_el)
            self.data.qpos[adr] += EPS
            length_plus = self._lengths()
            self.data.qpos[adr] -= 2 * EPS
            length_minus = self._lengths()
            moment[:, column] = -sign * (length_plus - length_minus) / (2 * EPS)
        self._set_pose(q_sh, q_el)

        # A = R * diag(active tension): the torque one unit of activation gives.
        a_matrix = moment.T * active[None, :]
        passive_torque = moment.T @ passive
        wanted = np.array([tau_sh, tau_el])

        # min ||a||^2 s.t. A a = wanted - passive, 0 <= a <= 1 (active set).
        a = np.zeros(len(self.muscles))
        free = np.ones(len(self.muscles), bool)
        target = wanted - passive_torque
        for _ in range(10):
            if not free.any():
                break
            solution, *_ = np.linalg.lstsq(a_matrix[:, free], target, rcond=None)
            high = solution > 1.0
            low = solution < 0.0
            index = np.where(free)[0]
            if not high.any() and not low.any():
                a[index] = solution
                break
            a[index[high]] = 1.0
            a[index[low]] = 0.0
            free[index[high]] = False
            free[index[low]] = False
            target = wanted - passive_torque - a_matrix[:, ~free] @ a[~free]
        a = np.clip(a, 0.0, 1.0)
        residual = float(np.linalg.norm(a_matrix @ a + passive_torque - wanted))
        return a, residual

    def summary(self, run) -> dict:
        """Mean/peak activation over one run, split by which joint a muscle drives."""
        arrays = run.arrays()
        rows = []
        for k in range(len(arrays["time"])):
            a, residual = self.activations(float(arrays["q_sh"][k]), float(arrays["q_el"][k]),
                                           float(arrays["human_sh_nm"][k]),
                                           float(arrays["human_el_nm"][k]))
            rows.append((a, residual))
        if not rows:
            return {}
        acts = np.array([r[0] for r in rows])
        residuals = np.array([r[1] for r in rows])
        # Muscle groups: which joint each muscle acts on most at the first sample.
        self._set_pose(float(arrays["q_sh"][0]), float(arrays["q_el"][0]))
        moment = np.zeros((len(self.muscles), 2))
        for column, adr in enumerate((self.shoulder_q, self.elbow_q)):
            self.data.qpos[adr] += EPS
            plus = self._lengths()
            self.data.qpos[adr] -= 2 * EPS
            minus = self._lengths()
            moment[:, column] = -(plus - minus) / (2 * EPS)
            self.data.qpos[adr] += EPS
        shoulder_group = np.abs(moment[:, 0]) >= np.abs(moment[:, 1])
        return {
            # Mean over all 63 muscles is diluted; the sum and the peak are the
            # informative numbers, plus the mean over the loaded muscles.
            "mean_activation": float(acts.mean()),
            "sum_activation": float(acts.sum(axis=1).mean()),
            "peak_activation": float(acts.max()),
            "mean_activation_loaded": float(acts[acts > 0.01].mean()) if (acts > 0.01).any() else 0.0,
            "loaded_muscles": float((acts > 0.01).sum(axis=1).mean()),
            "mean_activation_shoulder_muscles": float(acts[:, shoulder_group].mean()),
            "mean_activation_elbow_muscles": float(acts[:, ~shoulder_group].mean()),
            "max_constraint_residual_nm": float(residuals.max()),
            "muscles": int(len(self.muscles)),
        }
