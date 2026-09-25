#!/usr/bin/env python3
"""Explicit-port, read-only S1 command validation; never enables motor output.

POSIX/macOS standard library only. --seconds 600 adds 10 queries/sec soak.
Does not flash, reset the board, or change ST-LINK firmware.
"""
import argparse
import json
import os
from pathlib import Path
import select
import termios
import time
import tty

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--seconds", type=int, default=0)
parser.add_argument("--report", type=Path, required=True)
args = parser.parse_args()
report = {"port": args.port, "seconds": args.seconds, "passed": False, "queries": 0}
fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
original = termios.tcgetattr(fd)
pending = bytearray()

def send(data):
    view = memoryview(data)
    while view:
        _, writable, _ = select.select([], [fd], [], 2)
        if not writable:
            raise TimeoutError("serial write timeout")
        view = view[os.write(fd, view):]

def line():
    deadline = time.monotonic() + 2
    while b"\n" not in pending:
        wait = deadline - time.monotonic()
        if wait <= 0:
            raise TimeoutError(f"reply timeout, pending={pending!r}")
        ready, _, _ = select.select([fd], [], [], wait)
        if ready:
            pending.extend(os.read(fd, 4096))
    end = pending.index(10) + 1
    result = bytes(pending[:end])
    del pending[:end]
    if not result.endswith(b"\r\n"):
        raise AssertionError(f"bad line ending: {result!r}")
    return result.decode("ascii").strip()

def status():
    send(b"status\n")
    text = line()
    assert text.startswith("OK stage=S1_COMM "), text
    values = dict(item.split("=", 1) for item in text.split()[1:])
    for key in ["output_allowed", "uart_errors", "rx_overflows", "tx_timeouts"]:
        assert values[key] == "0", text
    return values

try:
    tty.setraw(fd)
    cfg = termios.tcgetattr(fd)
    cfg[4] = cfg[5] = termios.B115200
    cfg[2] |= termios.CLOCAL | termios.CREAD
    cfg[2] &= ~termios.CRTSCTS
    termios.tcsetattr(fd, termios.TCSANOW, cfg)
    termios.tcflush(fd, termios.TCIOFLUSH)
    report["initial_status"] = status()
    for cmd, expected in [(b"ping\r\n", "OK pong"), (b"? extra\n", "ERR args"),
                          (b'ping ""\n', "ERR args"), (b"ping\rx\n", "ERR syntax"),
                          (b"x" * 97 + b"\n", "ERR line_too_long"), (b"ping\n", "OK pong")]:
        send(cmd)
        assert line() == expected
    send(b"ping\n" * 120)  # 600-byte burst crosses 512-byte DMA wrap
    for _ in range(120):
        assert line() == "OK pong"
    print("PASS quick commands and 600-byte DMA burst", flush=True)
    start = time.monotonic()
    for i in range(args.seconds * 10):
        delay = start + i / 10 - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        if i % 2:
            report["latest_status"] = status()
        else:
            send(b"ping\n")
            assert line() == "OK pong"
        report["queries"] = i + 1
        if (i + 1) % 300 == 0:
            print(f"PASS {i + 1} queries / {(i + 1) / 10:.0f}s", flush=True)
    report["final_status"] = status()
    report["elapsed_seconds"] = round(time.monotonic() - start, 3)
    report["passed"] = True
    print(json.dumps(report, ensure_ascii=False), flush=True)
except Exception as exc:
    report["error"] = repr(exc)
    raise
finally:
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    try:
        termios.tcsetattr(fd, termios.TCSANOW, original)
    finally:
        os.close(fd)
