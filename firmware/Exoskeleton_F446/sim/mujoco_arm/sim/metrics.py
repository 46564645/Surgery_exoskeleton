"""Effort metrics for one run (M4).

The human effort is the torque its own actuators apply; lower is better.
Everything is reported per joint over the measured window, and for lift tasks
also split into raising and lowering halves, because the literature shows the
two halves respond differently to assistance.
"""

from __future__ import annotations

import numpy as np

from sim.runner import Run


def _stats(x: np.ndarray, dt: float) -> dict[str, float]:
    if x.size == 0:
        return {"rms": 0.0, "peak": 0.0, "abs_integral": 0.0}
    integral = float(np.sum(np.abs(x)) * dt)   # N*m*s, rectangle rule on samples
    return {"rms": float(np.sqrt(np.mean(x ** 2))),
            "peak": float(np.max(np.abs(x))),
            "abs_integral": integral}


def summarize(run: Run) -> dict:
    a = run.arrays()
    t = a["time"]
    dt = float(np.median(np.diff(t))) if t.size > 1 else 0.0
    window = a["human_sh_nm"] * 0 + 1.0
    out = {
        "condition": run.condition,
        "task": run.task,
        "duration_s": float(t[-1] - t[0]) if t.size else 0.0,
        "shoulder": _stats(a["human_sh_nm"], dt),
        "elbow": _stats(a["human_el_nm"], dt),
        "tracking_rms_deg": {
            "shoulder": float(np.degrees(np.sqrt(np.mean((a["ref_sh"] - a["q_sh"]) ** 2)))),
            "elbow": float(np.degrees(np.sqrt(np.mean((a["ref_el"] - a["q_el"]) ** 2)))),
        },
        "exo": {"shoulder": _stats(a["exo_sh_nm"], dt), "elbow": _stats(a["exo_el_nm"], dt)},
        "strap_n": {"upper_rms": _stats(a["strap_upper_n"], dt)["rms"],
                    "forearm_rms": _stats(a["strap_forearm_n"], dt)["rms"]},
        "table_contact_steps": int(run.table_contacts),
        "notes": run.notes,
    }
    # Total effort = sum of |torque| integral over both joints, normalised by
    # time, which is what "less effort" means for a multi-joint task.
    out["effort_nm_mean"] = (out["shoulder"]["abs_integral"] + out["elbow"]["abs_integral"]) \
        / max(out["duration_s"], 1e-9)

    # Raising vs lowering, from the reference shoulder velocity sign.
    v = np.gradient(a["ref_sh"], t) if t.size > 2 else np.zeros_like(t)
    for label, mask in (("raising", v > 1e-3), ("lowering", v < -1e-3)):
        if mask.any():
            out[f"shoulder_{label}"] = _stats(a["human_sh_nm"][mask], dt)
            out[f"elbow_{label}"] = _stats(a["human_el_nm"][mask], dt)
    return out


def compare(summaries: list[dict], baseline: str = "C1") -> dict:
    """Effort reduction of every condition against the baseline (C1 by default)."""
    base = next((s for s in summaries if s["condition"] == baseline), None)
    if base is None:
        return {}
    out = {"baseline": baseline, "reduction": {}}
    for s in summaries:
        out["reduction"][s["condition"]] = {
            "effort_nm_mean_pct": 100.0 * (1.0 - s["effort_nm_mean"] / base["effort_nm_mean"])
            if base["effort_nm_mean"] else 0.0,
            "shoulder_rms_pct": 100.0 * (1.0 - s["shoulder"]["rms"] / base["shoulder"]["rms"])
            if base["shoulder"]["rms"] else 0.0,
            "elbow_rms_pct": 100.0 * (1.0 - s["elbow"]["rms"] / base["elbow"]["rms"])
            if base["elbow"]["rms"] else 0.0,
        }
    return out
