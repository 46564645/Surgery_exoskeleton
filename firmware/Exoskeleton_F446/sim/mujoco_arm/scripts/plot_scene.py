#!/usr/bin/env python3
"""Render the M1b scene (MyoArm + Fusion OrthoEase + table), headless or live.

Rebuilds the model from params.json, sets a pose on both the human and the
exoskeleton joints, and writes PNG views comparable with the user's CAD four
views: front (camera in front of the person), right side, back, top, and a
right-front oblique. ``--hide human`` / ``--hide exo`` makes one group
transparent for that render only. ``--viewer`` opens the MuJoCo window.

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
import mujoco.viewer
import numpy as np
from PIL import Image

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT / "model"))
import build_model as bm  # noqa: E402

OUT_DIR = FW_ROOT / "build" / "sim" / "scene"
# MuJoCo free camera: azimuth 90 looks along +y, i.e. from in front of a
# person who faces -y; azimuth 0 looks along +x, i.e. from the right side.
VIEWS = (("front", 90, -10), ("right", 0, -10), ("back", 270, -10),
         ("top", 90, -89), ("oblique", 45, -20), ("pole", 250, 55))


def set_pose(model: mujoco.MjModel, data: mujoco.MjData,
             shoulder_deg: float, elbow_deg: float) -> None:
    data.qpos[:] = 0.0
    for joint, degrees in ((bm.EXO_SHOULDER, shoulder_deg), (bm.EXO_ELBOW, elbow_deg),
                           (bm.HUMAN_SHOULDER, shoulder_deg), (bm.HUMAN_ELBOW, elbow_deg)):
        data.qpos[bm.joint_qpos(model, joint)] = math.radians(degrees)
    mujoco.mj_forward(model, data)


def hide(model: mujoco.MjModel, group: str) -> None:
    for gid in range(model.ngeom):
        body = model.body(int(model.geom_bodyid[gid])).name
        if bm.body_group(model, body) == group:
            model.geom_rgba[gid, 3] = 0.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pose", default="0,0", help="shoulder,elbow in degrees")
    parser.add_argument("--hide", choices=("none", "human", "exo"), default="none")
    parser.add_argument("--out", default=str(OUT_DIR))
    parser.add_argument("--width", type=int, default=900)
    parser.add_argument("--height", type=int, default=700)
    parser.add_argument("--viewer", type=float, default=0.0,
                        help="open the interactive viewer for this many seconds")
    args = parser.parse_args()

    model, info = bm.build()
    data = mujoco.MjData(model)
    shoulder, elbow = (float(v) for v in args.pose.split(","))
    set_pose(model, data, shoulder, elbow)
    if args.hide != "none":
        hide(model, args.hide)

    print(f"model {info['xml']}")
    print(f"pose shoulder {shoulder:.1f} deg, elbow {elbow:.1f} deg, hide={args.hide}")
    for name in (bm.EXO_UPPER_BODY, bm.EXO_FOREARM_BODY, bm.HUMAN_UPPER_BODY, bm.HUMAN_FOREARM_BODY):
        pos = data.body(name).xpos
        print(f"  {name:20s} ({pos[0]:+.3f}, {pos[1]:+.3f}, {pos[2]:+.3f}) m")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    renderer = mujoco.Renderer(model, args.height, args.width)
    camera = mujoco.MjvCamera()
    for name, azimuth, elevation in VIEWS:
        if name == "pole":
            # Close-up on the 01B clamp where the 14 mm pole passes through.
            bore = info["clamp_bore_world"]
            camera.lookat[:] = [bore[0], bore[1], 0.5 * (info["clamp_world_lo"][2]
                                                         + info["clamp_world_hi"][2])]
            camera.distance = 0.45
        else:
            camera.lookat[:] = [-0.22, 0.02, 1.15]
            camera.distance = 1.9
        camera.azimuth = azimuth
        camera.elevation = elevation
        renderer.update_scene(data, camera)
        target = out / f"scene_{name}_sh{shoulder:.0f}_el{elbow:.0f}_{args.hide}.png"
        Image.fromarray(renderer.render()).save(target)
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
