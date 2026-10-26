#!/usr/bin/env python3
"""M3 check: the simulation sees the same slow, quantised, delayed signals the
firmware does.

Numbers come from two places only: the periods the shared library reports
(``Controller.site``) and the model assumptions in ``sim/timing.json``. This
script never declares a firmware period itself.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\check_timing.py
"""

from __future__ import annotations

import math
import statistics
import sys
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_ROOT))
from sim.controller import Controller, DEFAULT_LIB  # noqa: E402
from sim.io_model import (ActuatorModel, Command, CommandHold, ForceModel,  # noqa: E402
                          SensorModel, load_timing)

FAILURES: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    print(f"  [{'ok' if ok else 'FAIL'}] {name}{(' - ' + detail) if detail else ''}")
    if not ok:
        FAILURES.append(name)


def main() -> int:
    timing = load_timing()
    if not DEFAULT_LIB.exists():
        raise SystemExit(f"{DEFAULT_LIB} missing; run scripts/build_bridge.py first")
    controller = Controller(DEFAULT_LIB)
    site = controller.site
    print(f"timing.json: {SIM_ROOT / 'sim' / 'timing.json'}")
    print(f"site periods: shoulder read {site['shoulder_property_period_ms']} ms, "
          f"send {site['send_interval_ms']} ms, timeout {site['feedback_timeout_ms']} ms")

    step_s = timing["physics.step_s"]
    step_ms = step_s * 1000.0
    sensors = SensorModel(site["shoulder_property_period_ms"],
                          timing["sensor.elbow_counts_per_rev"],
                          quantize=bool(timing["sensor.elbow_quantize"]))
    hold = CommandHold(site["send_interval_ms"])
    actuator = ActuatorModel(site["shoulder_rotor_per_joint"],
                             site["elbow_motor_kt_nm_per_a"],
                             shoulder_lag_s=timing["actuator.shoulder_lag_s"],
                             elbow_lag_s=timing["actuator.elbow_lag_s"])
    force = ForceModel(timing["force.rate_hz"], timing["force.delay_ms"],
                       timing["force.noise_sigma_n"], seed=1)

    print("\n[1] shoulder angle is held for one property period")
    sensors.reset()
    values = []
    for index in range(1000):
        now = index * step_ms
        sample = sensors.sample(now, now / 1000.0, 1.0, 0.0, 0.0)
        values.append(sample.shoulder_rad)
    period = site["shoulder_property_period_ms"]
    check("20 updates per second", sensors.updates == int(1000 / period),
          f"{sensors.updates} updates in 1 s for a {period} ms period")
    held_ok = all(abs(value - math.floor(i / period) * period / 1000.0) < 1e-9
                  for i, value in enumerate(values))
    check("the held value stays flat between updates", held_ok)

    print("\n[2] command is refreshed at the send interval")
    hold.reset()
    applied = []
    for index in range(1000):
        now = index * step_ms
        applied.append(hold.apply(now, Command(now / 1000.0, 0.0)).shoulder_rotor_nm)
    check("20 command updates per second",
          hold.updates == int(1000 / site["send_interval_ms"]),
          f"{hold.updates} updates in 1 s")
    check("the actuator sees a staircase", hold.updates == int(1000 / site["send_interval_ms"]),
          f"{hold.updates} updates")

    print("\n[3] elbow feedback is quantised to the C620 tick")
    sensors.reset()
    counts = timing["sensor.elbow_counts_per_rev"]
    tick = 2.0 * math.pi / counts
    base = sensors.sample(0.0, 0.0, 0.0, 0.0, 0.0).elbow_rad
    steps = []
    for k in range(1, 6):
        value = sensors.sample(k * step_ms, 0.0, 0.0, k * tick * 0.3, 0.0).elbow_rad
        steps.append(round((value - base) / tick))
    check("elbow angle moves in whole ticks", steps == [0, 1, 1, 1, 2] or steps == [0, 0, 1, 1, 1],
          f"tick counts {steps}")

    print("\n[4] elbow actuator lag")
    actuator.reset()
    tau = timing["actuator.elbow_lag_s"]
    torque = 0.0
    for _ in range(int(round(tau / step_s))):
        _, torque = actuator.joint_torque(step_s, Command(0.0, 1.0))
    target = 1.0 * site["elbow_motor_kt_nm_per_a"]
    check("63% of the target after one time constant", abs(torque / target - 0.632) < 0.02,
          f"{torque / target * 100.0:.1f}% after {tau * 1000.0:.0f} ms")
    quick = ActuatorModel(site["shoulder_rotor_per_joint"],
                          site["elbow_motor_kt_nm_per_a"], enabled=False)
    shoulder_nm, elbow_nm = quick.joint_torque(step_s, Command(0.2, 1.0))
    check("lag off gives the exact mapping",
          abs(shoulder_nm - 0.2 * site["shoulder_rotor_per_joint"]) < 1e-6
          and abs(elbow_nm - 1.0 * site["elbow_motor_kt_nm_per_a"]) < 1e-6,
          f"{shoulder_nm:.3f} Nm joint, {elbow_nm:.4f} Nm")

    print("\n[5] force sampling: 10 SPS, one-period delay, noise")
    force.reset()
    samples = []
    step_ms_at = 200.0
    for index in range(5000):
        now = index * step_ms
        force.update(now, 1.0 if now >= step_ms_at else 0.0)
        if force.sample_ms == now:
            samples.append((now, force.value))
    check("50 samples in 5 s", len(samples) == 50, f"{len(samples)} samples")
    first = next((stamp for stamp, value in samples if value > 0.5), None)
    check("step appears one sample period later",
          first is not None and abs((first - step_ms_at) - timing["force.delay_ms"]) <= step_ms * 1.5,
          f"observed {0.0 if first is None else first - step_ms_at:.0f} ms "
          f"vs {timing['force.delay_ms']:.0f} ms assumed")
    noise = statistics.pstdev([value for _, value in samples[5:]])
    check("noise matches the configured sigma",
          abs(noise - timing["force.noise_sigma_n"]) < timing["force.noise_sigma_n"] * 0.5,
          f"sigma {noise * 1000.0:.3f} mN vs {timing['force.noise_sigma_n'] * 1000.0:.1f} mN")

    def run_loop(degrees_at) -> list[float]:
        controller.reset(0)
        sensors.reset()
        hold.reset()
        actuator.reset()
        torques = []
        for index in range(2000):
            now = index * step_ms
            sample = sensors.sample(now, math.radians(degrees_at(now)), 0.0, 0.0, 0.0)
            command = controller.step(shoulder_rad=sample.shoulder_rad,
                                      shoulder_vel=sample.shoulder_vel,
                                      elbow_rad=sample.elbow_rad,
                                      elbow_vel=sample.elbow_vel, now_ms=now)
            held = hold.apply(now, Command(command.shoulder_rotor_nm,
                                           command.elbow_current_a))
            shoulder_nm, _ = actuator.joint_torque(step_s, held)
            torques.append(shoulder_nm)
        return torques

    print("\n[6] full loop: DLL + held sensors + held command, 2 s")
    fixed = run_loop(lambda now: 60.0)
    expected_nm = site["shoulder_gravity_amplitude_nm"] * math.sin(math.radians(60.0))
    check("level matches the firmware shoulder model",
          abs(fixed[-1] - expected_nm) < 0.01,
          f"{fixed[-1]:.3f} Nm vs {expected_nm:.3f} Nm")

    moving = run_loop(lambda now: 60.0 + 20.0 * now / 2000.0)
    changes = sum(1 for a, b in zip(moving, moving[1:]) if abs(b - a) > 1e-6)
    expected = int(2000 / site["send_interval_ms"])
    check("joint torque follows in 20 Hz steps", abs(changes - expected) <= 1,
          f"{changes} steps in 2 s vs {expected}")

    print()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)} check(s): {', '.join(FAILURES)}")
        return 1
    print("all M3 timing checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
