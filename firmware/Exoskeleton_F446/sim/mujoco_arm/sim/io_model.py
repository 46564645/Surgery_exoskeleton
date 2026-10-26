"""M3 sensor, command-hold, actuator and force models.

The simulation steps physics at 1 ms, but the firmware does not see the world
at 1 ms: the shoulder angle arrives every 50 ms (property read) and is held in
between, the command is refreshed every 50 ms (send interval), the elbow
feedback is a 8192-count/rev value, and the HX711 gives one sample every
100 ms. Without these the assist looks better than it is.

Every period that the firmware already exposes is read from the shared library
(``Controller.site``); this module never re-declares those numbers. Only the
model assumptions (lag, force delay/noise) come from ``timing.json``.
"""

from __future__ import annotations

import json
import math
import random
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
TIMING_PATH = SIM_ROOT / "sim" / "timing.json"


def load_timing(path: Path = TIMING_PATH) -> dict:
    """Flatten timing.json to {section.leaf: value}."""
    with path.open(encoding="utf-8") as handle:
        raw = json.load(handle)
    flat: dict[str, float] = {}
    for section, entries in raw.items():
        if section.startswith("_") or not isinstance(entries, dict):
            continue
        for name, entry in entries.items():
            flat[f"{section}.{name}"] = entry["value"]
    return flat


@dataclass
class SensorSample:
    shoulder_rad: float
    shoulder_vel: float
    elbow_rad: float
    elbow_vel: float
    shoulder_age_ms: float


class SensorModel:
    """Shoulder property read at its own period, elbow every control cycle."""

    def __init__(self, shoulder_period_ms: int, elbow_counts_per_rev: float,
                 quantize: bool = True, enabled: bool = True) -> None:
        self.shoulder_period_ms = int(shoulder_period_ms)
        self.elbow_counts_per_rev = float(elbow_counts_per_rev)
        self.quantize = quantize
        self.enabled = enabled
        self.reset()

    def reset(self) -> None:
        self._next_shoulder_ms = 0
        self._shoulder_rad = 0.0
        self._shoulder_vel = 0.0
        self._shoulder_ms = 0
        self.updates = 0

    def _quantize(self, rad: float) -> float:
        if not self.quantize:
            return rad
        counts = round(rad * self.elbow_counts_per_rev / (2.0 * math.pi))
        return counts * 2.0 * math.pi / self.elbow_counts_per_rev

    def sample(self, now_ms: float, shoulder_rad: float, shoulder_vel: float,
               elbow_rad: float, elbow_vel: float) -> SensorSample:
        if not self.enabled:
            return SensorSample(shoulder_rad, shoulder_vel, elbow_rad, elbow_vel, 0.0)
        if now_ms >= self._next_shoulder_ms:
            self._shoulder_rad = shoulder_rad
            self._shoulder_vel = shoulder_vel
            self._shoulder_ms = now_ms
            self.updates += 1
            # Keep the phase: catch up in whole periods, never drift.
            while self._next_shoulder_ms <= now_ms:
                self._next_shoulder_ms += self.shoulder_period_ms
        return SensorSample(
            shoulder_rad=self._shoulder_rad,
            shoulder_vel=self._shoulder_vel,
            elbow_rad=self._quantize(elbow_rad),
            elbow_vel=elbow_vel,
            shoulder_age_ms=now_ms - self._shoulder_ms,
        )


@dataclass
class Command:
    shoulder_rotor_nm: float
    elbow_current_a: float


class CommandHold:
    """Zero-order hold at the firmware send interval."""

    def __init__(self, period_ms: int, enabled: bool = True) -> None:
        self.period_ms = int(period_ms)
        self.enabled = enabled
        self.reset()

    def reset(self) -> None:
        self._next_ms = 0
        self.held = Command(0.0, 0.0)
        self.updates = 0

    def apply(self, now_ms: float, command: Command) -> Command:
        if not self.enabled:
            self.held = command
            return command
        if now_ms >= self._next_ms:
            self.held = command
            self.updates += 1
            while self._next_ms <= now_ms:
                self._next_ms += self.period_ms
        return self.held


@dataclass
class ActuatorModel:
    """Command units to joint torque, with an optional first-order lag."""

    shoulder_rotor_per_joint: float
    elbow_motor_kt_nm_per_a: float
    shoulder_lag_s: float = 0.0
    elbow_lag_s: float = 0.0
    enabled: bool = True
    _shoulder: float = field(default=0.0, init=False)
    _elbow: float = field(default=0.0, init=False)

    def reset(self) -> None:
        self._shoulder = 0.0
        self._elbow = 0.0

    def _lag(self, current: float, target: float, dt_s: float, tau_s: float) -> float:
        if tau_s <= 0.0:
            return target
        alpha = 1.0 - math.exp(-dt_s / tau_s)
        return current + (target - current) * alpha

    def joint_torque(self, dt_s: float, command: Command) -> tuple[float, float]:
        """(shoulder joint Nm, elbow joint Nm)."""
        shoulder_target = command.shoulder_rotor_nm * self.shoulder_rotor_per_joint
        elbow_target = command.elbow_current_a * self.elbow_motor_kt_nm_per_a
        if not self.enabled:
            self._shoulder, self._elbow = shoulder_target, elbow_target
            return shoulder_target, elbow_target
        self._shoulder = self._lag(self._shoulder, shoulder_target, dt_s, self.shoulder_lag_s)
        self._elbow = self._lag(self._elbow, elbow_target, dt_s, self.elbow_lag_s)
        return self._shoulder, self._elbow


class ForceModel:
    """HX711 at 10 SPS with a fixed delay and gaussian noise."""

    def __init__(self, rate_hz: float, delay_ms: float, sigma_n: float,
                 enabled: bool = True, seed: int | None = None) -> None:
        self.period_ms = 1000.0 / float(rate_hz)
        self.delay_ms = float(delay_ms)
        self.sigma_n = float(sigma_n)
        self.enabled = enabled
        self._random = random.Random(seed)
        self.reset()

    def reset(self) -> None:
        self._next_ms = 0.0
        self._history: deque[tuple[float, float]] = deque()
        self._samples: deque[tuple[float, float]] = deque()
        self.value = 0.0
        self.sample_ms = -1e9
        self.count = 0

    def update(self, now_ms: float, true_force_n: float) -> None:
        """Call every physics step; new samples appear at 10 SPS."""
        if not self.enabled:
            self.value = true_force_n
            self.sample_ms = now_ms
            return
        self._history.append((now_ms, true_force_n))
        while self._history and self._history[0][0] < now_ms - self.delay_ms - 200.0:
            self._history.popleft()
        if now_ms < self._next_ms:
            return
        # The sample reflects the force delay_ms ago.
        delayed = true_force_n
        for stamp, force in reversed(self._history):
            if stamp <= now_ms - self.delay_ms:
                delayed = force
                break
        self.value = delayed + self._random.gauss(0.0, self.sigma_n)
        self.sample_ms = now_ms
        self.count += 1
        while self._next_ms <= now_ms:
            self._next_ms += self.period_ms

    def fresh(self, now_ms: float, freshness_ms: float) -> bool:
        return (now_ms - self.sample_ms) <= freshness_ms
