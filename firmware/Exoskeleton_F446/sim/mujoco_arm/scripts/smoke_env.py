#!/usr/bin/env python3
"""M0 smoke check: import MuJoCo, step a pendulum, optionally open the viewer.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\smoke_env.py [--viewer SECONDS]
"""
import argparse
import math
import time

import mujoco
import mujoco.viewer
import numpy as np

PENDULUM_XML = """
<mujoco model="smoke_pendulum">
  <option timestep="0.001" gravity="0 0 -9.81"/>
  <worldbody>
    <light pos="0 0 2"/>
    <geom type="plane" size="1 1 0.1" rgba=".8 .8 .8 1"/>
    <body name="link" pos="0 0 1">
      <joint name="hinge" type="hinge" axis="0 1 0" damping="0.01"/>
      <geom type="capsule" fromto="0 0 0 0 0 -0.3" size="0.02" mass="1"/>
    </body>
  </worldbody>
</mujoco>
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--viewer", type=float, default=0.0,
                        help="open the passive viewer for this many seconds (0 = headless)")
    args = parser.parse_args()

    model = mujoco.MjModel.from_xml_string(PENDULUM_XML)
    data = mujoco.MjData(model)
    data.qpos[0] = math.radians(30.0)
    mujoco.mj_forward(model, data)

    # Static check: holding torque at 30 deg must equal m*g*l*sin(q), l = 0.15 m.
    data.qvel[:] = 0.0
    data.qacc[:] = 0.0
    mujoco.mj_inverse(model, data)
    expected = 1.0 * 9.81 * 0.15 * math.sin(math.radians(30.0))
    print(f"mujoco {mujoco.__version__}, numpy {np.__version__}")
    print(f"hold torque at 30 deg: mj_inverse {data.qfrc_inverse[0]:+.4f} N*m, "
          f"m*g*l*sin(q) {expected:.4f} N*m")

    mujoco.mj_resetData(model, data)
    data.qpos[0] = math.radians(30.0)
    for _ in range(1000):
        mujoco.mj_step(model, data)
    print(f"after 1000 steps (1.0 s): q = {math.degrees(data.qpos[0]):+.2f} deg, t = {data.time:.3f} s")

    if args.viewer > 0.0:
        with mujoco.viewer.launch_passive(model, data) as viewer:
            end = time.monotonic() + args.viewer
            while viewer.is_running() and time.monotonic() < end:
                mujoco.mj_step(model, data)
                viewer.sync()
                time.sleep(model.opt.timestep)
        print("viewer opened and closed")


if __name__ == "__main__":
    main()
