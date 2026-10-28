"""Stage G: burst (explosive) movement definitions.

Two tasks modelled on orthopaedic actions:

  G1 mallet strike   elbow 100 -> 45 deg in 180 ms, hold 2 s, ten repeats
  G2 rapid lift      shoulder 5 -> 45 deg in 250 ms, hold 2 s, ten repeats

``Burst`` also carries a fixed-posture mode used for the static holds. The
muscle-driven runs (``sim/muscle_drive.py``) take the reference and the timing
constants from here.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import build_model as bm

# A person cannot step the torque: the 45 ms first-order lag stands for the
# muscle activation rise.
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
    # Static hold: when both are set the reference is this fixed pose and
    # start_deg/end_deg/joint are ignored.
    fixed_shoulder_deg: float | None = None
    fixed_elbow_deg: float | None = None

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
    if burst.fixed_shoulder_deg is not None and burst.fixed_elbow_deg is not None:
        return math.radians(burst.fixed_shoulder_deg), math.radians(burst.fixed_elbow_deg)
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
