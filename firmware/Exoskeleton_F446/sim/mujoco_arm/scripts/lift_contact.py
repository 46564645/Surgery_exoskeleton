#!/usr/bin/env python3
"""Lift the arm with a human PD and show when it meets the table.

The human shoulder is driven toward ``--target`` degrees with the elbow held
straight; the exoskeleton is unpowered. Frames are rendered at fixed times and
written as one labelled contact sheet, and the first arm-table contact is
printed (time, shoulder angle, penetration).

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\lift_contact.py
    ... --target 40 --seconds 4 --out build\\sim\\scene
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import mujoco
import numpy as np
from PIL import Image, ImageDraw

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT / "model"))
import build_model as bm  # noqa: E402

OUT_DIR = FW_ROOT / "build" / "sim" / "scene"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", type=float, default=40.0, help="shoulder target, deg")
    parser.add_argument("--seconds", type=float, default=4.0)
    parser.add_argument("--out", default=str(OUT_DIR))
    parser.add_argument("--width", type=int, default=560)
    parser.add_argument("--height", type=int, default=460)
    args = parser.parse_args()

    model, info = bm.build()
    data = mujoco.MjData(model)
    table_geoms = {model.geom("table_top").id, model.geom("table_pedestal").id}
    acts = (model.actuator(bm.ACT_HUMAN_SHOULDER).id, model.actuator(bm.ACT_HUMAN_ELBOW).id)
    gains = ((500.0, 30.0), (150.0, 10.0))

    def control() -> None:
        data.ctrl[:] = 0.0
        for act, joint, target, (kp, kd) in zip(
                acts, (bm.HUMAN_SHOULDER, bm.HUMAN_ELBOW), (args.target, 0.0), gains):
            err = math.radians(target) - data.qpos[bm.joint_qpos(model, joint)]
            data.ctrl[act] = kp * err - kd * data.qvel[bm.joint_dof(model, joint)]

    def table_hits():
        return [c for c in data.contact[:data.ncon]
                if c.geom1 in table_geoms or c.geom2 in table_geoms]

    renderer = mujoco.Renderer(model, args.height, args.width)
    camera = mujoco.MjvCamera()
    # Side view: forward is -y (to the left of this view), so the approach of
    # the hand towards the table near edge is seen directly.
    camera.lookat[:] = [-0.24, 0.0, 1.08]
    camera.distance = 1.35
    camera.azimuth, camera.elevation = 0, -6

    steps = int(args.seconds / model.opt.timestep)
    shots = [int(s / model.opt.timestep) for s in (0.0, 0.03, 0.1, 0.5, 1.5, args.seconds)]
    frames, first = [], None
    for step in range(steps + 1):
        control()
        hits = table_hits()
        if first is None and hits:
            names = sorted({model.geom(g).name for c in hits for g in (c.geom1, c.geom2)
                            if g not in table_geoms})
            first = (data.time, math.degrees(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]),
                     max(-c.dist for c in hits), names)
        if step in shots:
            mujoco.mj_forward(model, data)
            hand = data.xpos[model.body("lunate_r").id].copy()
            images = []
            for lookat, distance, azimuth, elevation in (
                    (camera.lookat.copy(), 1.35, 0, -6),
                    (np.array([hand[0], hand[1] - 0.02, hand[2] + 0.02]), 0.55, 20, -14)):
                camera.lookat[:] = lookat
                camera.distance, camera.azimuth, camera.elevation = distance, azimuth, elevation
                renderer.update_scene(data, camera)
                hits = table_hits()
                if hits:
                    scene = renderer.scene
                    geom = scene.geoms[scene.ngeom]
                    mujoco.mjv_initGeom(geom, mujoco.mjtGeom.mjGEOM_SPHERE, [0.008, 0, 0],
                                        hits[0].pos.copy(), np.eye(3).flatten(), [1, 0.1, 0.1, 1])
                    scene.ngeom += 1
                images.append(Image.fromarray(renderer.render()))
            frames.append((data.time, math.degrees(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]),
                           images))
        mujoco.mj_step(model, data)
    renderer.close()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGB", (args.width * len(frames), 2 * (args.height + 26)), "white")
    draw = ImageDraw.Draw(sheet)
    for k, (t, angle, images) in enumerate(frames):
        touched = first is not None and t >= first[0]
        draw.text((k * args.width + 10, 6),
                  f"t={t:.2f}s  shoulder {angle:.1f} deg" + ("  TABLE CONTACT" if touched else ""),
                  fill="darkred" if touched else "black")
        for row, img in enumerate(images):
            sheet.paste(img, (k * args.width, 26 + row * (args.height + 26)))
            if row:
                draw.text((k * args.width + 10, 26 + row * (args.height + 26) - 20),
                          "close-up on the hand", fill="grey")
    target = out / f"lift_contact_target{args.target:.0f}.png"
    sheet.save(target)
    print(f"model {info['xml']}, table top {info['table_top_z']:.2f} m, edge y={info['table_edge_y']:.3f} m")
    print(f"wrote {target}")
    if first:
        print(f"first table contact at t={first[0]:.3f} s, shoulder {first[1]:.1f} deg, "
              f"penetration {first[2] * 1000:.1f} mm, touching: {', '.join(first[3])}")
    else:
        print("no table contact in this window")
    print(f"final shoulder {math.degrees(data.qpos[bm.joint_qpos(model, bm.HUMAN_SHOULDER)]):.1f} deg, "
          f"exoskeleton {math.degrees(data.qpos[bm.joint_qpos(model, bm.EXO_SHOULDER)]):.1f} deg")
    return 0


if __name__ == "__main__":
    sys.exit(main())
