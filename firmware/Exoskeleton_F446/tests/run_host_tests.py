#!/usr/bin/env python3
"""Build real S1/S2 modules with a host HAL seam; no hardware access.

Windows: MSVC C11 (no sanitizer claim). POSIX: existing ASan/UBSan build.
"""
from pathlib import Path
import os
import subprocess
import shutil

root = Path(__file__).resolve().parents[1]
out = root / "build" / "host"
out.mkdir(parents=True, exist_ok=True)
sources = ["tests/host/test_s1.c", "Core/Src/board_console.c", "Core/Src/board_runtime.c",
           "App/application/app_main.c", "App/application/console_commands.c",
           "Core/Src/board_can.c", "App/application/can_monitor.c",
           "tests/host/can_mock.c", "tests/host/test_can.c",
           "App/devices/c620_feedback.c", "tests/host/test_c620.c",
           "third_party/lwrb/src/lwrb/lwrb.c"]
sources += [str(p.relative_to(root)) for p in sorted((root / "third_party/clip/src").glob("*.c"))]
includes = ["tests/host", "Core/Inc", "App/application", "App/devices", "third_party/lwrb/src/include", "third_party/clip/src"]
env = os.environ.copy()
exe = out / ("test_s1.exe" if os.name == "nt" else "test_s1")
if os.name == "nt":
    if shutil.which("cl", path=env.get("PATH")) is None:
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        paths = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-find",
                                        "VC/Auxiliary/Build/vcvars64.bat"], text=True).splitlines()
        if not paths:
            raise SystemExit("MSVC C tools unavailable; install them or run from a VS developer shell")
        # A batch file preserves cmd quoting; Python's argv quoting would add
        # backslash escapes around the embedded vcvars path with spaces.
        setup = out / "msvc_env.cmd"
        setup.write_text(f'@echo off\ncall "{paths[0]}" >nul\n'
                         'if errorlevel 1 exit /b %errorlevel%\nset\n', encoding="utf-8")
        output = subprocess.check_output(["cmd.exe", "/d", "/c", str(setup)],
                                         text=True, errors="replace")
        env.update(line.split("=", 1) for line in output.splitlines() if "=" in line and not line.startswith("="))
    compiler = shutil.which("cl", path=env.get("Path", env.get("PATH")))
    if compiler is None:
        raise SystemExit("MSVC environment did not provide cl.exe")
    cmd = [compiler, "/nologo", "/std:c11", "/experimental:c11atomics", "/W3", "/WX", "/Zi", "/Od", "/D_CRT_SECURE_NO_WARNINGS",
           f"/Fe:{exe}", f"/Fo:{out}{os.sep}", f"/Fd:{out / 'host.pdb'}"]
    cmd += [f"/I{p}" for p in includes]
    # Only this unchanged upstream CLIP translation unit narrows double->float
    # and size_t->byte (MSVC C4244/C4267). Keep /WX for all project code.
    clip_parse = "third_party/clip/src/clip_utils_parse.c"
    subprocess.run(cmd + ["/c", "/wd4244", "/wd4267", clip_parse], cwd=root, check=True, env=env)
    sources = [p for p in sources if Path(p).as_posix() != clip_parse]
    sources.append(str(out / "clip_utils_parse.obj"))
    # Existing S1 uses size_t lengths against LwRB's uint32_t size. The target
    # is 32-bit; this x64 simulation only exercises bounded <=2048B buffers.
    console = "Core/Src/board_console.c"
    subprocess.run(cmd + ["/c", "/wd4267", console], cwd=root, check=True, env=env)
    sources.remove(console)
    sources.append(str(out / "board_console.obj"))
else:
    cmd = [os.environ.get("HOST_CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-g", "-O1", "-fsanitize=address,undefined", "-o", str(exe)]
    cmd += [f"-I{p}" for p in includes]
subprocess.run(cmd + sources, cwd=root, check=True, env=env)
for case in ["commands", "rx_positions", "tx_queue", "backpressure", "rx_overflow", "uart_error",
             "start_error", "tx_error", "timeout", "late_poll", "timeout_race", "scheduler", "priorities",
             "can_startup", "can_isolation", "can_queue", "can_budget", "can_invalid",
             "can_filter_failure", "can_start_failure", "can_notify_failure", "can_init_failure",
             "can_errors", "can_commands", "can_failure_console",
             "c620_codec", "c620_invalid", "c620_freshness", "c620_wrap",
             "c620_integration", "c620_queue_age", "c620_console", "c620_failure_console"]:
    subprocess.run([str(exe), case], cwd=root, check=True)
