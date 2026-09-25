#!/usr/bin/env python3
"""Build real S1 modules with a host HAL seam; no hardware access."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / "build" / "host"
out.mkdir(parents=True, exist_ok=True)
sources = ["tests/host/test_s1.c", "Core/Src/board_console.c", "Core/Src/board_runtime.c",
           "App/application/app_main.c", "App/application/console_commands.c",
           "third_party/lwrb/src/lwrb/lwrb.c"]
sources += [str(p.relative_to(root)) for p in sorted((root / "third_party/clip/src").glob("*.c"))]
includes = ["tests/host", "Core/Inc", "App/application", "third_party/lwrb/src/include", "third_party/clip/src"]
cmd = [os.environ.get("HOST_CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
       "-g", "-O1", "-fsanitize=address,undefined", "-o", str(out / "test_s1")]
subprocess.run(cmd + [f"-I{p}" for p in includes] + sources, cwd=root, check=True)
for case in ["commands", "rx_positions", "tx_queue", "backpressure", "rx_overflow", "uart_error",
             "start_error", "tx_error", "timeout", "late_poll", "timeout_race", "scheduler", "priorities"]:
    subprocess.run([str(out / "test_s1"), case], cwd=root, check=True)
