"""Reference motions for the human arm (M4).

A task is a list of waypoints ``(time_s, shoulder_deg, elbow_deg)``; between
two waypoints the joints follow a minimum-jerk (smoothstep) profile, so the
reference has no velocity steps. Angles use the firmware convention: 0 is the
hanging arm, positive is flexion.

The default motions keep the elbow flexed: with the 0.90 m table a straight
arm meets the table front edge at about 6.6 deg (see ``lift_contact.py``), and
the user decided to keep that interference rather than move the table.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class Task:
    name: str
    waypoints: tuple[tuple[float, float, float], ...]   # (time_s, shoulder_deg, elbow_deg)
    description: str

    @property
    def duration_s(self) -> float:
        return self.waypoints[-1][0]

    def sample(self, t: float) -> tuple[float, float, float, float]:
        """(shoulder, elbow) rad and rad/s at time t."""
        wps = self.waypoints
        if t <= wps[0][0]:
            return math.radians(wps[0][1]), math.radians(wps[0][2]), 0.0, 0.0
        for (t0, s0, e0), (t1, s1, e1) in zip(wps, wps[1:]):
            if t <= t1:
                span = t1 - t0
                if span <= 0.0:
                    return math.radians(s1), math.radians(e1), 0.0, 0.0
                u = (t - t0) / span
                # minimum jerk: 10u^3 - 15u^4 + 6u^5
                f = 10 * u ** 3 - 15 * u ** 4 + 6 * u ** 5
                df = (30 * u ** 2 - 60 * u ** 3 + 30 * u ** 4) / span
                return (math.radians(s0 + (s1 - s0) * f),
                        math.radians(e0 + (e1 - e0) * f),
                        math.radians(s1 - s0) * df,
                        math.radians(e1 - e0) * df)
        return math.radians(wps[-1][1]), math.radians(wps[-1][2]), 0.0, 0.0


def _holds(points: tuple[tuple[float, float], ...], hold_s: float,
           move_s: float) -> tuple[tuple[float, float, float], ...]:
    """Waypoints that hold each (shoulder, elbow) pose for hold_s."""
    out, t = [], 0.0
    for k, (sh, el) in enumerate(points):
        if k:
            t += move_s
        out.append((t, sh, el))
        t += hold_s
        out.append((t, sh, el))
    return tuple(out)


def _cycles(low_deg: float, high_deg: float, elbow_deg: float, period_s: float,
            cycles: int) -> tuple[tuple[float, float, float], ...]:
    out, t = [], 0.0
    out.append((t, low_deg, elbow_deg))
    for _ in range(cycles):
        t += period_s / 2
        out.append((t, high_deg, elbow_deg))
        t += period_s / 2
        out.append((t, low_deg, elbow_deg))
    return tuple(out)


# The lower end is 5 deg, not 0: MyoArm's shoulder range starts at 0, so a
# hanging arm would rest on the joint limit and the limit, not the muscles,
# would carry the load.
LOW_DEG = 5.0

TASKS = {
    "T1": Task("T1", _holds(((LOW_DEG, 90.0), (25.0, 90.0), (45.0, 90.0)), 6.0, 3.0),
               "static holds at shoulder 5/25/45 deg, elbow 90 deg"),
    "T2slow": Task("T2slow", _cycles(LOW_DEG, 45.0, 90.0, 4.0, 3),
                   "lift/lower shoulder 5-45 deg, elbow 90 deg, 4 s cycle"),
    "T2fast": Task("T2fast", _cycles(LOW_DEG, 45.0, 90.0, 2.0, 5),
                   "lift/lower shoulder 5-45 deg, elbow 90 deg, 2 s cycle"),
    "T3": Task("T3", _holds(((10.0, 70.0), (30.0, 80.0), (50.0, 60.0),
                             (25.0, 100.0), (45.0, 70.0)), 3.0, 2.5),
               "free repositioning through five flexed poses"),
}


def get(name: str) -> Task:
    try:
        return TASKS[name]
    except KeyError:
        raise SystemExit(f"unknown task {name}; choose from {', '.join(TASKS)}") from None
