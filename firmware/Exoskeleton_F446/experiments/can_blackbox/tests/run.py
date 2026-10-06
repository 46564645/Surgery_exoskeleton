"""Offline tests of the real recorder/wrappers with register/HAL mocks. No device I/O."""
from pathlib import Path
import os
import shutil
import subprocess

folder = Path(__file__).resolve().parents[1]
root = folder.parents[1]
out = root / "build" / "can-blackbox-tests"
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
sources = [folder / "recorder.c", folder / "stm32_capture.c", folder / "tests/test_blackbox.c"]
includes = [folder / "tests", folder, root / "Core/Inc"]
if os.name == "nt":
    if not shutil.which("cl", path=env.get("PATH")):
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        vcvars = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-find", "VC/Auxiliary/Build/vcvars64.bat"], text=True).splitlines()[0]
        setup = out / "msvc_env.cmd"
        setup.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b %errorlevel%\nset\n', encoding="utf-8")
        result = subprocess.check_output(["cmd.exe", "/d", "/c", str(setup)], text=True, errors="replace")
        env.update(line.split("=", 1) for line in result.splitlines() if "=" in line and not line.startswith("="))
    compiler = shutil.which("cl", path=env.get("Path", env.get("PATH")))
    exe = out / "test.exe"
    cmd = [compiler, "/nologo", "/std:c11", "/W4", "/WX", "/D_CRT_SECURE_NO_WARNINGS", f"/Fe:{exe}"]
    cmd += [f"/I{p}" for p in includes]
else:
    exe = out / "test"
    cmd = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", str(exe)]
    cmd += [f"-I{p}" for p in includes]
subprocess.run(cmd + list(map(str, sources)), cwd=out, env=env, check=True)
subprocess.run([str(exe)], cwd=out, check=True)
