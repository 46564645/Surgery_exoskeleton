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
           "App/application/elbow_session_angle.c",
           "Core/Src/board_can.c", "App/application/can_monitor.c",
           "tests/host/can_mock.c", "tests/host/test_can.c",
           "App/devices/c620_feedback.c", "tests/host/test_c620.c",
           "App/devices/motor_command.c", "App/control/joint_state.c",
           "App/control/gravity_compensation.c", "tests/host/test_s4_gravity.c",
           "App/control/s5_elbow_support.c", "App/application/s5_site.c",
           "App/application/s5_follow.c",
           "tests/host/test_s5_follow.c",
           "App/application/s4_site.c", "tests/host/test_s4_site.c",
           "tests/host/test_s4_reader.c",
           "tests/host/test_s4_observe.c", "App/application/s4_follow.c",
           "tests/host/test_s4_chain.c",
           "tests/host/test_s4_gateway.c",
           "tests/host/test_s5_gateway.c",
           "tests/host/test_s4_follow_output.c",
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
           "App/devices/hx711.c", "tests/host/test_hx711.c",
           "third_party/lwrb/src/lwrb/lwrb.c"]
sources += [str(p.relative_to(root)) for p in sorted((root / "third_party/clip/src").glob("*.c"))]
includes = ["tests/host", "Core/Inc", "App/application", "App/devices", "App/control", "third_party/lwrb/src/include", "third_party/clip/src"]
env = os.environ.copy()
r4_sign = env.get("S5_R4_HOST_SIGN")
r4_ma = env.get("S5_R4_HOST_MA")
r4_defines = []
if r4_sign is not None or r4_ma is not None:
    if r4_sign not in ("1", "-1") or r4_ma is None or not r4_ma.isdecimal() \
            or not 1 <= int(r4_ma) <= 4000:
        raise SystemExit("S5 R4 host profile requires sign=1/-1 and magnitude=1..4000 mA")
    r4_defines = ["S5_R4_ELBOW_FOLLOW", f"S5_R4_ELBOW_SUPPORT_SIGN={r4_sign}",
                  f"S5_R4_ELBOW_SUPPORT_MA={r4_ma}"]
r5_sign = env.get("S5_R5_HOST_SIGN")
r5_ma = env.get("S5_R5_HOST_MA")
r5_defines = []
if r5_sign is not None or r5_ma is not None:
    if r5_sign not in ("1", "-1") or r5_ma is None or not r5_ma.isdecimal() \
            or not 1 <= int(r5_ma) <= 20000:
        raise SystemExit("S5 R5 host profile requires sign=1/-1 and target=1..20000 mA")
    r5_defines = ["S5_R5_DUAL_FOLLOW", f"S5_R5_ELBOW_SUPPORT_SIGN={r5_sign}",
                  f"S5_R5_ELBOW_SUPPORT_MA={r5_ma}"]
if r4_defines and r5_defines:
    raise SystemExit("S5 R4 and R5 host profiles are mutually exclusive")
site_defines = r4_defines + r5_defines
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
    cmd = [compiler, "/nologo", "/std:c11", "/experimental:c11atomics", "/W3", "/WX", "/Zi", "/Od", "/D_CRT_SECURE_NO_WARNINGS", "/DS4_SITE_HOST_TEST", "/DS5_SITE_HOST_TEST",
           f"/Fe:{exe}", f"/Fo:{out}{os.sep}", f"/Fd:{out / 'host.pdb'}"]
    cmd += [f"/D{definition}" for definition in site_defines]
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
    cmd = [os.environ.get("HOST_CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-DS4_SITE_HOST_TEST", "-DS5_SITE_HOST_TEST",
           "-g", "-O1", "-fsanitize=address,undefined", "-o", str(exe)]
    cmd += [f"-D{definition}" for definition in site_defines]
    cmd += [f"-I{p}" for p in includes]
subprocess.run(cmd + sources, cwd=root, check=True, env=env)
cases = ["s3_chain_relative_anchor", "s3_chain_relative_rejects", "s3_chain_relative_cli", "s3_chain_relative_config",
         "s4_gravity",
         "s4_site",
         "s4_reader_claim",
         "s4_reader_mode",
         "s4_read_status_cli",
         "s4_gateway_ramp", "s4_gateway_expiry", "s4_gateway_faults",
         "s5_gateway_pair", "s5_gateway_stop",
         "s5_model_site", "s5_elbow_follow", "s5_dual_follow",
         "s4_follow_cli_cancel", "s4_follow_expiry", "s4_follow_long_warning", "s4_follow_midcycle_arm",
         "s4_follow_read",
         "s4_observe_raw_mapping", "s4_observe_age_fault",
         "s4_chain_complete_fault", "s4_chain_cancel_pending", "s4_chain_cancel_delayed_abort", "s4_chain_alst_retry",
         "s4_chain_alst_failures", "s4_chain_alst_stray",
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
             "hx711_poll", "hx711_console",
              "s3_codec", "s3_mapping", "s3_default", "s3_lifecycle", "s3_interlocks", "s3_transport", "s3_bxcan", "s3_prepare", "s3_gds", "s3_cli", "s3_axes", "s3_observations", "s3_shoulder_console", "s3_standstill", "s3_elbow_session_angle", "s3_elbow_session_bench", "s3_bench_stop_policy", "s3_bench_stop_cli"]
if r4_defines:
    cases.append("s5_r4_site")
if r5_defines:
    cases += ["s5_r5_site", "s5_r5_dual_two_pair_zero", "s5_r5_dual_no_post_zero_dwell",
               "s5_r5_dual_cancel_early", "s5_r5_dual_cancel_pending",
               "s5_r5_dual_chain", "s5_r5_dual_elbow_stale", "s5_r5_dual_elbow_step",
               "s5_r5_dual_can_history",
               "s5_r5_dual_upper_boundary", "s5_r5_dual_soft_pause",
               "s5_r5_dual_hard_speed", "s5_r5_dual_stop_settling",
               "s5_r5_dual_elbow_lower_margin"]
selected = sys.argv[1:] or cases
if any(case not in cases for case in selected):
    raise SystemExit("Unknown host scenario")
for case in selected:
    subprocess.run([str(exe), case], cwd=root, check=True)
