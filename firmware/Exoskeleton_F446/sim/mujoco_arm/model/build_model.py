#!/usr/bin/env python3
"""Build the M1b model: MyoArm human + Fusion OrthoEase exoskeleton + table.

Sources (sim AGENTS reuse order):
  1. the user's Fusion export ``model/orthoease/orthoease.xml`` (exoskeleton
     geometry, kinematic chain, cuff binding sites; the copy stays read-only,
     every change is made here in code),
  3. MyoSim 0.2.3 ``myoarm_r`` (human arm on a fixed torso), and the static
     table writing of MyoSuite/RoboHive ``myoarm_relocate.xml``.

Physical numbers come from ``model/params.json``. Controller numbers stay in
the firmware (``App/application/s5_site.c``) and reach the simulation through
the bridge DLL.

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\model\\build_model.py
Writes build/sim/arm_exo_table.xml and prints the derived geometry.
"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

import mujoco
import myo_sim
import numpy as np

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
PARAMS_PATH = SIM_ROOT / "model" / "params.json"
OUT_PATH = FW_ROOT / "build" / "sim" / "arm_exo_table.xml"

EXO_PREFIX = "exo_"
HUMAN_SHOULDER = "shoulder_elv_r"
HUMAN_ELBOW = "elbow_flexion_r"
EXO_YAW = EXO_PREFIX + "stand_passive_yaw"
EXO_SHOULDER = EXO_PREFIX + "shoulder_motor"
EXO_ELBOW = EXO_PREFIX + "elbow_motor"
ACT_EXO_SHOULDER = "exo_shoulder_torque"
ACT_EXO_ELBOW = "exo_elbow_torque"
ACT_HUMAN_SHOULDER = "human_shoulder_torque"
ACT_HUMAN_ELBOW = "human_elbow_torque"
STRAP_UPPER = "strap_upper"
STRAP_FOREARM = "strap_forearm"
EXO_UPPER_SITE = EXO_PREFIX + "upper_arm_human_bind"
EXO_FOREARM_SITE = EXO_PREFIX + "forearm_human_bind"
HUMAN_UPPER_SITE = "human_upper_arm_exo_attach"
HUMAN_FOREARM_SITE = "human_forearm_exo_attach"
HUMAN_UPPER_BODY = "humerus_r"
HUMAN_FOREARM_BODY = "ulna_r"
EXO_UPPER_BODY = EXO_PREFIX + "upper_arm_drive"
EXO_FOREARM_BODY = EXO_PREFIX + "forearm_drive"
EXO_STAND_BODY = EXO_PREFIX + "stand_rotary"
SCENE_BODIES = ("table", "mount")

# Parts of the upper-arm body that sit below the 04 adjustment rail and move
# with the elbow when the rail is extended (component_manifest.json).
ELBOW_SIDE_GEOMS = ("vis_05d_elbow_motor_mount", "vis_05c_elbow_fixed_output_disc",
                    "vis_elbow_motor_housing")

# Rotation that takes the Fusion export frame (forward = -x, exoskeleton on
# +y of the arm) into the MyoArm frame (forward = -y, right side = -x):
# Rz(+90 deg) maps (x, y, z) -> (-y, x, z).
EXO_QUAT = np.array([math.cos(math.pi / 4), 0.0, 0.0, math.sin(math.pi / 4)])


def load_params(path: Path = PARAMS_PATH) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def _v(entry: dict):
    return entry["value"]


def _rot(vec) -> np.ndarray:
    """Fusion export frame -> MyoArm frame (Rz +90 deg)."""
    x, y, z = vec
    return np.array([-y, x, z], dtype=float)


def _unrot(vec) -> np.ndarray:
    x, y, z = vec
    return np.array([y, -x, z], dtype=float)


def clamp_bore(path: Path, slices: int = 12, grid: int = 200) -> tuple[np.ndarray, float]:
    """Centre (Fusion x, y) and radius of the enclosed bore of the 01B clamp.

    Each z slice is cut into segments, rasterised and flood filled from the
    border; the largest enclosed hole is the tube bore. Returns the median
    centre and the median hole radius over the slices that show a hole.
    """
    raw = path.read_bytes()
    count = struct.unpack("<I", raw[80:84])[0]
    tri = np.frombuffer(raw[84:84 + 50 * count],
                        dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
    tris = tri["v"].reshape(-1, 3, 3).astype(np.float64)
    lo, hi = tris.reshape(-1, 3).min(axis=0), tris.reshape(-1, 3).max(axis=0)
    gx = np.linspace(lo[0], hi[0], grid)
    gy = np.linspace(lo[1], hi[1], grid)
    step = gx[1] - gx[0]
    centres, radii = [], []
    for zc in np.linspace(lo[2] + 0.005, hi[2] - 0.005, slices):
        occ = np.zeros((grid, grid), bool)
        for t in tris:
            d = t[:, 2] - zc
            if d.max() < 0 or d.min() > 0:
                continue
            pts = []
            for i in range(3):
                a, b = t[i], t[(i + 1) % 3]
                if (a[2] - zc) * (b[2] - zc) <= 0 and abs(b[2] - a[2]) > 1e-12:
                    s = (zc - a[2]) / (b[2] - a[2])
                    pts.append(a[:2] + s * (b[:2] - a[:2]))
            if len(pts) == 2:
                a, b = pts
                for s in np.linspace(0.0, 1.0, int(np.hypot(*(b - a)) / step) + 2):
                    q = a + s * (b - a)
                    occ[int(round((q[0] - lo[0]) / step)), int(round((q[1] - lo[1]) / step))] = True
        outside = np.zeros_like(occ)
        stack = [(0, 0)]
        outside[0, 0] = True
        while stack:
            i, j = stack.pop()
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                a, b = i + di, j + dj
                if 0 <= a < grid and 0 <= b < grid and not outside[a, b] and not occ[a, b]:
                    outside[a, b] = True
                    stack.append((a, b))
        hole = ~occ & ~outside
        if not hole.any():
            continue
        # Every 4-connected hole component; the tube bore is the round one
        # (the clamp also has open jaw slots that are not circular).
        seen = np.zeros_like(hole)
        best, best_area = None, 0
        for start in zip(*np.nonzero(hole)):
            if seen[start]:
                continue
            comp, stack = [], [start]
            seen[start] = True
            while stack:
                i, j = stack.pop()
                comp.append((i, j))
                for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    a, b = i + di, j + dj
                    if 0 <= a < grid and 0 <= b < grid and hole[a, b] and not seen[a, b]:
                        seen[a, b] = True
                        stack.append((a, b))
            ii, jj = (np.array(c) for c in zip(*comp))
            cx, cy = gx[ii].mean(), gy[jj].mean()
            radius = float(np.sqrt((gx[ii] - cx) ** 2 + (gy[jj] - cy) ** 2).max())
            circularity = len(comp) * step * step / (math.pi * radius * radius) if radius else 0.0
            if 0.005 < radius < 0.012 and circularity > 0.85 and len(comp) > best_area:
                best, best_area = (cx, cy, radius), len(comp)
        if best is not None:
            centres.append(best[:2])
            radii.append(best[2])
    if not centres:
        raise RuntimeError("no clamp bore found in the 01B mesh")
    return np.median(np.array(centres), axis=0), float(np.median(radii))


def _axis_angle_quat(axis, angle: float) -> np.ndarray:
    axis = np.asarray(axis, dtype=float)
    axis = axis / np.linalg.norm(axis)
    half = 0.5 * angle
    return np.concatenate(([math.cos(half)], math.sin(half) * axis))


def stl_volume_centroid(path: Path) -> tuple[float, np.ndarray, np.ndarray, np.ndarray]:
    """Volume (m^3), volume centroid and bounding box of a binary STL."""
    raw = path.read_bytes()
    count = struct.unpack("<I", raw[80:84])[0]
    tri = np.frombuffer(raw[84:84 + 50 * count],
                        dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
    v = tri["v"].reshape(-1, 3, 3).astype(np.float64)
    signed = np.einsum("ij,ij->i", v[:, 0], np.cross(v[:, 1], v[:, 2])) / 6.0
    volume = float(signed.sum())
    centroid = (signed[:, None] * v.sum(axis=1) / 4.0).sum(axis=0) / volume
    pts = v.reshape(-1, 3)
    return abs(volume), centroid, pts.min(axis=0), pts.max(axis=0)


# --------------------------------------------------------------------------
# Human: MyoArm without muscles, two degrees of freedom.
# --------------------------------------------------------------------------

def build_human_spec(params: dict) -> mujoco.MjSpec:
    human = params["human"]
    if not _v(human["lock_shoulder_girdle"]) or not _v(human["remove_muscles"]):
        raise NotImplementedError("M1b only supports a locked girdle without muscles")
    spec = myo_sim.load_spec(_v(human["myo_sim_model"]))
    for group in (spec.actuators, spec.tendons, spec.equalities, spec.pairs,
                  spec.excludes, spec.sensors, spec.keys):
        for element in list(group):
            spec.delete(element)

    # Bake the elevation plane: elv_angle sits on humphant_r and shoulder1_r2
    # (= -elv_angle) is the last transform of humphant1_r, so with both fixed
    # they become the orientations of humphant_r and humerus_r (pos = 0).
    plane = float(_v(human["shoulder_plane_rad"]))
    elv_axis = np.array(spec.joint("elv_angle_r").axis)
    sh1_axis = np.array(spec.joint("shoulder1_r2_r").axis)
    keep = {HUMAN_SHOULDER, HUMAN_ELBOW}
    for joint in list(spec.joints):
        if joint.name not in keep:
            spec.delete(joint)
    spec.body("humphant_r").quat = _axis_angle_quat(elv_axis, plane)
    spec.body("humerus_r").quat = _axis_angle_quat(sh1_axis, -plane)

    # Human geoms touch the table only (contype 2 / conaffinity 0): no
    # self-contact inside the now rigid bone groups, no contact with the
    # exoskeleton they are strapped to.
    for geom in spec.geoms:
        if geom.contype or geom.conaffinity:
            geom.contype, geom.conaffinity = 2, 0

    for name, joint, limit in ((ACT_HUMAN_SHOULDER, HUMAN_SHOULDER, human["shoulder_torque_limit_nm"]),
                               (ACT_HUMAN_ELBOW, HUMAN_ELBOW, human["elbow_torque_limit_nm"])):
        act = spec.add_actuator(name=name, target=joint, trntype=mujoco.mjtTrn.mjTRN_JOINT)
        act.set_to_motor()
        act.ctrllimited = mujoco.mjtLimited.mjLIMITED_TRUE
        act.ctrlrange = [-float(_v(limit)), float(_v(limit))]
    return spec


def human_landmarks(spec: mujoco.MjSpec) -> dict:
    model = spec.compile()
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    arm_root = model.body(HUMAN_UPPER_BODY).id
    arm = [b for b in range(model.nbody)
           if b == arm_root or _is_descendant(model, b, arm_root)]
    return {
        "shoulder": data.xpos[model.body(HUMAN_UPPER_BODY).id].copy(),
        "elbow": data.xpos[model.body(HUMAN_FOREARM_BODY).id].copy(),
        "wrist": data.xpos[model.body("lunate_r").id].copy(),
        "arm_front_y": float(min(data.xpos[b][1] for b in arm)),
        "shoulder_axis": data.xaxis[model.joint(HUMAN_SHOULDER).id].copy(),
        "elbow_axis": data.xaxis[model.joint(HUMAN_ELBOW).id].copy(),
        "upper_mass": float(model.body_subtreemass[arm_root]),
        "forearm_mass": float(model.body_subtreemass[model.body(HUMAN_FOREARM_BODY).id]),
    }


def _is_descendant(model: mujoco.MjModel, body: int, root: int) -> bool:
    while body > 0:
        body = int(model.body_parentid[body])
        if body == root:
            return True
    return False


# --------------------------------------------------------------------------
# Exoskeleton: Fusion export, edited in code.
# --------------------------------------------------------------------------

def build_exo_spec(params: dict, land: dict) -> tuple[mujoco.MjSpec, dict]:
    exo = params["exo"]
    pkg_xml = SIM_ROOT / _v(exo["package_xml"])
    pkg_dir = pkg_xml.parent
    spec = mujoco.MjSpec.from_file(str(pkg_xml))

    # Package scene, position servos and sensors are replaced by ours.
    for group in (spec.actuators, spec.sensors, spec.keys):
        for element in list(group):
            spec.delete(element)
    for element in (list(spec.worldbody.geoms) + list(spec.worldbody.lights)
                    + list(spec.worldbody.cameras)):
        spec.delete(element)

    # Angles below are written in radians.
    spec.compiler.degree = False
    for mesh in spec.meshes:
        mesh.file = str((pkg_dir / mesh.file).resolve())

    # Masses from STL volume x ABS (Q4); motors use their catalogue mass.
    density = float(_v(exo["abs_density_kg_m3"]))
    volumes = {mesh.name: stl_volume_centroid(Path(mesh.file)) for mesh in spec.meshes}
    gim = ("shoulder_motor_gim6010", "shoulder_motor_gim6010_2")
    gim_volume = sum(volumes[name][0] for name in gim)
    masses = {}
    for geom in spec.geoms:
        if geom.name.startswith("col_"):
            # Package collision capsules: massless and inert (the human arm
            # overlaps them at the cuffs; the table is not reached).
            geom.mass, geom.contype, geom.conaffinity = 0.0, 0, 0
            continue
        if not geom.meshname:
            continue
        volume = volumes[geom.meshname][0]
        if geom.meshname == "elbow_motor_housing":
            mass = float(_v(exo["elbow_motor_mass_kg"]))
        elif geom.meshname in gim:
            mass = float(_v(exo["shoulder_motor_mass_kg"])) * volume / gim_volume
        else:
            mass = volume * density
        geom.mass = mass
        masses[geom.name] = mass
    for mesh in spec.meshes:
        mesh.inertia = mujoco.mjtMeshInertia.mjMESH_INERTIA_EXACT

    deg = math.radians
    yaw = spec.joint("stand_passive_yaw")
    # mujoco 3.16 joint damping is a polynomial [linear, ...]; only linear.
    yaw.damping = [float(_v(exo["yaw_damping_nms_rad"])), 0.0, 0.0]
    yaw.range = [deg(-90.0), deg(90.0)]
    for name, damping, rng in (("shoulder_motor", exo["shoulder_damping_nms_rad"], exo["shoulder_range_deg"]),
                               ("elbow_motor", exo["elbow_damping_nms_rad"], exo["elbow_range_deg"])):
        joint = spec.joint(name)
        joint.damping = [float(_v(damping)), 0.0, 0.0]
        joint.armature = float(_v(exo["joint_armature_kgm2"]))
        joint.range = [deg(v) for v in _v(rng)]

    # Placement. Package frame: stand_base at the origin, no rotations, so
    # body positions add up to world positions.
    stand = np.array(spec.body("stand_rotary").pos)
    upper = np.array(spec.body("upper_arm_drive").pos)
    fore = np.array(spec.body("forearm_drive").pos)
    shoulder_cad = stand + upper
    elbow_cad = shoulder_cad + fore
    upper_bind_cad = shoulder_cad + np.array(spec.site("upper_arm_human_bind").pos)

    # Shoulder axis through the MyoArm shoulder centre (y, z); lateral offset
    # so that the upper cuff inner surface lies one arm radius outside the
    # humerus axis (cuff section 03C).
    sh, el = land["shoulder"], land["elbow"]
    shift = np.zeros(3)
    shift[1:] = sh[1:] - _rot(shoulder_cad)[1:]
    bind_z = _rot(upper_bind_cad)[2] + shift[2]
    s = (sh[2] - bind_z) / (sh[2] - el[2])
    humerus_axis_x = sh[0] + s * (el[0] - sh[0])
    radius_upper = float(_v(params["human"]["upper_arm_radius_m"]))
    shift[0] = humerus_axis_x - radius_upper - _rot(upper_bind_cad)[0]

    # Q1: extend along the 04 rail so the exoskeleton elbow axis passes
    # through the MyoArm elbow (in the flexion plane y-z; the lateral
    # coordinate along the axis is kept).
    elbow_world = _rot(elbow_cad) + shift
    delta_cad = np.zeros(3)
    if _v(exo["match_human_upper_arm"]):
        target = np.array([elbow_world[0], el[1], el[2]])
        delta_cad = _unrot(target - elbow_world)
        spec.body("forearm_drive").pos = fore + delta_cad
        for geom in spec.geoms:
            if geom.name in ELBOW_SIDE_GEOMS:
                geom.pos = np.array(geom.pos) + delta_cad
    fore_new = fore + delta_cad
    forearm_bind_cad = shoulder_cad + fore_new + np.array(spec.site("forearm_human_bind").pos)
    _inertia_from_geoms(spec, ("stand_rotary", "upper_arm_drive", "forearm_drive"))

    def in_plane(vec):
        return float(math.hypot(vec[0], vec[2]))  # package frame: x-z plane

    info = {
        "exo_shift": shift,
        "exo_upper_len_before": in_plane(fore),
        "exo_upper_len_after": in_plane(fore_new),
        "rail_extension": float(np.linalg.norm(delta_cad)),
        "exo_shoulder_world": _rot(shoulder_cad) + shift,
        "exo_elbow_world": _rot(shoulder_cad + fore_new) + shift,
        "upper_bind_world": _rot(upper_bind_cad) + shift,
        "forearm_bind_world": _rot(forearm_bind_cad) + shift,
        "part_masses": masses,
    }
    clamp = volumes["01b_stand_clamp"]
    info["clamp_world_lo"] = _rot(clamp[2]) + shift
    info["clamp_world_hi"] = _rot(clamp[3]) + shift
    bore_xy, bore_radius = clamp_bore(Path(spec.mesh("01b_stand_clamp").file))
    info["clamp_bore_fusion_xy"] = bore_xy
    info["clamp_bore_radius"] = bore_radius
    info["clamp_bore_world"] = _rot(np.array([bore_xy[0], bore_xy[1], 0.0]))[:2] + shift[:2]
    return spec, info


def _inertia_from_geoms(spec: mujoco.MjSpec, bodies: tuple[str, ...]) -> None:
    """Replace the package's placeholder <inertial> with geom-derived values.

    The package bodies carry explicit inertials (2.0/2.5/1.2 kg). A copy is
    compiled with inertiafromgeom=true, so each body's mass, centre of mass
    and inertia follow from the mesh geoms whose masses were set above, and
    those numbers are written back as the body inertial.
    """
    probe = spec.copy()
    probe.compiler.inertiafromgeom = mujoco.mjtInertiaFromGeom.mjINERTIAFROMGEOM_TRUE
    model = probe.compile()
    for name in bodies:
        bid = model.body(name).id
        body = spec.body(name)
        body.mass = float(model.body_mass[bid])
        body.ipos = model.body_ipos[bid].copy()
        body.iquat = model.body_iquat[bid].copy()
        body.inertia = model.body_inertia[bid].copy()
        body.explicitinertial = True


# --------------------------------------------------------------------------
# Assembly.
# --------------------------------------------------------------------------

def _local_pose(model: mujoco.MjModel, data: mujoco.MjData, body: str,
                pos_world: np.ndarray, quat_world: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    bid = model.body(body).id
    inv = np.zeros(4)
    mujoco.mju_negQuat(inv, data.xquat[bid])
    local_pos = np.zeros(3)
    mujoco.mju_rotVecQuat(local_pos, pos_world - data.xpos[bid], inv)
    local_quat = np.zeros(4)
    mujoco.mju_mulQuat(local_quat, inv, quat_world)
    return local_pos, local_quat


def _add_scene(spec: mujoco.MjSpec, params: dict, land: dict, info: dict) -> None:
    table, mount = params["table"], params["mount"]
    top = float(_v(table["height_m"]))
    thick = float(_v(table["top_thickness_m"]))
    width = float(_v(table["width_x_m"]))
    depth = float(_v(table["depth_y_m"]))
    center_x = float(_v(table["center_x_m"]))
    radius = float(_v(params["human"]["upper_arm_radius_m"]))
    edge_y = land["arm_front_y"] - radius - float(_v(table["edge_clearance_m"]))
    collide = bool(_v(table["collision_enabled"]))
    ct, ca = (0, 2) if collide else (0, 0)

    world = spec.worldbody
    world.add_light(name="scene_key", pos=[0.6, -1.2, 2.4], dir=[-0.3, 0.6, -1.0],
                    type=int(mujoco.mjtLightType.mjLIGHT_DIRECTIONAL))
    # MyoArm ships its own floor plane at z = 0; it must not touch anything.
    floor = spec.geom("floor")
    floor.contype, floor.conaffinity = 0, 0
    body = world.add_body(name="table", pos=[center_x, edge_y - depth / 2, top - thick / 2])
    body.add_geom(name="table_top", type=mujoco.mjtGeom.mjGEOM_BOX,
                  size=[width / 2, depth / 2, thick / 2], rgba=[0.78, 0.70, 0.58, 1],
                  contype=ct, conaffinity=ca)
    body.add_geom(name="table_pedestal", type=mujoco.mjtGeom.mjGEOM_BOX,
                  size=[0.25 * width, 0.25 * depth, (top - thick) / 2],
                  pos=[0, 0, -top / 2], rgba=[0.55, 0.47, 0.38, 1], contype=ct, conaffinity=ca)

    # Fixed seat on the near edge, reaching back past the edge to the pole;
    # the pole goes up through the bore of the 01B clamp of the Fusion model,
    # so its axis is the bore axis, not the part's bounding-box centre.
    lo, hi = info["clamp_world_lo"], info["clamp_world_hi"]
    pole_x, pole_y = info["clamp_bore_world"]
    clamp_mid_z = 0.5 * (min(lo[2], hi[2]) + max(lo[2], hi[2]))
    block_h = float(_v(mount["block_height_m"]))
    block_w = float(_v(mount["block_width_m"]))
    y0 = edge_y - float(_v(mount["block_inset_m"]))
    y1 = pole_y + float(_v(mount["block_tail_m"]))
    pole_r = 0.5 * float(_v(mount["pole_diameter_m"]))
    seat_top = top + block_h
    mnt = world.add_body(name="mount")
    mnt.add_geom(name="seat_block", type=mujoco.mjtGeom.mjGEOM_BOX,
                 size=[block_w / 2, (y1 - y0) / 2, block_h / 2],
                 pos=[pole_x, 0.5 * (y0 + y1), top + block_h / 2],
                 rgba=[0.35, 0.35, 0.38, 1], contype=0, conaffinity=0)
    mnt.add_geom(name="pole", type=mujoco.mjtGeom.mjGEOM_CYLINDER,
                 size=[pole_r, (clamp_mid_z - seat_top) / 2, 0],
                 pos=[pole_x, pole_y, 0.5 * (clamp_mid_z + seat_top)],
                 rgba=[0.55, 0.55, 0.58, 1], contype=0, conaffinity=0)
    info.update({"table_edge_y": edge_y, "table_top_z": top, "pole_xy": (pole_x, pole_y),
                 "pole_radius": pole_r,
                 "pole_length": clamp_mid_z - seat_top, "seat_top_z": seat_top,
                 "seat_overhang": y1 - edge_y})


def build(params: dict | None = None, write: bool = True) -> tuple[mujoco.MjModel, dict]:
    params = params or load_params()
    spec = build_human_spec(params)
    land = human_landmarks(spec)
    exo_spec, info = build_exo_spec(params, land)
    info["human"] = land

    # Global options follow the human (parent) spec; align the child so the
    # attach step has nothing to arbitrate.
    exo_spec.option.integrator = spec.option.integrator
    exo_spec.memory = spec.memory
    frame = spec.worldbody.add_frame(pos=info["exo_shift"], quat=EXO_QUAT)
    spec.attach(exo_spec, prefix=EXO_PREFIX, frame=frame)

    spec.option.timestep = float(_v(params["meta"]["timestep_s"]))
    spec.option.gravity = [0.0, 0.0, -float(_v(params["meta"]["gravity_m_s2"]))]
    spec.option.integrator = mujoco.mjtIntegrator.mjINT_IMPLICITFAST
    spec.visual.global_.offwidth = 1280
    spec.visual.global_.offheight = 960
    _add_scene(spec, params, land, info)

    limit = float(_v(params["exo"]["motor_torque_limit_nm"]))
    for name, joint in ((ACT_EXO_SHOULDER, EXO_SHOULDER), (ACT_EXO_ELBOW, EXO_ELBOW)):
        act = spec.add_actuator(name=name, target=joint, trntype=mujoco.mjtTrn.mjTRN_JOINT)
        act.set_to_motor()
        act.ctrllimited = mujoco.mjtLimited.mjLIMITED_TRUE
        act.ctrlrange = [-limit, limit]

    # Human cuff sites at the exoskeleton binding sites (cuff inner surface)
    # in the home pose, so both welds start without violation (F5).
    model = spec.compile()
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    for exo_site, human_site, body in ((EXO_UPPER_SITE, HUMAN_UPPER_SITE, HUMAN_UPPER_BODY),
                                       (EXO_FOREARM_SITE, HUMAN_FOREARM_SITE, HUMAN_FOREARM_BODY)):
        sid = model.site(exo_site).id
        quat = np.zeros(4)
        mujoco.mju_mat2Quat(quat, data.site_xmat[sid])
        pos, local_quat = _local_pose(model, data, body, data.site_xpos[sid].copy(), quat)
        spec.body(body).add_site(name=human_site, pos=pos, quat=local_quat, size=[0.008, 0, 0],
                                 rgba=[0.1, 0.8, 0.2, 1])

    straps = params["straps"]
    for name, exo_site, human_site in ((STRAP_UPPER, EXO_UPPER_SITE, HUMAN_UPPER_SITE),
                                       (STRAP_FOREARM, EXO_FOREARM_SITE, HUMAN_FOREARM_SITE)):
        eq = spec.add_equality(name=name, type=mujoco.mjtEq.mjEQ_WELD,
                               objtype=mujoco.mjtObj.mjOBJ_SITE, name1=exo_site, name2=human_site)
        eq.solref = [float(_v(straps["solref_timeconst_s"])), float(_v(straps["solref_dampratio"]))]
        data_vec = np.array(eq.data)
        data_vec[10] = float(_v(straps["torquescale"]))
        eq.data = data_vec

    for joint in (EXO_YAW, EXO_SHOULDER, EXO_ELBOW, HUMAN_SHOULDER, HUMAN_ELBOW):
        spec.add_sensor(name=joint + "_pos", type=mujoco.mjtSensor.mjSENS_JOINTPOS,
                        objtype=mujoco.mjtObj.mjOBJ_JOINT, objname=joint)
        spec.add_sensor(name=joint + "_vel", type=mujoco.mjtSensor.mjSENS_JOINTVEL,
                        objtype=mujoco.mjtObj.mjOBJ_JOINT, objname=joint)
    for act in (ACT_EXO_SHOULDER, ACT_EXO_ELBOW, ACT_HUMAN_SHOULDER, ACT_HUMAN_ELBOW):
        spec.add_sensor(name=act + "_frc", type=mujoco.mjtSensor.mjSENS_ACTUATORFRC,
                        objtype=mujoco.mjtObj.mjOBJ_ACTUATOR, objname=act)

    model = spec.compile()
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    info["exo_body_mass"] = {name: float(model.body_mass[model.body(name).id])
                             for name in (EXO_STAND_BODY, EXO_UPPER_BODY, EXO_FOREARM_BODY)}
    forearm_axis = data.xpos[model.body(HUMAN_FOREARM_BODY).id]
    wrist = data.xpos[model.body("lunate_r").id]
    info["forearm_cuff_offset"] = _point_line_distance(info["forearm_bind_world"], forearm_axis, wrist)
    info["upper_cuff_offset"] = _point_line_distance(info["upper_bind_world"], land["shoulder"],
                                                    land["elbow"])
    if write:
        OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
        OUT_PATH.write_text(spec.to_xml(), encoding="utf-8")
        info["xml"] = OUT_PATH
    return model, info


def _point_line_distance(point, a, b) -> float:
    a, b, point = (np.asarray(v, dtype=float) for v in (a, b, point))
    direction = (b - a) / np.linalg.norm(b - a)
    rel = point - a
    return float(np.linalg.norm(rel - rel.dot(direction) * direction))


def joint_qpos(model: mujoco.MjModel, name: str) -> int:
    return int(model.jnt_qposadr[model.joint(name).id])


def joint_dof(model: mujoco.MjModel, name: str) -> int:
    return int(model.jnt_dofadr[model.joint(name).id])


def body_group(model: mujoco.MjModel, name: str) -> str:
    """'exo', 'scene' or 'human' for a body name of the built model."""
    if name.startswith(EXO_PREFIX):
        return "exo"
    if name in SCENE_BODIES or name == "world":
        return "scene"
    return "human"


def main() -> None:
    model, info = build()
    land = info["human"]
    mm = 1000.0
    print(f"wrote {info['xml']}")
    print(f"model: nq={model.nq} nv={model.nv} nu={model.nu} neq={model.neq} nbody={model.nbody}")
    print(f"human shoulder centre {np.round(land['shoulder'], 4)}, elbow {np.round(land['elbow'], 4)}, "
          f"upper arm {np.linalg.norm(land['elbow'] - land['shoulder']) * mm:.1f} mm, "
          f"arm mass {land['upper_mass']:.3f} kg (forearm+hand {land['forearm_mass']:.3f} kg)")
    print(f"exoskeleton shoulder axis point {np.round(info['exo_shoulder_world'], 4)}, "
          f"elbow {np.round(info['exo_elbow_world'], 4)}")
    print(f"upper-arm length {info['exo_upper_len_before'] * mm:.1f} -> "
          f"{info['exo_upper_len_after'] * mm:.1f} mm (rail extension {info['rail_extension'] * mm:.1f} mm)")
    print(f"cuff inner surface to bone axis: upper {info['upper_cuff_offset'] * mm:.1f} mm, "
          f"forearm {info['forearm_cuff_offset'] * mm:.1f} mm")
    print("exoskeleton body mass: " + ", ".join(
        f"{k[len(EXO_PREFIX):]} {v:.3f} kg" for k, v in info["exo_body_mass"].items()))
    print(f"table top {info['table_top_z']:.2f} m, near edge y={info['table_edge_y']:.3f} m; "
          f"seat top {info['seat_top_z']:.3f} m, overhang {info['seat_overhang'] * mm:.0f} mm; "
          f"pole at ({info['pole_xy'][0]:.3f}, {info['pole_xy'][1]:.3f}), "
          f"length {info['pole_length']:.3f} m")
    print(f"clamp bore: Fusion ({info['clamp_bore_fusion_xy'][0]:.5f}, "
          f"{info['clamp_bore_fusion_xy'][1]:.5f}) m, dia {2 * info['clamp_bore_radius'] * 1000:.1f} mm; "
          f"pole dia {2 * info['pole_radius'] * 1000:.1f} mm on the same axis")


if __name__ == "__main__":
    main()
