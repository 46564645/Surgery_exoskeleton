#!/usr/bin/env python3
"""Build real S1/S2 modules with a host HAL seam; no hardware access.

Windows: MSVC C11 (no sanitizer claim). POSIX: existing ASan/UBSan build.
"""
from pathlib import Path
import os
import subprocess
import shutil
import sys

root = Path(__file__).resolve().parents[1]
out = root / "build" / "host"
out.mkdir(parents=True, exist_ok=True)
sources = ["tests/host/test_s1.c", "Core/Src/board_console.c", "Core/Src/board_runtime.c",
           "App/application/app_main.c", "App/application/console_commands.c",
           "App/application/c620_standstill.c",
           "Core/Src/board_can.c", "App/application/can_monitor.c",
           "tests/host/can_mock.c", "tests/host/test_can.c",
           "App/devices/c620_feedback.c", "tests/host/test_c620.c",
           "App/devices/motor_command.c", "App/control/joint_state.c",
           "App/control/safety_supervisor.c", "App/application/output_gateway.c",
           "App/application/s3_bench.c", "App/application/s3_feedback.c", "tests/host/test_s3.c",
           "App/devices/gds_feedback.c", "tests/host/s3_site_mock.c",
           "App/devices/gds_sdo_read.c", "App/application/shoulder_read_diagnostic.c",
           "tests/host/test_sdo.c",
           "App/application/encoder_stream.c", "tests/host/test_encoder_stream.c",
           "App/application/shoulder_property_feedback.c", "tests/host/test_shoulder_properties.c",
           "Core/Src/board_can_tx.c", "App/application/output_can_transport.c",
           "Core/Src/board_stop_input.c", "App/application/s3_stop_input.c",
           "App/application/shoulder_standstill.c", "tests/host/test_shoulder_standstill.c",
           "tests/host/test_gateway_deadlines.c",
            "App/application/s3_console.c", "App/application/shoulder_qualification.c",
            "tests/host/test_shoulder_qualification.c",
           "App/application/s3_shoulder_service.c", "tests/host/stop_input_mock.c",
           "tests/host/test_stop_input.c", "tests/host/test_shoulder_chain.c",
           "tests/host/can_tx_mock.c",
           "third_party/lwrb/src/lwrb/lwrb.c"]
sources += [str(p.relative_to(root)) for p in sorted((root / "third_party/clip/src").glob("*.c"))]
includes = ["tests/host", "Core/Inc", "App/application", "App/devices", "App/control", "third_party/lwrb/src/include", "third_party/clip/src"]
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
cases = ["s3_chain_relative_anchor", "s3_chain_relative_rejects", "s3_chain_relative_cli", "s3_chain_relative_config",
         "s3_telemetry_cli", "s3_chain_recording", "shoulder_qual_success", "shoulder_qual_faults",
         "shoulder_qual_ownership", "shoulder_qual_can_fault", "shoulder_qual_uart_fault", "s3_shoulder_standstill", "s3_gateway_ready_deadline", "s3_gateway_first_deadline",
         "s3_gateway_shoulder_once", "s3_chain_supervised", "s3_chain_supervised_faults", "s3_stop_input", "s3_chain_mailboxes", "s3_chain_run", "s3_chain_faults",
         "s3_properties_codec", "s3_properties_association", "s3_properties_consumer",
         "s2_stream_lifecycle", "s2_stream_faults", "s2_stream_cli",
             "s2_sdo_codec", "s2_sdo_lifecycle", "s2_sdo_interlocks", "s2_sdo_cli",
             "commands", "rx_positions", "tx_queue", "backpressure", "rx_overflow", "uart_error",
             "start_error", "tx_error", "timeout", "late_poll", "timeout_race", "scheduler", "priorities",
             "can_startup", "can_isolation", "can_queue", "can_budget", "can_invalid",
             "can_filter_failure", "can_start_failure", "can_notify_failure", "can_init_failure",
             "can_errors", "can_commands", "can_failure_console",
             "c620_codec", "c620_invalid", "c620_freshness", "c620_wrap",
             "c620_integration", "c620_queue_age", "c620_console", "c620_failure_console",
             "s3_codec", "s3_mapping", "s3_default", "s3_lifecycle", "s3_interlocks", "s3_transport", "s3_bxcan", "s3_prepare", "s3_gds", "s3_cli", "s3_axes", "s3_observations", "s3_shoulder_console", "s3_standstill", "s3_bench_stop_policy", "s3_bench_stop_cli"]
selected = sys.argv[1:] or cases
if any(case not in cases for case in selected):
    raise SystemExit("Unknown host scenario")
for case in selected:
    subprocess.run([str(exe), case], cwd=root, check=True)
