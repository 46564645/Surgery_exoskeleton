#!/usr/bin/env python3
"""Build the M1 MuJoCo model: table + fixed exoskeleton + human right arm.

The scene follows the static table writing of MyoSuite/RoboHive
``myoarm_relocate.xml`` (robohive commit 3397e80): a static ``body`` holding
box geoms with ``contype``/``conaffinity`` for the collision surface. The
MyoArm muscles and the relocate task are not used.

All physical numbers come from ``model/params.json``. Controller numbers are
not duplicated here; they stay in the firmware (``App/application/s5_site.c``).

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\model\\build_model.py
Writes build/sim/arm_exo_table.xml and prints the derived geometry.
"""

from __future__ import annotations

import json
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
PARAMS_PATH = SIM_ROOT / "model" / "params.json"
OUT_PATH = FW_ROOT / "build" / "sim" / "arm_exo_table.xml"


def load_params(path: Path = PARAMS_PATH) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def _v(entry: dict) -> float:
    """Value of a params.json leaf ({"value": ..., "status": ...})."""
    return float(entry["value"])


def derive(params: dict) -> dict:
    """Turn params.json into concrete lengths, masses and scene positions."""
    table = params["table"]
    mount = params["mount"]
    exo = params["exo"]
    human = params["human"]

    height = _v(human["height_m"])
    mass = _v(human["body_mass_kg"])

    def segment(node: dict) -> dict:
        length = _v(node["length_fraction_of_height"]) * height
        return {
            "length": length,
            "mass": _v(node["mass_fraction_of_body"]) * mass,
            "com": _v(node["com_fraction_from_proximal"]) * length,
            "radius": _v(node["radius_m"]),
        }

    upper = segment(human["upper_arm"])
    forearm = segment(human["forearm"])
    hand = segment(human["hand"])

    exo_upper_len = _v(exo["upper_link_m"])
    exo_fore_len = _v(exo["forearm_link_m"])
    edge_x = _v(table["edge_x_m"])
    column = _v(mount["column_height_m"])
    offset = _v(mount["shoulder_offset_x_m"])

    strap_upper = _v(params["straps"]["upper_arm"]["distance_from_shoulder_m"])
    strap_fore_h = _v(params["straps"]["forearm"]["human_distance_from_elbow_m"])
    strap_fore_e = _v(params["straps"]["forearm"]["exo_distance_from_elbow_m"])
    if strap_upper > exo_upper_len or strap_upper > upper["length"]:
        raise ValueError("upper strap lies outside one of the upper links")
    if strap_fore_e > exo_fore_len or strap_fore_h > forearm["length"]:
        raise ValueError("forearm strap lies outside one of the forearm links")

    # The exoskeleton runs along the outside of the arm (user SolidWorks view
    # 2026-10-26), so its plane is offset in y and the cuffs must not overlap
    # the arm. The joint axes stay parallel to y, so the sagittal dynamics are
    # unchanged; only the band geometry sees the offset.
    offset_y = _v(exo["lateral_offset_y_m"])
    if offset_y < upper["radius"] + _v(exo["upper_radius_m"]):
        raise ValueError(
            f"exoskeleton offset {offset_y:.3f} m would intersect the arm "
            f"(needs > {upper['radius'] + _v(exo['upper_radius_m']):.3f} m)")
    # The marker sites are visual only (the band is a joint-angle coupling),
    # so the forearm marker is kept level with the human one at q = 0.
    strap_fore_expected = upper["length"] + strap_fore_h - exo_upper_len
    if abs(strap_fore_expected - strap_fore_e) > 1e-9:
        raise ValueError(
            f"forearm cuff markers are not level at q=0: params say "
            f"{strap_fore_e:.4f} m, geometry needs {strap_fore_expected:.4f} m")

    return {
        "table_height": _v(table["height_m"]),
        "table_thickness": _v(table["top_thickness_m"]),
        "table_length": _v(table["top_length_m"]),
        "table_width": _v(table["top_width_m"]),
        "table_collision": bool(table["collision_enabled"]["value"]),
        "edge_x": edge_x,
        "column_height": column,
        "column_radius": _v(mount["column_radius_m"]),
        "shoulder_offset": offset,
        "shoulder_x": edge_x - offset,
        "shoulder_z": _v(table["height_m"]) + column,
        "exo_upper": {
            "length": exo_upper_len,
            "mass": _v(exo["upper_mass_kg"]),
            "com": _v(exo["upper_com_fraction"]) * exo_upper_len,
            "radius": _v(exo["upper_radius_m"]),
        },
        "exo_fore": {
            "length": exo_fore_len,
            "mass": _v(exo["forearm_mass_kg"]),
            "com": _v(exo["forearm_com_fraction"]) * exo_fore_len,
            "radius": _v(exo["forearm_radius_m"]),
        },
        "exo_motor": {
            "mass": _v(exo["elbow_motor_mass_kg"]),
            "radius": 0.035,  # visual only, the motor sits on the elbow axis
        },
        "human_upper": upper,
        "human_fore": forearm,
        "human_hand": hand,
        "strap_upper": strap_upper,
        "strap_fore_h": strap_fore_h,
        "strap_fore_e": strap_fore_e,
        "offset_y": offset_y,
        "strap_solref": (
            _v(params["straps"]["band"]["solref_timeconst_s"]),
            _v(params["straps"]["band"]["solref_dampratio"]),
        ),
        "gravity": _v(params["meta"]["gravity_m_s2"]),
        "timestep": _v(params["meta"]["timestep_s"]),
    }


def _rod_inertia(mass: float, length: float, radius: float) -> tuple[float, float]:
    """(transverse, axial) inertia of a uniform rod with a capsule skin.

    The exact capsule inertia differs by a few percent; gravity torque and
    slow motion do not depend on it. Documented as an approximation.
    """
    transverse = mass * length * length / 12.0
    axial = 0.5 * mass * radius * radius
    return max(transverse, 1e-6), max(axial, 1e-6)


def _inertial(mass: float, length: float, radius: float, com: float) -> str:
    transverse, axial = _rod_inertia(mass, length, radius)
    return (
        f'      <inertial pos="0 0 {-com:.6f}" mass="{mass:.6f}" '
        f'diaginertia="{transverse:.6f} {transverse:.6f} {axial:.6f}"/>'
    )


def build_xml(d: dict) -> str:
    g = d["gravity"]
    dt = d["timestep"]
    eu = d["exo_upper"]
    ef = d["exo_fore"]
    em = d["exo_motor"]
    hu = d["human_upper"]
    hf = d["human_fore"]
    hh = d["human_hand"]
    solref = "%.4f %.4f" % d["strap_solref"]

    table_center_x = d["edge_x"] + d["table_length"] / 2.0
    table_center_z = d["table_height"] - d["table_thickness"] / 2.0
    pedestal_half = (d["table_height"] - d["table_thickness"]) / 2.0
    post_half = d["column_height"] / 2.0
    # The desk is the mount in M1; its collision surface is optional because a
    # straight forward sweep would pass through it (see params.json).
    table_contact = "1" if d["table_collision"] else "0"

    # Sagittal plane x-z, hinge axis -y so a positive angle is forward flexion
    # (+x), matching the firmware zero (hanging arm) and sign convention.
    axis = "0 -1 0"

    return f"""<mujoco model="arm_exo_table">
  <!-- Generated by sim/mujoco_arm/model/build_model.py from params.json.
       M1 scene: static table, fixed exoskeleton, human right arm, two soft
       straps. No controller is connected yet (that is M2b). -->
  <compiler angle="radian" autolimits="true"/>
  <option timestep="{dt:g}" gravity="0 0 {-g:g}" integrator="implicitfast"
          cone="elliptic" impratio="10"/>
  <statistic center="{d['shoulder_x']:.4f} -0.05 {d['shoulder_z'] - 0.25:.4f}" extent="1.0"/>
  <visual>
    <global offwidth="1280" offheight="720"/>
    <headlight ambient="0.4 0.4 0.4" diffuse="0.6 0.6 0.6"/>
  </visual>
  <worldbody>
    <light pos="0 0 3" dir="0 0 -1" directional="true"/>
    <geom name="floor" type="plane" size="2 2 0.1" rgba="0.85 0.85 0.85 1"
          contype="{table_contact}" conaffinity="{table_contact}"/>
    <!-- Table: static, holds the exoskeleton reaction forces (see
         myoarm_relocate.xml for the box table writing). -->
    <body name="table" pos="{table_center_x:.6f} 0 {table_center_z:.6f}">
      <geom name="table_top" type="box" size="{d['table_length']/2:.6f} {d['table_width']/2:.6f} {d['table_thickness']/2:.6f}"
            rgba="0.78 0.70 0.58 1" contype="{table_contact}" conaffinity="{table_contact}"/>
      <geom name="table_pedestal" type="box" size="0.25 0.25 {pedestal_half:.6f}"
            pos="0 0 {-d['table_height']/2:.6f}" rgba="0.55 0.47 0.38 1"
            contype="{table_contact}" conaffinity="{table_contact}"/>
    </body>
    <!-- Mount: vertical post on the table edge plus a short bracket, so the
         exoskeleton shoulder axis sits at seat shoulder height above the desk
         (D4: one number keeps both aligned). -->
    <body name="mount" pos="{d['edge_x']:.6f} {d['offset_y']:.6f} {d['table_height']:.6f}">
      <geom name="post" type="cylinder" size="{d['column_radius']:.6f} {post_half:.6f}"
            pos="0 0 {post_half:.6f}" rgba="0.35 0.35 0.38 1" contype="0" conaffinity="0"/>
      <geom name="bracket" type="capsule" fromto="0 0 {d['column_height']:.6f} {-d['shoulder_offset']:.6f} 0 {d['column_height']:.6f}"
            size="0.015" rgba="0.35 0.35 0.38 1" contype="0" conaffinity="0"/>
      <!-- Exoskeleton: shoulder hinge -> upper link -> elbow hinge + M3508 ->
           forearm link. Masses are the 3MF/ABS estimates from params.json. -->
      <body name="exo_upper" pos="{-d['shoulder_offset']:.6f} 0 {d['column_height']:.6f}">
        <joint name="exo_shoulder" type="hinge" axis="{axis}" damping="0.02"/>
{_inertial(eu['mass'], eu['length'], eu['radius'], eu['com'])}
        <geom name="exo_upper_geom" type="capsule" fromto="0 0 0 0 0 {-eu['length']:.6f}"
              size="{eu['radius']:.6f}" mass="{eu['mass']:.6f}" rgba="0.20 0.45 0.75 1"
              contype="0" conaffinity="0"/>
        <site name="strap_upper_exo" type="sphere" size="0.012" rgba="1 0.5 0.1 1"
              pos="0 0 {-d['strap_upper']:.6f}"/>
        <body name="exo_elbow" pos="0 0 {-eu['length']:.6f}">
          <joint name="exo_elbow" type="hinge" axis="{axis}" damping="0.02"/>
          <inertial pos="0 0 0" mass="{em['mass']:.6f}" diaginertia="0.0002 0.0002 0.0002"/>
          <geom name="exo_motor_geom" type="cylinder" size="{em['radius']:.6f} 0.025"
                euler="1.5707963 0 0" mass="{em['mass']:.6f}" rgba="0.15 0.15 0.15 1"
                contype="0" conaffinity="0"/>
          <body name="exo_fore">
{_inertial(ef['mass'], ef['length'], ef['radius'], ef['com'])}
            <geom name="exo_fore_geom" type="capsule" fromto="0 0 0 0 0 {-ef['length']:.6f}"
                  size="{ef['radius']:.6f}" mass="{ef['mass']:.6f}" rgba="0.20 0.45 0.75 1"
                  contype="0" conaffinity="0"/>
            <site name="strap_fore_exo" type="sphere" size="0.012" rgba="1 0.5 0.1 1"
                  pos="0 0 {-d['strap_fore_e']:.6f}"/>
          </body>
        </body>
      </body>
    </body>
    <!-- Human: fixed trunk at the same shoulder axis, upper arm, forearm,
         hand. Collides with the table only (contype 2 / conaffinity 1). -->
    <body name="human_trunk" pos="{d['shoulder_x']:.6f} 0 {d['shoulder_z']:.6f}">
      <geom name="trunk_geom" type="box" size="0.09 0.12 0.18" pos="-0.10 0 0.05"
            rgba="0.9 0.75 0.65 1" contype="0" conaffinity="0"/>
      <body name="human_upper" pos="0 0 0">
        <joint name="human_shoulder" type="hinge" axis="{axis}" damping="0.02"/>
{_inertial(hu['mass'], hu['length'], hu['radius'], hu['com'])}
        <geom name="human_upper_geom" type="capsule" fromto="0 0 0 0 0 {-hu['length']:.6f}"
              size="{hu['radius']:.6f}" mass="{hu['mass']:.6f}" rgba="0.85 0.62 0.50 1"
              contype="2" conaffinity="1"/>
        <site name="strap_upper_human" type="sphere" size="0.012" rgba="0.1 0.8 0.2 1"
              pos="0 {d['offset_y']:.6f} {-d['strap_upper']:.6f}"/>
        <body name="human_fore" pos="0 0 {-hu['length']:.6f}">
          <joint name="human_elbow" type="hinge" axis="{axis}" damping="0.02"/>
{_inertial(hf['mass'], hf['length'], hf['radius'], hf['com'])}
          <geom name="human_fore_geom" type="capsule" fromto="0 0 0 0 0 {-hf['length']:.6f}"
                size="{hf['radius']:.6f}" mass="{hf['mass']:.6f}" rgba="0.85 0.62 0.50 1"
                contype="2" conaffinity="1"/>
          <site name="strap_fore_human" type="sphere" size="0.012" rgba="0.1 0.8 0.2 1"
                pos="0 {d['offset_y']:.6f} {-d['strap_fore_h']:.6f}"/>
          <body name="human_hand" pos="0 0 {-hf['length']:.6f}">
{_inertial(hh['mass'], hh['length'], hh['radius'], hh['com'])}
            <geom name="human_hand_geom" type="capsule" fromto="0 0 0 0 0 {-hh['length']:.6f}"
                  size="{hh['radius']:.6f}" mass="{hh['mass']:.6f}" rgba="0.85 0.62 0.50 1"
                  contype="2" conaffinity="1"/>
          </body>
        </body>
      </body>
    </body>
  </worldbody>
  <!-- Two bands. Each one keeps the exoskeleton joint angle equal to the arm
       joint angle (1:1), which is what a well-fitted cuff does: the band
       absorbs the axial mismatch (the exoskeleton elbow axis sits 5.5 cm above
       the human elbow, so two point attachments cannot stay coincident) and
       leaves the arm free to bend. The constraint also carries the
       exoskeleton weight back to the arm, which is the C1 effect. A point
       attachment is deliberately NOT used: with that 5.5 cm offset it becomes
       infeasible past a small elbow angle and fights the human. -->
  <equality>
    <joint name="strap_upper" joint1="human_shoulder" joint2="exo_shoulder"
           polycoef="0 1 0 0 0" solref="{solref}" solimp="0.95 0.99 0.001"/>
    <joint name="strap_fore" joint1="human_elbow" joint2="exo_elbow"
           polycoef="0 1 0 0 0" solref="{solref}" solimp="0.95 0.99 0.001"/>
  </equality>
  <!-- Joint torque actuators, 1:1 with the joint. The firmware maps shoulder
       rotor Nm x8 and elbow current x kt; that mapping lives in the M2b
       bridge, not here. -->
  <actuator>
    <motor name="exo_shoulder_motor" joint="exo_shoulder" gear="1" ctrlrange="-50 50"/>
    <motor name="exo_elbow_motor" joint="exo_elbow" gear="1" ctrlrange="-50 50"/>
    <motor name="human_shoulder_motor" joint="human_shoulder" gear="1" ctrlrange="-50 50"/>
    <motor name="human_elbow_motor" joint="human_elbow" gear="1" ctrlrange="-50 50"/>
  </actuator>
  <sensor>
    <jointpos name="exo_shoulder_pos" joint="exo_shoulder"/>
    <jointpos name="exo_elbow_pos" joint="exo_elbow"/>
    <jointpos name="human_shoulder_pos" joint="human_shoulder"/>
    <jointpos name="human_elbow_pos" joint="human_elbow"/>
    <jointvel name="exo_shoulder_vel" joint="exo_shoulder"/>
    <jointvel name="exo_elbow_vel" joint="exo_elbow"/>
    <jointvel name="human_shoulder_vel" joint="human_shoulder"/>
    <jointvel name="human_elbow_vel" joint="human_elbow"/>
    <actuatorfrc name="exo_shoulder_frc" actuator="exo_shoulder_motor"/>
    <actuatorfrc name="exo_elbow_frc" actuator="exo_elbow_motor"/>
    <actuatorfrc name="human_shoulder_frc" actuator="human_shoulder_motor"/>
    <actuatorfrc name="human_elbow_frc" actuator="human_elbow_motor"/>
  </sensor>
</mujoco>
"""


def write_model(d: dict | None = None, out_path: Path = OUT_PATH) -> Path:
    d = d or derive(load_params())
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(build_xml(d), encoding="utf-8")
    return out_path


def main() -> None:
    params = load_params()
    d = derive(params)
    path = write_model(d)
    hu, hf, hh = d["human_upper"], d["human_fore"], d["human_hand"]
    print(f"wrote {path}")
    print(f"table {d['table_height']:.2f} m high, shoulder axis at "
          f"x={d['shoulder_x']:.3f} z={d['shoulder_z']:.3f} m")
    print(f"exoskeleton: upper {d['exo_upper']['length']:.3f} m / "
          f"{d['exo_upper']['mass']:.3f} kg, forearm {d['exo_fore']['length']:.3f} m / "
          f"{d['exo_fore']['mass']:.3f} kg, elbow motor {d['exo_motor']['mass']:.3f} kg")
    print(f"human: upper {hu['length']:.3f} m / {hu['mass']:.3f} kg, "
          f"forearm {hf['length']:.3f} m / {hf['mass']:.3f} kg, "
          f"hand {hh['length']:.3f} m / {hh['mass']:.3f} kg")


if __name__ == "__main__":
    main()
