"""Offline prototype for the interaction-force trigger.

Fits the passive force model F0(q) = c0 + c1*sin(q_sh) + c2*sin(q_sh+q_el) from
a calibration session (arm relaxed, assist on, slow sweep), then replays the
same hysteresis state machine as App/control/force_assist.c on another session
and reports triggers, durations and the coefficients to paste into s5_site.c.

The replay mirrors the firmware thresholds exactly; it never talks to hardware.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path

import numpy as np

DEFAULTS = {"on": 0.5, "off": 0.25, "boost": 50.0, "ramp_ms": 200, "hold_ms": 3000}


def read_csv(path):
    if not path.is_file():
        return []
    with path.open(encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def number(row, key):
    value = row.get(key, "")
    if value in ("", None, "na"):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def pairs(directory):
    """(t, q_sh_rad, q_el_rad, force_N) from one live-monitor session."""
    samples = [r for r in read_csv(directory / "samples.csv") if r.get("phase") == "4"]
    force = read_csv(directory / "force.csv")
    if not samples or not force:
        return []
    times = [float(r["t_s"]) for r in samples]
    out, index = [], 0
    for row in force:
        t = number(row, "t_s")
        value = number(row, "force_N")
        if t is None or value is None:
            continue
        while index + 1 < len(times) and abs(times[index + 1] - t) <= abs(times[index] - t):
            index += 1
        if abs(times[index] - t) > 0.15:
            continue
        sample = samples[index]
        q_sh = number(sample, "q_sh_deg")
        q_el = number(sample, "q_el_deg")
        if q_sh is None or q_el is None:
            continue
        out.append((t, math.radians(q_sh), math.radians(q_el), value))
    return out


def fit(rows):
    design = np.array([[1.0, math.sin(q_sh), math.sin(q_sh + q_el)]
                       for _, q_sh, q_el, _ in rows])
    force = np.array([f for *_, f in rows])
    coef, *_ = np.linalg.lstsq(design, force, rcond=None)
    residual = force - design @ coef
    return coef, float(np.sqrt((residual ** 2).mean())), force


def replay(rows, coef, config):
    """Same state machine as the firmware: hysteresis, frozen reference, hold limit."""
    active = False
    ref = 0.0
    gain = 1.0
    entered = 0.0
    last_ms = rows[0][0] * 1000.0 if rows else 0.0
    triggers, spans, hold_start = [], [], None
    for t, q_sh, q_el, force in rows:
        now = t * 1000.0
        expected = coef[0] + coef[1] * math.sin(q_sh) + coef[2] * math.sin(q_sh + q_el)
        error = force - expected
        if not active:
            if abs(error) > config["on"]:
                active, ref, entered = True, expected, now
                triggers.append({"t_s": round(t, 2), "e_N": round(error, 2)})
                hold_start = t
        else:
            expired = (now - entered) >= config["hold_ms"]
            if expired or abs(force - ref) < config["off"]:
                active = False
                spans.append({"start_s": round(hold_start, 2), "end_s": round(t, 2)})
        target = 1.0 + config["boost"] / 100.0 if active else 1.0
        span = config["boost"] / 100.0
        step = span * (now - last_ms) / config["ramp_ms"]
        gain = min(target, gain + step) if gain < target else max(target, gain - step)
        last_ms = now
    if active and hold_start is not None:
        spans.append({"start_s": round(hold_start, 2), "end_s": round(rows[-1][0], 2)})
    return {"triggers": triggers, "spans": spans}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("calib", type=Path, help="校准会话：手臂放松、助力开、缓慢遍历")
    parser.add_argument("--validate", type=Path, help="验证会话；省略时回放校准会话本身")
    parser.add_argument("--on", type=float, default=DEFAULTS["on"], help="进入阈值 N")
    parser.add_argument("--off", type=float, default=DEFAULTS["off"], help="退出阈值 N")
    parser.add_argument("--boost", type=float, default=DEFAULTS["boost"], help="增量百分比")
    parser.add_argument("--out", type=Path, help="输出目录，默认 <calib>/force_sim")
    parser.add_argument("--json", action="store_true", help="只输出 JSON")
    args = parser.parse_args()

    calibration = pairs(args.calib)
    if len(calibration) < 20:
        raise SystemExit("校准会话缺少 RUNNING 样本或力数据")
    coef, rms, _ = fit(calibration)
    config = {"on": args.on, "off": args.off, "boost": args.boost,
              "ramp_ms": DEFAULTS["ramp_ms"], "hold_ms": DEFAULTS["hold_ms"]}
    validation = pairs(args.validate) if args.validate else calibration
    result = replay(validation, coef, config)
    duration = (validation[-1][0] - validation[0][0]) if validation else 0.0
    summary = {
        "calibration": str(args.calib),
        "validate": str(args.validate) if args.validate else None,
        "samples_calib": len(calibration),
        "samples_validate": len(validation),
        "coefficients": {"c0": round(float(coef[0]), 4), "c1": round(float(coef[1]), 4),
                         "c2": round(float(coef[2]), 4)},
        "residual_rms_N": round(rms, 3),
        "config": config,
        "duration_s": round(duration, 1),
        "triggers": result["triggers"],
        "spans": result["spans"],
        "triggers_per_min": round(len(result["triggers"]) * 60.0 / duration, 1) if duration > 0 else None,
    }
    outdir = args.out or (args.calib / "force_sim")
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "force_sim.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    if args.json:
        print(json.dumps(summary, ensure_ascii=False))
        return 0
    print(json.dumps(summary["coefficients"], ensure_ascii=False))
    print(f"residual RMS {summary['residual_rms_N']} N, "
          f"{len(result['triggers'])} triggers over {summary['duration_s']} s "
          f"({summary['triggers_per_min']}/min)")
    for span in result["spans"][:10]:
        print(f"  boost {span['start_s']:6.2f} .. {span['end_s']:6.2f} s")
    print(f"summary: {outdir / 'force_sim.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
