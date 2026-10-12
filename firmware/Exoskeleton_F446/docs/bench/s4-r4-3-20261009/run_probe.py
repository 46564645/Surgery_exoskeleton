"""Compile R4-3's exact materialized site gate with MSVC; no device access."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parents[2]
FIRST_WINDOW = FIRMWARE / "docs/bench/s4-first-window-20261009"
sys.path.insert(0, str(FIRST_WINDOW))
from run_probe import msvc_environment, read_gate_slice  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--probe-source", type=Path, default=HERE / "probe_profile.c")
    args = parser.parse_args()
    source = args.source.resolve()
    probe_source = args.probe_source.resolve()
    if probe_source.parent != HERE or probe_source.name not in ("probe_profile.c", "probe_speed15.c", "probe_gain18.c", "probe_gain36.c", "probe_speed25.c", "probe_speed30.c", "probe_speed40.c"):
        parser.error("Probe source must be one of this R4-3 directory's fixed probes")
    build_root = (FIRMWARE / "build").resolve()
    if FIRMWARE.drive.upper() != "H:" or build_root not in source.parents:
        parser.error("Source must be an H: firmware/build/ snapshot")
    manifest = source / "source-manifest.json"
    if not manifest.is_file():
        parser.error("Missing source manifest")
    out = source / ("build/host-profile-r4-3-speed40" if probe_source.name == "probe_speed40.c"
                    else "build/host-profile-r4-3-speed30" if probe_source.name == "probe_speed30.c"
                    else "build/host-profile-r4-3-speed25" if probe_source.name == "probe_speed25.c"
                    else "build/host-profile-r4-3-gain36" if probe_source.name == "probe_gain36.c"
                    else "build/host-profile-r4-3-gain18" if probe_source.name == "probe_gain18.c"
                    else "build/host-profile-r4-3")
    out.mkdir(parents=True, exist_ok=True)
    env = msvc_environment(out)
    reader_gate = read_gate_slice(source, out)
    compiler = shutil.which("cl", path=env.get("Path", env.get("PATH")))
    if compiler is None:
        raise RuntimeError("MSVC compiler unavailable")
    exe = out / "probe_profile.exe"
    includes = ("Core/Inc", "App/application", "App/control", "App/devices")
    files = (
        source / "App/application/s4_site.c",
        reader_gate,
        source / "App/application/shoulder_standstill.c",
        source / "App/control/safety_supervisor.c",
        probe_source,
    )
    cmd = [compiler, "/nologo", "/std:c11", "/W3", "/WX", "/Gy", "/O2",
           "/D_CRT_SECURE_NO_WARNINGS", f"/Fe:{exe}", f"/Fo:{out}{os.sep}"]
    cmd += [f"/I{source / folder}" for folder in includes]
    cmd += [str(path) for path in files]
    cmd += ["/link", "/OPT:REF"]
    result = subprocess.run(cmd, cwd=source, env=env, text=True, errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / "compile.log").write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        raise SystemExit(f"Probe compile failed ({result.returncode}): {out / 'compile.log'}")
    result = subprocess.run([str(exe)], cwd=source, env=env, text=True, errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / "run.log").write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        raise SystemExit(f"Probe failed ({result.returncode}): {out / 'run.log'}")
    print(result.stdout, end="")
    print("probe_sha256=" + hashlib.sha256(probe_source.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
