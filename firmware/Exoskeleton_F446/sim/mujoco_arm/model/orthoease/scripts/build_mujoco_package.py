#!/usr/bin/env python3
"""Build a directly loadable MJCF package from the read-only Fusion snapshot."""

from __future__ import annotations

import gzip
import json
import math
import re
from pathlib import Path
from xml.etree import ElementTree as ET

import numpy as np
import trimesh


ROOT = Path(__file__).resolve().parents[1]
SNAPSHOT = ROOT / "metadata" / "fusion_snapshot.json.gz"
ASSETS = ROOT / "assets"


PARTS = [
    ("01B_", "01b_stand_clamp", "base", "0.48 0.78 0.86 1"),
    ("01A_", "01a_passive_rotary_base", "stand", "0.44 0.58 0.84 1"),
    ("02E_", "02e_shoulder_motor_mount", "stand", "0.48 0.62 0.34 1"),
    ("GIM6010", "shoulder_motor_gim6010", "stand", "0.72 0.72 0.68 1"),
    ("02B_", "02b_upper_arm_beam", "upper", "0.85 0.85 0.85 1"),
    ("04_", "04_upper_arm_adjustment_rail", "upper", "0.92 0.92 0.92 1"),
    ("02C_", "02c_shoulder_driven_rotor", "upper", "0.72 0.55 0.30 1"),
    ("02D_", "02d_shoulder_output_link", "upper", "0.78 0.63 0.38 1"),
    ("03_", "03_upper_arm_magnetic_interface", "upper", "0.70 0.58 0.82 0.92"),
    ("03C_", "03c_upper_arm_cuff", "upper", "0.61 0.43 0.74 0.78"),
    ("05D_", "05d_elbow_motor_mount", "upper", "0.82 0.30 0.26 1"),
    ("05C_", "05c_elbow_fixed_output_disc", "upper", "0.78 0.20 0.18 1"),
    ("肘部电机_", "elbow_motor_housing", "upper", "0.91 0.72 0.30 1"),
    ("05B_", "05a_elbow_driven_fork", "forearm", "0.48 0.05 0.16 1"),
    ("06_", "06_triple_head_link", "forearm", "0.46 0.66 0.90 1"),
    ("08B_", "08b_forearm_magnetic_interface", "forearm", "0.86 0.80 0.35 1"),
    ("08A_", "08a_forearm_cuff", "forearm", "0.82 0.35 0.08 0.88"),
]


def fusion_to_mj(points_cm: np.ndarray) -> np.ndarray:
    """Fusion XYZ cm -> MuJoCo XYZ m. Fusion +Y is down in the installed pose."""
    return np.column_stack((points_cm[:, 0], points_cm[:, 2], -points_cm[:, 1])) * 0.01


def point_to_mj(point_cm: list[float]) -> np.ndarray:
    return fusion_to_mj(np.asarray(point_cm, dtype=float).reshape(1, 3))[0]


def classify(path: str) -> tuple[str, str, str]:
    component = path.split(":", 1)[0]
    # Nested components use the last component for 04 and shoulder motor assets.
    tokens = path.split("+")
    candidates = [tokens[-1].split(":", 1)[0], component]
    for candidate in candidates:
        for prefix, asset, body, color in PARTS:
            if candidate.startswith(prefix):
                return asset, body, color
    raise KeyError(f"Unclassified Fusion occurrence: {path}")


def fmt(values) -> str:
    return " ".join(f"{float(x):.9g}" for x in values)


def add_visual(parent, asset: str, color: str):
    ET.SubElement(
        parent,
        "geom",
        name=f"vis_{asset}",
        type="mesh",
        mesh=asset,
        rgba=color,
        group="2",
        contype="0",
        conaffinity="0",
        mass="0",
    )


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    with gzip.open(SNAPSHOT, "rt", encoding="utf-8") as handle:
        data = json.load(handle)

    joints = {item["name"][:3]: item for item in data["joints"]}
    j1 = point_to_mj(np.array(joints["J01"]["transform_cm"]).reshape(4, 4)[:3, 3])
    j2 = point_to_mj(np.array(joints["J02"]["transform_cm"]).reshape(4, 4)[:3, 3])
    j3 = point_to_mj(np.array(joints["J03"]["transform_cm"]).reshape(4, 4)[:3, 3])
    origins = {"base": np.zeros(3), "stand": j1, "upper": j2, "forearm": j3}

    manifest = []
    body_assets = {name: [] for name in origins}
    used_names = {}
    for part in data["parts"]:
        asset, body, color = classify(part["path"])
        used_names[asset] = used_names.get(asset, 0) + 1
        if used_names[asset] > 1:
            asset = f"{asset}_{used_names[asset]}"
        vertices = np.asarray(part["vertices_cm"], dtype=float).reshape(-1, 3)
        transform = np.asarray(part["transform_cm"], dtype=float).reshape(4, 4)
        vertices_world_cm = vertices @ transform[:3, :3].T + transform[:3, 3]
        vertices_local_m = fusion_to_mj(vertices_world_cm) - origins[body]
        faces = np.asarray(part["triangles"], dtype=np.int64).reshape(-1, 3)
        mesh = trimesh.Trimesh(vertices=vertices_local_m, faces=faces, process=False)
        filename = f"{asset}.stl"
        mesh.export(ASSETS / filename, file_type="stl")
        body_assets[body].append((asset, color))
        manifest.append(
            {
                "asset": asset,
                "file": f"assets/{filename}",
                "fusion_path": part["path"],
                "fusion_body": part["name"],
                "rigid_body": body,
                "vertices": int(len(vertices)),
                "triangles": int(len(faces)),
                "visual_only": True,
            }
        )

    model = ET.Element("mujoco", model="orthoease_upper_limb_exoskeleton")
    ET.SubElement(model, "compiler", angle="degree", meshdir=".", autolimits="true")
    ET.SubElement(model, "option", timestep="0.002", gravity="0 0 -9.81", integrator="implicitfast")
    ET.SubElement(model, "size", memory="64M")
    visual = ET.SubElement(model, "visual")
    ET.SubElement(visual, "global", azimuth="135", elevation="-15")
    ET.SubElement(visual, "headlight", ambient="0.35 0.35 0.35", diffuse="0.7 0.7 0.7")
    default = ET.SubElement(model, "default")
    ET.SubElement(default, "joint", damping="1.5", armature="0.01", limited="true")
    ET.SubElement(default, "geom", friction="0.8 0.1 0.01")

    assets = ET.SubElement(model, "asset")
    ET.SubElement(assets, "texture", name="sky", type="skybox", builtin="gradient", rgb1="0.95 0.97 1", rgb2="0.55 0.62 0.70", width="512", height="3072")
    ET.SubElement(assets, "material", name="floor_mat", rgba="0.72 0.74 0.76 1", reflectance="0.15")
    for item in manifest:
        ET.SubElement(assets, "mesh", name=item["asset"], file=item["file"], inertia="convex")

    world = ET.SubElement(model, "worldbody")
    ET.SubElement(world, "light", name="key", pos="0 -1 1.2", dir="0 1 -1", directional="true")
    ET.SubElement(world, "geom", name="floor", type="plane", pos="0 0 -0.35", size="1 1 0.02", material="floor_mat")
    ET.SubElement(world, "camera", name="overview", pos="0.75 -0.85 0.55", xyaxes="0.75 0.66 0 -0.25 0.29 0.92")

    base_body = ET.SubElement(world, "body", name="stand_base")
    for asset, color in body_assets["base"]:
        add_visual(base_body, asset, color)

    stand = ET.SubElement(base_body, "body", name="stand_rotary", pos=fmt(j1))
    ET.SubElement(stand, "joint", name="stand_passive_yaw", type="hinge", axis="0 0 -1", range="-90 90", damping="3")
    ET.SubElement(stand, "inertial", pos="0 0 0", mass="2.0", diaginertia="0.01 0.01 0.01")
    for asset, color in body_assets["stand"]:
        add_visual(stand, asset, color)

    upper = ET.SubElement(stand, "body", name="upper_arm_drive", pos=fmt(j2 - j1))
    ET.SubElement(upper, "joint", name="shoulder_motor", type="hinge", axis="0 1 0", range="0 110", damping="2", armature="0.02")
    ET.SubElement(upper, "inertial", pos=fmt((j3 - j2) * 0.48), mass="2.5", diaginertia="0.035 0.006 0.035")
    for asset, color in body_assets["upper"]:
        add_visual(upper, asset, color)
    ET.SubElement(upper, "geom", name="col_upper_arm", type="capsule", fromto=fmt([0, 0, 0, *(j3 - j2)]), size="0.025", rgba="0.3 0.5 0.8 0", group="3")
    upper_bind_world = point_to_mj([0.3, -15.134, -4.15])
    ET.SubElement(upper, "site", name="upper_arm_human_bind", type="sphere", pos=fmt(upper_bind_world - j2), size="0.008", rgba="0.1 1 0.2 0.9", group="3")

    forearm = ET.SubElement(upper, "body", name="forearm_drive", pos=fmt(j3 - j2))
    ET.SubElement(forearm, "joint", name="elbow_motor", type="hinge", axis="0 1 0", range="0 110", damping="1.5", armature="0.015")
    forearm_bind_world = point_to_mj([0.3, 9.5, -4.15])
    forearm_tip = forearm_bind_world - j3
    ET.SubElement(forearm, "inertial", pos=fmt(forearm_tip * 0.55), mass="1.2", diaginertia="0.012 0.002 0.012")
    for asset, color in body_assets["forearm"]:
        add_visual(forearm, asset, color)
    ET.SubElement(forearm, "geom", name="col_forearm", type="capsule", fromto=fmt([0, 0, 0, *forearm_tip]), size="0.022", rgba="0.8 0.5 0.2 0", group="3")
    ET.SubElement(forearm, "site", name="forearm_human_bind", type="sphere", pos=fmt(forearm_tip), size="0.008", rgba="1 0.45 0.05 0.9", group="3")

    contact = ET.SubElement(model, "contact")
    ET.SubElement(contact, "exclude", body1="stand_rotary", body2="upper_arm_drive")
    ET.SubElement(contact, "exclude", body1="upper_arm_drive", body2="forearm_drive")

    actuator = ET.SubElement(model, "actuator")
    # Actuator controls and keyframe qpos use MuJoCo's native radians even when
    # compiler angle="degree" is used for angle-valued XML attributes.
    limit_rad = math.radians(110)
    ET.SubElement(actuator, "position", name="shoulder_position", joint="shoulder_motor", kp="120", kv="12", ctrlrange=fmt([0, limit_rad]), forcelimited="true", forcerange="-30 30")
    ET.SubElement(actuator, "position", name="elbow_position", joint="elbow_motor", kp="80", kv="8", ctrlrange=fmt([0, limit_rad]), forcelimited="true", forcerange="-20 20")

    sensor = ET.SubElement(model, "sensor")
    for joint in ["stand_passive_yaw", "shoulder_motor", "elbow_motor"]:
        ET.SubElement(sensor, "jointpos", name=f"{joint}_position", joint=joint)
        ET.SubElement(sensor, "jointvel", name=f"{joint}_velocity", joint=joint)
    ET.SubElement(sensor, "framepos", name="upper_bind_world_position", objtype="site", objname="upper_arm_human_bind")
    ET.SubElement(sensor, "framepos", name="forearm_bind_world_position", objtype="site", objname="forearm_human_bind")

    keyframe = ET.SubElement(model, "keyframe")
    ET.SubElement(keyframe, "key", name="home", qpos="0 0 0", ctrl="0 0")
    ET.SubElement(keyframe, "key", name="mid_flexion", qpos=fmt([0, math.radians(55), math.radians(55)]), ctrl=fmt([math.radians(55), math.radians(55)]))
    ET.SubElement(keyframe, "key", name="max_flexion", qpos=fmt([0, limit_rad, limit_rad]), ctrl=fmt([limit_rad, limit_rad]))

    ET.indent(model, space="  ")
    ET.ElementTree(model).write(ROOT / "orthoease.xml", encoding="utf-8", xml_declaration=True)
    with (ROOT / "metadata" / "component_manifest.json").open("w", encoding="utf-8") as handle:
        json.dump(
            {
                "source_document": data["document"],
                "coordinate_mapping": "MuJoCo XYZ [m] = (Fusion X, Fusion Z, -Fusion Y) [cm] * 0.01",
                "joint_origins_world_m": {"stand_passive_yaw": j1.tolist(), "shoulder_motor": j2.tolist(), "elbow_motor": j3.tolist()},
                "human_binding_sites_world_home_m": {"upper_arm_human_bind": upper_bind_world.tolist(), "forearm_human_bind": forearm_bind_world.tolist()},
                "parts": manifest,
            },
            handle,
            ensure_ascii=False,
            indent=2,
        )
    print(json.dumps({"xml": str(ROOT / "orthoease.xml"), "assets": len(manifest), "triangles": sum(i["triangles"] for i in manifest)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
