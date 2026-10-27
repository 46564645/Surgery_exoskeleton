#!/usr/bin/env python3
"""M2b check: the Python side calls the firmware control code and the numbers
match the hand calculation of the same formulas.

Nothing here is a hardware result. The expected values are computed from the
site numbers the library itself reports (shoulder amplitude, gain, 8:1, kt,
caps), so a mismatch means the bridge or the site wiring is wrong.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\test_bridge.py
    ... --sweep      # also rebuild with gain 0.6 and check the C3 sweep path
"""

from __future__ import annotations

import argparse
import math
import subprocess
import sys
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_ROOT))
from sim.controller import Controller, DEFAULT_LIB  # noqa: E402

FAILURES: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    print(f"  [{'ok' if ok else 'FAIL'}] {name}{(' - ' + detail) if detail else ''}")
    if not ok:
        FAILURES.append(name)


def near(actual: float, expected: float, tol: float = 1e-4) -> bool:
    return abs(actual - expected) <= tol


def expected_command(site: dict, q_sh: float, q_el: float) -> tuple[float, float]:
    """Hand calculation of the firmware composition with the reported numbers."""
    amplitude = site["shoulder_gravity_amplitude_nm"]
    gain = site["shoulder_gain"]
    rotor = amplitude * math.sin(q_sh) * gain / site["shoulder_rotor_per_joint"]
    rotor = max(-site["shoulder_cap_rotor_nm"], min(site["shoulder_cap_rotor_nm"], rotor))
    elbow_nm = site["elbow_gravity_amplitude_nm"] * max(0.0, math.sin(q_sh + q_el))
    current = elbow_nm / site["elbow_motor_kt_nm_per_a"]
    current = max(-site["elbow_cap_a"], min(site["elbow_cap_a"], current))
    return rotor, current


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sweep", action="store_true",
                        help="also build gain 0.6 and check the sweep path")
    parser.add_argument("--lib", default=str(DEFAULT_LIB))
    args = parser.parse_args()

    controller = Controller(args.lib)
    site = controller.site
    print(f"library {args.lib}")
    print(f"  dual={site['dual']} gain={site['shoulder_gain']} "
          f"shoulder {site['shoulder_gravity_amplitude_nm']:.3f} Nm joint, "
          f"rotor 1:{site['shoulder_rotor_per_joint']:.0f}")
    print(f"  elbow {site['elbow_gravity_amplitude_nm']:.4f} Nm joint, "
          f"kt {site['elbow_motor_kt_nm_per_a']:.3f} Nm/A, "
          f"caps {site['shoulder_cap_rotor_nm']:.3f} Nm rotor / {site['elbow_cap_a']:.1f} A")
    print(f"  periods send={site['send_interval_ms']} ms session={site['session_ms']} ms "
          f"timeout={site['feedback_timeout_ms']} ms")

    print("\n[1] site numbers match the firmware profile")
    check("shoulder rotor mapping 8:1", near(site["shoulder_rotor_per_joint"], 8.0))
    check("elbow mapping 1:1", near(site["elbow_rotor_per_joint"], 1.0))
    check("elbow torque constant 0.02 Nm/A", near(site["elbow_motor_kt_nm_per_a"], 0.02))
    check("shoulder model 1.64 Nm joint", near(site["shoulder_gravity_amplitude_nm"], 1.64))
    # The site owns the period; it moved from 50 to 10 ms on 2026-10-27. Only
    # check that the value is inside the gateway's limits.
    check("send interval inside the gateway limits",
          0 < site["send_interval_ms"] < site["feedback_timeout_ms"],
          f"{site['send_interval_ms']} ms")
    check("feedback timeout 250 ms", site["feedback_timeout_ms"] == 250)
    check("dual axis build", site["dual"])

    print("\n[2] command matches the firmware formula at several poses")
    for deg in (0.0, 30.0, 60.0, 90.0):
        q = math.radians(deg)
        command = controller.step(shoulder_rad=q, shoulder_vel=0.0, elbow_rad=0.0,
                                  elbow_vel=0.0, now_ms=0)
        rotor, current = expected_command(site, q, 0.0)
        ok = command.ok and near(command.shoulder_rotor_nm, rotor, 1e-5) \
            and near(command.elbow_current_a, current, 1e-5)
        print(f"    shoulder {deg:5.1f} deg: rotor {command.shoulder_rotor_nm:+.4f} Nm "
              f"(expected {rotor:+.4f}), elbow {command.elbow_current_a:+.4f} A "
              f"(expected {current:+.4f})")
        check(f"pose {deg:.0f} deg matches", ok)

    print("\n[3] elbow angle drives only the elbow term")
    q_el = math.radians(90.0)
    command = controller.step(shoulder_rad=0.0, shoulder_vel=0.0, elbow_rad=q_el,
                              elbow_vel=0.0, now_ms=0)
    rotor, current = expected_command(site, 0.0, q_el)
    check("shoulder stays at zero", near(command.shoulder_rotor_nm, 0.0, 1e-6),
          f"{command.shoulder_rotor_nm:+.4f} Nm")
    check("elbow reaches the peak current", near(command.elbow_current_a, current, 1e-5),
          f"{command.elbow_current_a:+.4f} A")

    print("\n[4] assist gate and invalid feedback")
    command = controller.step(shoulder_rad=math.radians(60.0), shoulder_vel=0.0,
                              elbow_rad=0.0, elbow_vel=0.0, now_ms=0,
                              assist_enabled=False)
    check("assist off commands zero", command.ok
          and command.shoulder_rotor_nm == 0.0 and command.elbow_current_a == 0.0)
    command = controller.step(shoulder_rad=math.radians(60.0), shoulder_vel=0.0,
                              elbow_rad=0.0, elbow_vel=0.0, now_ms=0,
                              shoulder_valid=False)
    check("stale shoulder feedback is rejected", not command.ok
          and command.shoulder_rotor_nm == 0.0 and command.elbow_current_a == 0.0)

    print("\n[5] force trigger is off in the default build")
    check("force assist unconfirmed", not site["force_confirmed"])
    with_force = controller.step(shoulder_rad=math.radians(60.0), shoulder_vel=0.0,
                                 elbow_rad=0.0, elbow_vel=0.0, now_ms=0,
                                 force_n=5.0, force_valid=True)
    without = controller.step(shoulder_rad=math.radians(60.0), shoulder_vel=0.0,
                              elbow_rad=0.0, elbow_vel=0.0, now_ms=0,
                              force_n=0.0, force_valid=False)
    check("a force sample changes nothing while the feature is off",
          near(with_force.shoulder_rotor_nm, without.shoulder_rotor_nm, 1e-6))

    if args.sweep:
        print("\n[6] C3 sweep path: rebuild with gain 0.6")
        swept = Path(args.lib).with_name("sim_bridge_g06" + Path(args.lib).suffix)
        subprocess.run([sys.executable, str(SIM_ROOT / "scripts" / "build_bridge.py"),
                        "--gain", "0.6", "--out", str(swept)], check=True)
        low = Controller(swept)
        q = math.radians(30.0)
        high_cmd = controller.step(shoulder_rad=q, shoulder_vel=0.0, elbow_rad=0.0,
                                   elbow_vel=0.0, now_ms=0)
        low_cmd = low.step(shoulder_rad=q, shoulder_vel=0.0, elbow_rad=0.0,
                           elbow_vel=0.0, now_ms=0)
        ratio = low_cmd.shoulder_rotor_nm / high_cmd.shoulder_rotor_nm
        check("gain 0.6 scales the shoulder command by 0.6", near(ratio, 0.6, 1e-4),
              f"ratio {ratio:.4f}")

    print()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)} check(s): {', '.join(FAILURES)}")
        return 1
    print("all M2b bridge checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
