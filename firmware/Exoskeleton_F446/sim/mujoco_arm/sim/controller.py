"""ctypes wrapper around the firmware control library (M2b).

The shared library is built from the real firmware sources by
``scripts/build_bridge.py``. This module only marshals numbers; it never
re-implements a control formula, and it reads the mapping constants
(8:1 shoulder, 0.02 Nm/A elbow, caps, periods) back from the library instead
of declaring them here.

Units: joint radians and rad/s in, shoulder rotor Nm and elbow C620 amps out.
The conversion to joint torque is done by the simulation's actuator model
(M3), which multiplies by the site numbers reported here.
"""

from __future__ import annotations

import ctypes
import os
from dataclasses import dataclass
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
LIB_NAME = "sim_bridge.dll" if os.name == "nt" else "sim_bridge.dylib"
DEFAULT_LIB = FW_ROOT / "build" / "sim" / LIB_NAME


class _Input(ctypes.Structure):
    _fields_ = [
        ("shoulder_pos_rad", ctypes.c_float),
        ("shoulder_vel_rad_s", ctypes.c_float),
        ("elbow_pos_rad", ctypes.c_float),
        ("elbow_vel_rad_s", ctypes.c_float),
        ("force_n", ctypes.c_float),
        ("now_ms", ctypes.c_uint32),
        ("shoulder_valid", ctypes.c_int32),
        ("elbow_valid", ctypes.c_int32),
        ("assist_enabled", ctypes.c_int32),
        ("force_valid", ctypes.c_int32),
    ]


class _Output(ctypes.Structure):
    _fields_ = [
        ("shoulder_rotor_nm", ctypes.c_float),
        ("elbow_current_a", ctypes.c_float),
        ("ok", ctypes.c_int32),
    ]


class _Site(ctypes.Structure):
    _fields_ = [
        ("shoulder_rotor_per_joint", ctypes.c_float),
        ("shoulder_rotor_zero_rad", ctypes.c_float),
        ("shoulder_direction", ctypes.c_int32),
        ("elbow_rotor_per_joint", ctypes.c_float),
        ("elbow_rotor_zero_rad", ctypes.c_float),
        ("elbow_direction", ctypes.c_int32),
        ("elbow_motor_kt_nm_per_a", ctypes.c_float),
        ("shoulder_gain", ctypes.c_float),
        ("shoulder_gravity_amplitude_nm", ctypes.c_float),
        ("elbow_gravity_amplitude_nm", ctypes.c_float),
        ("shoulder_cap_rotor_nm", ctypes.c_float),
        ("elbow_cap_a", ctypes.c_float),
        ("soft_speed_rad_s", ctypes.c_float * 2),
        ("amplify_increment_rotor_nm", ctypes.c_float),
        ("send_interval_ms", ctypes.c_uint32),
        ("session_ms", ctypes.c_uint32),
        ("feedback_timeout_ms", ctypes.c_uint32),
        ("shoulder_property_period_ms", ctypes.c_uint32),
        ("force_freshness_ms", ctypes.c_uint32),
        ("dual", ctypes.c_int32),
        ("force_confirmed", ctypes.c_int32),
    ]


@dataclass(frozen=True)
class Command:
    """One controller output, in the firmware's own units.

    ``shoulder_rotor_nm`` is rotor-side; multiply by
    ``Controller.site["shoulder_rotor_per_joint"]`` for joint Nm (M3 does this).
    ``elbow_current_a`` is the C620 command; multiply by
    ``Controller.site["elbow_motor_kt_nm_per_a"]`` for joint Nm.
    """

    shoulder_rotor_nm: float
    elbow_current_a: float
    ok: bool


class Controller:
    def __init__(self, lib_path: Path | str = DEFAULT_LIB) -> None:
        path = Path(lib_path)
        if not path.exists():
            raise FileNotFoundError(
                f"{path} not found; run sim/mujoco_arm/scripts/build_bridge.py first")
        self._lib = ctypes.CDLL(str(path))
        self._lib.SimBridge_Reset.argtypes = [ctypes.c_uint32]
        self._lib.SimBridge_Reset.restype = None
        self._lib.SimBridge_Step.argtypes = [ctypes.POINTER(_Input), ctypes.POINTER(_Output)]
        self._lib.SimBridge_Step.restype = ctypes.c_int32
        self._lib.SimBridge_GetSite.argtypes = [ctypes.POINTER(_Site)]
        self._lib.SimBridge_GetSite.restype = None
        self._lib.SimBridge_Sizes.argtypes = [ctypes.POINTER(ctypes.c_int32)] * 3
        self._lib.SimBridge_Sizes.restype = None
        self._check_layout()
        self.site = self._read_site()
        self.reset(0)

    def _check_layout(self) -> None:
        sizes = [ctypes.c_int32() for _ in range(3)]
        self._lib.SimBridge_Sizes(*[ctypes.byref(size) for size in sizes])
        reported = tuple(size.value for size in sizes)
        expected = (ctypes.sizeof(_Input), ctypes.sizeof(_Output), ctypes.sizeof(_Site))
        if reported != expected:
            raise RuntimeError(
                f"struct layout mismatch: C {reported} vs ctypes {expected}")

    def _read_site(self) -> dict:
        site = _Site()
        self._lib.SimBridge_GetSite(ctypes.byref(site))
        return {
            "shoulder_rotor_per_joint": site.shoulder_rotor_per_joint,
            "shoulder_rotor_zero_rad": site.shoulder_rotor_zero_rad,
            "shoulder_direction": site.shoulder_direction,
            "elbow_rotor_per_joint": site.elbow_rotor_per_joint,
            "elbow_rotor_zero_rad": site.elbow_rotor_zero_rad,
            "elbow_direction": site.elbow_direction,
            "elbow_motor_kt_nm_per_a": site.elbow_motor_kt_nm_per_a,
            "shoulder_gain": site.shoulder_gain,
            "shoulder_gravity_amplitude_nm": site.shoulder_gravity_amplitude_nm,
            "elbow_gravity_amplitude_nm": site.elbow_gravity_amplitude_nm,
            "shoulder_cap_rotor_nm": site.shoulder_cap_rotor_nm,
            "elbow_cap_a": site.elbow_cap_a,
            "soft_speed_rad_s": tuple(site.soft_speed_rad_s),
            "amplify_increment_rotor_nm": site.amplify_increment_rotor_nm,
            "send_interval_ms": site.send_interval_ms,
            "session_ms": site.session_ms,
            "feedback_timeout_ms": site.feedback_timeout_ms,
            "shoulder_property_period_ms": site.shoulder_property_period_ms,
            "force_freshness_ms": site.force_freshness_ms,
            "dual": bool(site.dual),
            "force_confirmed": bool(site.force_confirmed),
        }

    def reset(self, now_ms: int = 0) -> None:
        self._lib.SimBridge_Reset(now_ms)

    def step(self, *, shoulder_rad: float, shoulder_vel: float,
             elbow_rad: float, elbow_vel: float, now_ms: int,
             force_n: float = 0.0, force_valid: bool = False,
             assist_enabled: bool = True,
             shoulder_valid: bool = True, elbow_valid: bool = True) -> Command:
        data = _Input(
            shoulder_pos_rad=shoulder_rad,
            shoulder_vel_rad_s=shoulder_vel,
            elbow_pos_rad=elbow_rad,
            elbow_vel_rad_s=elbow_vel,
            force_n=force_n,
            now_ms=int(now_ms),
            shoulder_valid=int(shoulder_valid),
            elbow_valid=int(elbow_valid),
            assist_enabled=int(assist_enabled),
            force_valid=int(force_valid),
        )
        out = _Output()
        self._lib.SimBridge_Step(ctypes.byref(data), ctypes.byref(out))
        return Command(out.shoulder_rotor_nm, out.elbow_current_a, bool(out.ok))
