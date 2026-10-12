"""Export the frozen CAN2 elbow diagnostic through the existing UART.

Only ecstatus/ecfault/ecgate/ecfreeze/ecrewind/ecnext are sent. There are no
CAN, reset, enable, motor or firmware-write commands. Requires pyserial.

Use an explicit, new directory on H: for each attempt. raw.jsonl keeps every
reply, including partial/error replies; capture.json records parsed evidence
and whether the export completed. Keep USB power on until export is finished:
the firmware recorder is RAM-only and does not survive an MCU reset.
"""

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import struct
import sys


sys.dont_write_bytecode = True

UINT32_MAX = (1 << 32) - 1
CAPACITY = 128
HEX8 = re.compile(r"[0-9A-Fa-f]{8}\Z")
HEX16 = re.compile(r"[0-9A-Fa-f]{16}\Z")
COMMANDS = frozenset({
    "ecstatus", "ecfault", "ecgate", "ecfreeze", "ecrewind", "ecnext"
})


def number(value, name, minimum=0, maximum=UINT32_MAX):
    if not re.fullmatch(r"-?(?:0|[1-9][0-9]*)", value):
        raise ValueError(f"{name}: invalid decimal integer")
    parsed = int(value, 10)
    if parsed < minimum or parsed > maximum:
        raise ValueError(f"{name}: out of range")
    return parsed


def bit(value, name):
    return number(value, name, 0, 1)


def hex_value(value, name, pattern):
    if pattern.fullmatch(value) is None:
        raise ValueError(f"{name}: invalid fixed-width hex")
    return value.upper()


def tokens(reply, expected):
    if not reply.startswith("OK ec "):
        raise ValueError(f"{expected}: unexpected reply prefix")
    result = {}
    for token in reply[6:].split():
        if token.count("=") != 1:
            raise ValueError(f"{expected}: malformed field")
        key, value = token.split("=", 1)
        if not key or not value or key in result:
            raise ValueError(f"{expected}: empty or duplicate field")
        result[key] = value
    return result


def exact(fields, names, source):
    if set(fields) != set(names):
        missing = sorted(set(names) - set(fields))
        extra = sorted(set(fields) - set(names))
        raise ValueError(f"{source}: fields mismatch missing={missing} extra={extra}")


def status(reply):
    raw = tokens(reply, "ecstatus")
    exact(raw, ("v", "bus", "count", "frozen", "triggered", "trigger_seq",
                "trigger_kind", "overwritten", "suppressed", "cursor"), "ecstatus")
    result = {key: number(value, key) for key, value in raw.items()}
    if result["v"] != 1 or result["bus"] != 2:
        raise ValueError("ecstatus: unsupported recorder version or bus")
    if result["count"] > CAPACITY or result["cursor"] > result["count"]:
        raise ValueError("ecstatus: invalid count or cursor")
    if result["frozen"] not in (0, 1) or result["triggered"] not in (0, 1):
        raise ValueError("ecstatus: invalid boolean")
    if result["trigger_kind"] not in (0, 2, 3):
        raise ValueError("ecstatus: invalid trigger kind")
    if result["triggered"] != (result["trigger_kind"] != 0):
        raise ValueError("ecstatus: trigger flags disagree")
    return result


def fault(reply):
    raw = tokens(reply, "ecfault")
    present = bit(raw.get("fault", ""), "fault")
    if not present:
        exact(raw, ("fault",), "ecfault")
        return {"fault": 0}
    keys = ("fault", "pre", "post", "last_ticks", "last_ms", "last_seq",
            "has", "valid", "ticks", "rpm", "seq", "rx_ms", "now_ms",
            "timeout", "limit_bits", "dir", "still")
    exact(raw, keys, "ecfault")
    result = {key: number(raw[key], key) for key in keys
              if key not in ("limit_bits", "rpm", "dir")}
    result["rpm"] = number(raw["rpm"], "rpm", -32768, 32767)
    result["dir"] = number(raw["dir"], "dir", -128, 127)
    result["limit_bits"] = hex_value(raw["limit_bits"], "limit_bits", HEX8)
    if result["last_ticks"] > 65535 or result["ticks"] > 65535:
        raise ValueError("ecfault: tick field exceeds uint16")
    for key in ("has", "valid", "still"):
        if result[key] not in (0, 1):
            raise ValueError(f"ecfault: invalid {key} flag")
    return result


def gate(reply):
    raw = tokens(reply, "ecgate")
    present = bit(raw.get("gate", ""), "gate")
    if not present:
        exact(raw, ("gate",), "ecgate")
        return {"gate": 0}
    keys = ("gate", "pre", "post", "reason", "safety", "now_ms", "in",
            "transport", "j0", "j1", "drivers", "stop_clear", "still")
    exact(raw, keys, "ecgate")
    result = {key: number(value, key) for key, value in raw.items()}
    for key in ("in", "transport", "j0", "j1", "drivers", "stop_clear", "still"):
        if result[key] not in (0, 1):
            raise ValueError(f"ecgate: invalid {key} flag")
    return result


def event(reply, expected_index, previous_sequence):
    raw = tokens(reply, "ecnext")
    keys = ("i", "seq", "ms", "kind", "rx_ms", "fb_seq", "id", "dlc",
            "status", "data")
    exact(raw, keys, "ecnext")
    result = {key: number(raw[key], key) for key in keys if key not in ("id", "data")}
    result["id"] = hex_value(raw["id"], "id", HEX8)
    result["data"] = hex_value(raw["data"], "data", HEX16)
    if result["i"] != expected_index:
        raise ValueError("ecnext: cursor mismatch")
    if previous_sequence is not None and ((result["seq"] - previous_sequence) & UINT32_MAX) != 1:
        raise ValueError("ecnext: nonconsecutive recorder sequence")
    if result["kind"] not in (1, 2, 3) or result["dlc"] > 8:
        raise ValueError("ecnext: invalid kind or DLC")
    if int(result["id"], 16) > 0x7FF:
        raise ValueError("ecnext: invalid standard CAN ID")
    if result["status"] not in (0, 1) or (result["kind"] in (2, 3) and result["status"] != 1):
        raise ValueError("ecnext: invalid status flag")
    if result["kind"] == 3 and (result["rx_ms"] != 0 or result["fb_seq"] != 0):
        raise ValueError("ecnext: gateway event has unexpected feedback fields")
    if result["kind"] == 1 and result["status"] == 0 and result["fb_seq"] != 0:
        raise ValueError("ecnext: rejected feedback has a sequence")
    return result


def ack(reply, key, expected):
    raw = tokens(reply, key)
    exact(raw, (key,), key)
    if number(raw[key], key) != expected:
        raise ValueError(f"{key}: unexpected acknowledgement")


def make_folder(raw_path):
    if not raw_path.is_absolute() or raw_path.drive.upper() != "H:":
        raise ValueError("--out must be an absolute path on H:")
    folder = raw_path.resolve(strict=False)
    if folder.drive.upper() != "H:":
        raise ValueError("--out resolves outside H:")
    folder.mkdir(parents=True, exist_ok=False)
    return folder


def fault_hints(snapshot):
    """Recalculate two limits for orientation only, never a root-cause verdict."""
    hint = {"speed_exceeded": None, "jump_exceeded": None,
            "step_ticks": None, "elapsed_ms": None, "allowed_ticks": None,
            "note": "Offline Python arithmetic is only a hint near thresholds; firmware checks speed before jump and its raw trigger record is authoritative"}
    if snapshot["fault"] != 1:
        return hint
    limit = struct.unpack(">f", bytes.fromhex(snapshot["limit_bits"]))[0]
    age = (snapshot["now_ms"] - snapshot["rx_ms"]) & UINT32_MAX
    if (not math.isfinite(limit) or limit <= 0.0 or snapshot["dir"] not in (-1, 1)
            or not snapshot["has"] or not snapshot["valid"] or snapshot["seq"] == 0
            or age > snapshot["timeout"] or snapshot["ticks"] >= 8192):
        return hint
    speed = abs(snapshot["dir"] * snapshot["rpm"] * (2.0 * math.pi / 60.0))
    hint["speed_exceeded"] = speed > limit
    if snapshot["pre"] != 2:
        return hint
    if snapshot["last_ticks"] >= 8192:
        return hint
    sequence_step = (snapshot["seq"] - snapshot["last_seq"]) & UINT32_MAX
    elapsed = (snapshot["rx_ms"] - snapshot["last_ms"]) & UINT32_MAX
    if sequence_step == 0 or sequence_step >= 0x80000000 \
            or elapsed >= 0x80000000 or elapsed > snapshot["timeout"]:
        return hint
    step = snapshot["ticks"] - snapshot["last_ticks"]
    if step > 4096:
        step -= 8192
    elif step < -4096:
        step += 8192
    allowed = limit * (elapsed / 1000.0) / (2.0 * math.pi / 8192.0) + 2.0
    hint["step_ticks"] = step
    hint["elapsed_ms"] = elapsed
    hint["allowed_ticks"] = allowed
    hint["jump_exceeded"] = abs(step) > allowed
    return hint


def run(port_name, folder):
    capture = {
        "format": "elbow_can2_capture_v1", "port": port_name,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "status_before": None, "status": None, "fault": None, "gate": None,
        "events": [], "status_after": None, "fault_hints": None,
        "complete": False, "error": None,
        "note": "MCU main-loop RAM observations with millisecond timestamps; not an independent wire trace or measured joint torque",
    }
    port = None
    try:
        with (folder / "raw.jsonl").open("w", encoding="utf-8") as log:
            def ask(command):
                if command not in COMMANDS:
                    raise ValueError("command is outside the read-only recorder grammar")
                record = {"utc": datetime.now(timezone.utc).isoformat(),
                          "command": command, "reply_hex": ""}
                try:
                    written = port.write((command + "\n").encode("ascii"))
                    if written != len(command) + 1:
                        raise RuntimeError(f"{command}: partial UART write")
                    raw = port.read_until(b"\n", size=256)
                    record["reply_hex"] = raw.hex()
                finally:
                    log.write(json.dumps(record, ensure_ascii=False) + "\n")
                    log.flush()
                if not raw.endswith(b"\r\n") or len(raw) > 255:
                    raise RuntimeError(f"{command}: missing or oversized reply; raw saved")
                reply = raw.decode("ascii", errors="strict")
                if not reply.startswith("OK ec "):
                    raise RuntimeError(f"{command}: unexpected reply; raw saved")
                return reply

            try:
                import serial
                # Set modem-line preferences before open; adapter behaviour is
                # hardware-dependent, so the operator must keep USB power on.
                port = serial.Serial(port=None, baudrate=115200,
                                     timeout=2, write_timeout=2)
                port.dtr = False
                port.rts = False
                port.port = port_name
                port.open()

                capture["status_before"] = status(ask("ecstatus"))
                ack(ask("ecfreeze"), "frozen", 1)
                ack(ask("ecrewind"), "cursor", 0)
                capture["status"] = status(ask("ecstatus"))
                fixed = capture["status"]
                if fixed["frozen"] != 1 or fixed["cursor"] != 0:
                    raise ValueError("ecstatus: recorder was not frozen and rewound")
                capture["fault"] = fault(ask("ecfault"))
                capture["fault_hints"] = fault_hints(capture["fault"])
                capture["gate"] = gate(ask("ecgate"))
                for index in range(fixed["count"]):
                    prior = capture["events"][-1]["seq"] if capture["events"] else None
                    capture["events"].append(event(ask("ecnext"), index, prior))
                end = tokens(ask("ecnext"), "ecnext")
                exact(end, ("end",), "ecnext end")
                if bit(end["end"], "end") != 1:
                    raise ValueError("ecnext: missing end marker")
                capture["status_after"] = status(ask("ecstatus"))
                after = capture["status_after"]
                if after["count"] != fixed["count"] or after["cursor"] != fixed["count"] \
                        or after["trigger_seq"] != fixed["trigger_seq"] \
                        or after["trigger_kind"] != fixed["trigger_kind"]:
                    raise ValueError("ecstatus: recorder changed during export")
                if fixed["triggered"]:
                    match = [e for e in capture["events"] if e["seq"] == fixed["trigger_seq"]]
                    if len(match) != 1 or match[0]["kind"] != fixed["trigger_kind"]:
                        raise ValueError("ecnext: missing or mismatched trigger event")
                    if fixed["trigger_kind"] == 2 and capture["fault"]["fault"] != 1:
                        raise ValueError("ecfault: trigger snapshot missing")
                    if fixed["trigger_kind"] == 3 and capture["gate"]["gate"] != 1:
                        raise ValueError("ecgate: trigger snapshot missing")
                capture["complete"] = True
            finally:
                if port is not None:
                    port.close()
    except Exception as exc:
        capture["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        capture["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (folder / "capture.json").write_text(
            json.dumps(capture, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
            encoding="utf-8")
    return capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Explicit COM port, e.g. COM10")
    parser.add_argument("--out", type=Path, required=True,
                        help="Absolute H: path for a new evidence directory")
    args = parser.parse_args()
    try:
        folder = make_folder(args.out)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    for key in ("TEMP", "TMP", "TMPDIR"):
        os.environ[key] = str(folder)
    result = run(args.port, folder)
    if not result["complete"]:
        print(f"Export failed; partial raw and parsed evidence saved at {folder}: {result['error']}",
              file=sys.stderr)
        return 1
    print(f"Saved {len(result['events'])} CAN2 diagnostic events to {folder}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
