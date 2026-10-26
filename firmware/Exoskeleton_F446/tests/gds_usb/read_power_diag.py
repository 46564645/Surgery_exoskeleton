"""GDS68 DC-bus diagnostic: fixed 20-request NativeUSB read. No arguments: OFFLINE selftest.

Reuses D:/codex_tasks/.../s3-shoulder-supervised/usb/read_s3_properties.py
(identity check, Fibre guard, USB claim/release, process deadline) and only
swaps its ten S3/limit fields for the ODrive DC-bus error/config endpoints
below. Never writes a scalar, calls a function, resets or saves config.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
BASE = Path(r"D:\codex_tasks\2026-10-01\task\s3-shoulder-supervised\usb")
sys.path.insert(0, str(BASE))
import read_s3_properties as rs  # noqa: E402

OUT = Path(__file__).resolve().parents[2] / "build" / "gds-power-diag"

# Endpoint ids/types from docs/reference/gds_hw3.11.1_fw0.6.5_endpoint0.json.
ERRORS = {
    "error": (1, "uint8"),                    # odrv0.error: 0x02 UV, 0x04 OV, 0x08 regen, 0x10 over-current
    "axis0.error": (138, "uint32"),
    "axis0.motor.error": (193, "uint64"),     # 0x01000000 SYSTEM_LEVEL
    "vbus_voltage": (2, "float"),
    "config.enable_brake_resistor": (95, "bool"),
    "config.max_regen_current": (93, "float"),
    "config.dc_bus_undervoltage_trip_level": (96, "float"),
}
CONFIG = {
    "config.dc_bus_overvoltage_trip_level": (97, "float"),
    "config.dc_max_positive_current": (101, "float"),
    "config.dc_max_negative_current": (102, "float"),
}


def patch():
    rs.S3, rs.LIMITS = ERRORS, CONFIG
    rs.FIELDS = {**rs.IDENTITY, **ERRORS, **CONFIG}
    rs.ORDER = tuple(rs.IDENTITY) + tuple(ERRORS) + tuple(CONFIG) + ("axis0.current_state",)
    rs.ALLOWED = {name: {item[1]} for name, item in rs.FIELDS.items()}
    rs.BOOL_IDS = {item[0] for item in rs.FIELDS.values() if item[1] == "bool"}
    rs.ROOT = OUT
    OUT.mkdir(parents=True, exist_ok=True)


def selftest():
    protocol = rs.reuse.load_protocol()
    index, crc = rs.fixed_schema(protocol)
    guard, report = rs.FixedGuard(index, crc), {}
    fake = {"error": 8, "axis0.error": 0x40, "axis0.motor.error": 0x01000000,
            "config.dc_max_negative_current": -0.01}

    class FakeTransport:
        channel = None

        def process_packet(self, packet):
            seq, length = guard.check(packet)
            name = rs.ORDER[guard.sent - 1]
            if name in rs.IDENTITY:
                value = rs.IDENTITY[name][2]
            else:
                value = fake.get(name, False if rs.FIELDS[name][1] == "bool" else 24.0
                                 if rs.FIELDS[name][1] == "float" else 0)
            _, fmt = rs.reuse.permitted_scalar(index, name, rs.ALLOWED)
            raw = struct.pack(fmt, value)
            assert len(raw) == length
            self.channel.process_packet(struct.pack("<H", seq | 0x8000) + raw)

    channel = rs.reuse.make_channel(protocol, FakeTransport())
    channel._interface_definition_crc = crc
    rs.read_values(channel, guard, index, report)
    assert guard.sent == 20 and report["identity_verified"]
    assert [r["endpoint_id"] for r in guard.requests] == list(guard.expected_ids)
    assert all(not r["input_hex"] for r in guard.requests)
    assert report["s3_properties"]["error"] == 8
    assert report["s3_properties"]["axis0.motor.error"] == 0x01000000
    assert abs(report["limits"]["config.dc_max_negative_current"] + 0.01) < 1e-6
    assert not any(n == "usb" or n.startswith("usb.") for n in sys.modules)
    print(json.dumps({"selftest": "PASS", "hardware_access": False,
                      "endpoint_ids": list(guard.expected_ids)}))
    return 0


def summarize(path):
    report = json.loads(Path(path).read_text(encoding="utf-8"))
    if not report.get("success"):
        return
    e, c = report["s3_properties"], report["limits"]
    bits = {0x01: "CONTROL_ITERATION_MISSED?", 0x02: "DC_BUS_UNDER_VOLTAGE", 0x04: "DC_BUS_OVER_VOLTAGE",
            0x08: "DC_BUS_OVER_REGEN_CURRENT", 0x10: "DC_BUS_OVER_CURRENT"}
    decoded = [v for k, v in bits.items() if e["error"] & k] or ["NONE"]
    print(json.dumps({"odrv_error": hex(e["error"]), "odrv_error_decoded": decoded,
                      "axis_error": hex(e["axis0.error"]), "motor_error": hex(e["axis0.motor.error"]),
                      "vbus_voltage": e["vbus_voltage"], **{k.split(".")[-1]: v for k, v in c.items()},
                      "enable_brake_resistor": e["config.enable_brake_resistor"],
                      "max_regen_current": e["config.max_regen_current"],
                      "undervoltage_trip": e["config.dc_bus_undervoltage_trip_level"]}, indent=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--confirm")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    patch()
    if not args.capture:
        return selftest()
    if args.confirm != rs.CONFIRM:
        parser.error("Capture requires " + rs.CONFIRM)
    if args.worker:
        return rs.capture()
    started = datetime.now(timezone.utc).timestamp()
    command = [sys.executable, "-B", str(Path(__file__).resolve()), "--capture", "--confirm", rs.CONFIRM, "--worker"]
    try:
        code = subprocess.run(command, timeout=rs.PROCESS_SECONDS, check=False).returncode
    except subprocess.TimeoutExpired:
        print(json.dumps({"success": False, "error": "process deadline; capture incomplete; no retry"}))
        return 2
    latest = [p for p in OUT.glob("captures/*/capture.json") if p.stat().st_mtime >= started]
    if latest:
        summarize(max(latest, key=lambda p: p.stat().st_mtime))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
