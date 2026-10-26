#!/usr/bin/env python3
"""Render the M1 scene, headless or in the interactive viewer.

The numeric checks in check_static.py say the model is right; this script is
for looking at it. It rebuilds the MJCF from params.json, sets a pose, and
writes PNG views from a few angles. ``--viewer`` opens the real MuJoCo window
(needs a working OpenGL driver; on 2026-10-26 the two GPUs recovered from
Code 43 and both paths work).

The exoskeleton is coaxial with the human arm and thinner, so the arm hides it
in a normal view; ``--hide human`` makes the arm transparent for a look at the
exoskeleton alone.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\plot_scene.py
    ... --pose 60,30 --hide human --out build\\sim\\scene
    ... --viewer 10
"""

from __future__ import annotations

import argparse
import math
import sys
import time
from pathlib import Path

import mujoco
import numpy as np
from PIL import Image

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT / "model"))
import build_model  # noqa: E402

OUT_DIR = FW_ROOT / "build" / "sim" / "scene"
HUMAN_GEOMS = ("human_upper_geom", "human_fore_geom", "human_hand_geom", "trunk_geom")
EXO_GEOMS = ("exo_upper_geom", "exo_motor_geom", "exo_fore_geom")


def set_pose(model: mujoco.MjModel, data: mujoco.MjData,
             shoulder_deg: float, elbow_deg: float) -> None:
    data.qpos[:] = 0.0
    for joint, degrees in (("exo_shoulder", shoulder_deg), ("exo_elbow", elbow_deg),
                           ("human_shoulder", shoulder_deg), ("human_elbow", elbow_deg)):
        jid = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, joint)
        data.qpos[model.jnt_qposadr[jid]] = math.radians(degrees)
    mujoco.mj_forward(model, data)


def hide(model: mujoco.MjModel, names: tuple[str, ...]) -> None:
    for name in names:
        gid = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, name)
        if gid >= 0:
            model.geom_rgba[gid, 3] = 0.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pose", default="0,0", help="shoulder,elbow in degrees")
    parser.add_argument("--hide", choices=("none", "human", "exo"), default="none")
    parser.add_argument("--out", default=str(OUT_DIR))
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--viewer", type=float, default=0.0,
                        help="open the interactive viewer for this many seconds")
    args = parser.parse_args()

    path = build_model.write_model()
    model = mujoco.MjModel.from_xml_path(str(path))
    data = mujoco.MjData(model)
    shoulder, elbow = (float(v) for v in args.pose.split(","))
    set_pose(model, data, shoulder, elbow)
    if args.hide == "human":
        hide(model, HUMAN_GEOMS)
    elif args.hide == "exo":
        hide(model, EXO_GEOMS)

    print(f"model {path}")
    print(f"pose shoulder {shoulder:.1f} deg, elbow {elbow:.1f} deg, hide={args.hide}")
    for name in ("exo_upper", "exo_elbow", "exo_fore", "human_fore", "human_hand"):
        pos = data.body(name).xpos
        print(f"  {name:12s} ({pos[0]:+.3f}, {pos[1]:+.3f}, {pos[2]:+.3f}) m")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    renderer = mujoco.Renderer(model, args.height, args.width)
    camera = mujoco.MjvCamera()
    camera.lookat[:] = [0.0, 0.0, 0.85]
    camera.distance = 1.6
    camera.elevation = -12
    for name, azimuth in (("front", 135), ("side", 90), ("back", 225)):
        camera.azimuth = azimuth
        renderer.update_scene(data, camera)
        image = renderer.render()
        target = out / f"scene_{name}_sh{shoulder:.0f}_el{elbow:.0f}.png"
        Image.fromarray(image).save(target)
        print(f"  wrote {target}")
    renderer.close()

    if args.viewer > 0.0:
        with mujoco.viewer.launch_passive(model, data) as viewer:
            end = time.monotonic() + args.viewer
            while viewer.is_running() and time.monotonic() < end:
                mujoco.mj_step(model, data)
                viewer.sync()
                time.sleep(model.opt.timestep)
        print("viewer opened and closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
