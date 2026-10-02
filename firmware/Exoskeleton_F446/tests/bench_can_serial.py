#!/usr/bin/env python3
"""USB-only S2 receive-foundation check; drivers/H-L disconnected, 24V off.

Uses pyserial on Windows/macOS/Linux. Sends query commands only; never flashes,
resets, enables outputs, or sends CAN traffic. Expects both buses unseen.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import time
import serial


def validate(command, reply):
    assert reply.endswith("\r\n") and len(reply) < 256, repr(reply)
    if command == "ping":
        assert reply == "OK pong\r\n", reply
    elif command in ("help", "?"):
        assert reply in ("OK commands=ping,status,help,?,canstatus,shoulder,elbow\r\n",
                         "OK commands=ping,status,help,?,canstatus,shoulder,elbow,elbowfeedback\r\n"), reply
    elif command == "canstatus":
        assert reply == ("OK can1_started=1 can1_baud=500000 can1_start_error=0 "
                         "can2_started=1 can2_baud=1000000 can2_start_error=0 output_allowed=0\r\n"), reply
    else:
        assert reply.startswith("OK "), reply
        fields = dict(item.split("=", 1) for item in reply.strip().split()[1:])
        expected = ({"stage": "S2_CAN_RX", "output_allowed": "0", "uart_errors": "0",
                     "rx_overflows": "0", "tx_timeouts": "0", "scheduler_overruns": "0"}
                    if command == "status" else
                    {"bus": command, "started": "1", "start_error": "0", "rx": "0",
                     "drop": "0", "invalid": "0", "fov": "0", "err": "0",
                     "last_err": "00000000", "bus_off": "0", "queued": "0",
                     "seen": "0", "id": "na", "dlc": "0", "data": "-", "age_ms": "na"})
        for key, value in expected.items():
            assert fields.get(key) == value, (key, value, reply)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    result = {"started_at": datetime.now().astimezone().isoformat(), "port": args.port,
              "baudrate": 115200, "requested_seconds": args.seconds, "passed": False,
              "condition": "USB only; no drivers; no 24V", "samples": []}
    start = time.monotonic()
    try:
        with serial.Serial(args.port, 115200, timeout=1, write_timeout=1,
                           xonxoff=False, rtscts=False, dsrdtr=False) as port:
            port.reset_input_buffer()

            def query(command):
                port.write((command + "\r\n").encode("ascii"))
                reply = port.read_until(b"\n", 256).decode("ascii")
                result["samples"].append({"elapsed_ms": round(1000 * (time.monotonic() - start)),
                                          "command": command, "reply": reply})
                validate(command, reply)
                return reply

            commands = ["ping", "status", "help", "?", "canstatus", "shoulder", "elbow"]
            for command in commands:
                print(command + ": " + query(command).strip(), flush=True)
            soak_start = time.monotonic()
            deadline = soak_start
            count = 0
            while time.monotonic() - soak_start < args.seconds:
                query(commands[count % len(commands)])
                count += 1
                deadline += 0.1
                time.sleep(max(0, deadline - time.monotonic()))
                if count % 150 == 0:
                    print(f"{count} queries passed", flush=True)
            result["soak_seconds"] = time.monotonic() - soak_start
            result["soak_queries"] = count
            result["final_status"] = query("status")
            result["passed"] = True
    except Exception as error:
        result["error"] = repr(error)
        raise
    finally:
        result["elapsed_seconds"] = time.monotonic() - start
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(f"passed={result['passed']} report={args.report}", flush=True)


if __name__ == "__main__":
    main()
