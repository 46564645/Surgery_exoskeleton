#!/usr/bin/env python3
"""Build the software-in-the-loop shared library from the firmware sources.

The library links the real control code, not a copy: App/control/*.c plus
s5_site.c (the site profile) and the two small self-contained modules the
profile validation references. build_bridge.py only adds the compiler command
and the R5 build options, exactly like the firmware CMake options.

Windows: MSVC, same discovery as tests/run_host_tests.py -> sim_bridge.dll
macOS:   clang -dynamiclib                                -> sim_bridge.dylib

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\build_bridge.py
    ... --gain 0.6 --ma 2000 --sign 1        # C3 gain sweep / other R5 builds
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
OUT_DIR = FW_ROOT / "build" / "sim"
OBJ_DIR = OUT_DIR / "bridge-obj"

SOURCES = [
    "App/control/joint_state.c",
    "App/control/gravity_compensation.c",
    "App/control/s5_elbow_support.c",
    "App/control/shoulder_amplify.c",
    "App/control/force_assist.c",
    "App/control/s5_assist_compose.c",
    "App/control/safety_supervisor.c",
    "App/application/shoulder_standstill.c",
    "App/application/s5_site.c",
    "sim/mujoco_arm/bridge/sim_bridge.c",
    "sim/mujoco_arm/bridge/site_stub.c",
]

INCLUDES = ["tests/host", "Core/Inc", "App/application", "App/devices", "App/control"]


def build_defines(args: argparse.Namespace) -> list[str]:
    """Mirror the firmware CMake options; nothing here is a new control value."""
    return [
        "S5_R5_DUAL_FOLLOW",
        f"S5_R5_ELBOW_SUPPORT_SIGN={args.sign}",
        f"S5_R5_ELBOW_SUPPORT_MA={args.ma}",
        f"S5_R5_SHOULDER_AMPLIFY_MNM={args.amplify_mnm}",
        f"S5_R5_SHOULDER_GAIN={args.gain}",
        f"S5_R5_SHOULDER_CAP_MNM={args.cap_mnm}",
    ] + (["S5_R5_FORCE_ASSIST", f"S5_R5_FORCE_BOOST_PCT={args.boost_pct}"]
         if args.force_assist else [])


def msvc_env() -> dict:
    env = os.environ.copy()
    if shutil.which("cl", path=env.get("PATH")) is not None:
        return env
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    paths = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-find",
                                    "VC/Auxiliary/Build/vcvars64.bat"], text=True).splitlines()
    if not paths:
        raise SystemExit("MSVC C tools unavailable; install them or run from a VS developer shell")
    setup = OBJ_DIR / "msvc_env.cmd"
    setup.write_text(f'@echo off\ncall "{paths[0]}" >nul\n'
                     'if errorlevel 1 exit /b %errorlevel%\nset\n', encoding="utf-8")
    output = subprocess.check_output(["cmd.exe", "/d", "/c", str(setup)],
                                     text=True, errors="replace")
    env.update(line.split("=", 1) for line in output.splitlines()
               if "=" in line and not line.startswith("="))
    return env


def build_windows(defines: list[str], out: Path) -> None:
    env = msvc_env()
    compiler = shutil.which("cl", path=env.get("Path", env.get("PATH")))
    if compiler is None:
        raise SystemExit("MSVC environment did not provide cl.exe")
    cmd = [compiler, "/nologo", "/std:c11", "/experimental:c11atomics", "/W3", "/WX", "/O2",
           "/D_CRT_SECURE_NO_WARNINGS", "/LD", f"/Fe:{out}", f"/Fo:{OBJ_DIR}{os.sep}",
           f"/Fd:{OBJ_DIR}{os.sep}bridge.pdb",
           # CMake string options arrive as double literals (0.6), which the
           # site profile stores in floats; GCC accepts this silently.
           "/wd4305"]
    cmd += [f"/D{d}" for d in defines]
    cmd += [f"/I{i}" for i in INCLUDES]
    subprocess.run(cmd + SOURCES, cwd=FW_ROOT, check=True, env=env)


def build_macos(defines: list[str], out: Path) -> None:
    compiler = os.environ.get("HOST_CC", "cc")
    cmd = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-fPIC",
           "-dynamiclib", "-o", str(out)]
    cmd += [f"-D{d}" for d in defines]
    cmd += [f"-I{i}" for i in INCLUDES]
    subprocess.run(cmd + SOURCES, cwd=FW_ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gain", default="1.5",
                        help="S5_R5_SHOULDER_GAIN (0.001..3.0); 1.5 = the 2026-10-27 "
                             "production choice")
    parser.add_argument("--cap-mnm", default="300",
                        help="S5_R5_SHOULDER_CAP_MNM (1..575); 575 = GDS68 rating / 8, "
                             "300 = 2.4 N*m joint (production choice)")
    parser.add_argument("--ma", default="10000",
                        help="S5_R5_ELBOW_SUPPORT_MA (1..20000); 10000 = the 2026-10-27 "
                             "production choice (10 A, M3508 rated)")
    parser.add_argument("--sign", default="1", help="S5_R5_ELBOW_SUPPORT_SIGN (1 or -1)")
    parser.add_argument("--amplify-mnm", default="0", help="S5_R5_SHOULDER_AMPLIFY_MNM (0..100)")
    parser.add_argument("--force-assist", action="store_true", help="S5_R5_FORCE_ASSIST")
    parser.add_argument("--boost-pct", default="50", help="S5_R5_FORCE_BOOST_PCT (1..50)")
    parser.add_argument("--out", default=None, help="output library path")
    args = parser.parse_args()

    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    suffix = ".dll" if os.name == "nt" else ".dylib"
    out = Path(args.out) if args.out else OUT_DIR / f"sim_bridge{suffix}"
    defines = build_defines(args)
    if os.name == "nt":
        build_windows(defines, out)
    else:
        build_macos(defines, out)
    print(f"wrote {out}")
    print(f"  defines: {' '.join(defines)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
