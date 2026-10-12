"""Compile and run the S4 site gate probe using MSVC; no device operation."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
from pathlib import Path


PROFILE_DIR = Path(__file__).resolve().parent
FIRMWARE = PROFILE_DIR.parents[2]


def msvc_environment(out: Path) -> dict[str, str]:
    env = os.environ.copy()
    temporary = out / "tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    env.update({"TEMP": str(temporary), "TMP": str(temporary),
                "TMPDIR": str(temporary)})
    if shutil.which("cl", path=env.get("PATH")) is not None:
        return env
    # Windows normalizes keys in an os.environ.copy() snapshot on some Python
    # versions, so query this mixed-case name from os.environ directly.
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vcvars = subprocess.check_output([
        str(vswhere), "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-find",
        "VC/Auxiliary/Build/vcvars64.bat"
    ], text=True, env=env).splitlines()
    if not vcvars:
        raise RuntimeError("MSVC C tools unavailable")
    setup = out / "msvc_env.cmd"
    setup.write_text(f'@echo off\ncall "{vcvars[0]}" >nul\n'
                     'if errorlevel 1 exit /b %errorlevel%\nset\n', encoding="utf-8")
    reply = subprocess.check_output(["cmd.exe", "/d", "/c", str(setup)],
                                    text=True, errors="replace", env=env)
    env.update(line.split("=", 1) for line in reply.splitlines()
               if "=" in line and not line.startswith("="))
    return env


def read_gate_slice(source: Path, out: Path) -> Path:
    """Compile the original reader configuration function without its HAL users.

    This is a byte-for-byte source slice, not a duplicate rule or a stub. Unique
    anchors make source drift fail closed, and the source hash remains in H:.
    """
    original = (source / "App/application/s3_shoulder_service.c").read_text(encoding="utf-8")
    duration_start = original.index("static bool duration(uint32_t t)")
    bench_start = original.index("static bool bench(void)", duration_start)
    read_start = original.index("bool S3ShoulderService_ReadProfileReady(", bench_start)
    next_start = original.index("bool S3ShoulderService_ProfileReady(", read_start)
    if (original.count("bool S3ShoulderService_ReadProfileReady(") != 1
            or original.count("static bool duration(uint32_t t)") != 1):
        raise RuntimeError("Reader gate source is ambiguous")
    slice_text = ("#include \"s3_shoulder_service.h\"\n#include \"s2_config.h\"\n"
                  + original[duration_start:bench_start]
                  + original[read_start:next_start])
    result = out / "s3_read_profile_slice.c"
    result.write_text(slice_text, encoding="utf-8")
    digest = hashlib.sha256(original.encode("utf-8")).hexdigest()
    (out / "reader-source-sha256.txt").write_text(digest + "\n", encoding="ascii")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path,
                        help="Materialized trial source below firmware/build/")
    args = parser.parse_args()
    source = args.source.resolve()
    build_root = (FIRMWARE / "build").resolve()
    if FIRMWARE.drive.upper() != "H:" or build_root not in source.parents:
        parser.error("Probe source must be materialized under firmware/build/ on H:")
    if not (source / "source-manifest.json").is_file():
        parser.error("Missing materialization manifest")
    out = source / "build/host-profile"
    out.mkdir(parents=True, exist_ok=True)
    env = msvc_environment(out)
    reader_gate = read_gate_slice(source, out)
    compiler = shutil.which("cl", path=env.get("Path", env.get("PATH")))
    if compiler is None:
        raise RuntimeError("MSVC environment did not provide cl.exe")
    exe = out / "probe_profile.exe"
    includes = ("Core/Inc", "App/application", "App/control", "App/devices")
    files = (
        source / "App/application/s4_site.c",
        reader_gate,
        source / "App/application/shoulder_standstill.c",
        source / "App/control/safety_supervisor.c",
        PROFILE_DIR / "probe_profile.c",
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
        raise SystemExit(f"Probe compile failed ({result.returncode}); see {out / 'compile.log'}\n"
                         + result.stdout[-3000:])
    result = subprocess.run([str(exe)], cwd=source, env=env, text=True, errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / "run.log").write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="")
    if result.returncode != 0:
        raise SystemExit(f"Probe failed ({result.returncode}); see {out / 'run.log'}")


if __name__ == "__main__":
    main()
