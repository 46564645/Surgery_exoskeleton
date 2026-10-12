"""One bounded R4-3 rotor speed trace. No mode, torque, flash or reset command.

Live mode requires the already flashed, independently read-back 15 deg/s trial
image and a cold SAFE_OFF board. It sends one followobserve, read queries, and
one followstop. The operator supplies the motion; this program never starts
FOLLOW. Default and --offline-check never open a serial port.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import math
import os
import re
import struct
import sys
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent
FW = HERE.parents[2]
BUILD = FW / "build"
BIN = BUILD / "s4-r4-3-speed15-20261009/build/Debug/Exoskeleton_F446.bin"
READBACK = BUILD / "s4-r4-3-speed15-board-20261009/postflash-113676.bin"
TRANSPORT = BUILD / "r4-1-serial-observe.py"
IMAGE_SHA256 = "17abdac670a8095ab959f941c92d8207cb6596c314f717f998a335e9868f471b"
PORT = "COM10"
READS = frozenset({"ping", "status", "shoulder", "shoulderfeedback",
                   "followstatus", "s3feedback"})
COMMANDS = READS | {"followobserve", "followstop"}
WARNING = "WARN FOLLOW ending soon; support arm; torque will ramp down"
MAX_CAPTURE_NS = 20_000_000_000
STILL_NS = 2_000_000_000
MOVE_NS = 12_000_000_000
HOLD_NS = 2_000_000_000
TARGET_ROTOR_REV = 0.46  # nominal 20.7 joint degrees at the unverified 8:1 ratio
MAX_AGE_MS = 150


def require(ok: bool, message: str) -> None:
    if not ok:
        raise RuntimeError(message)


def utc() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha256(path: Path) -> str:
    require(path.is_file(), f"Missing image/readback: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def image_check() -> dict:
    require(FW.resolve().drive.upper() == "H:", "Firmware workspace must be on H:")
    require(BIN.stat().st_size == READBACK.stat().st_size == 113676,
            "BIN and independent readback sizes differ from trial image")
    require(sha256(BIN) == IMAGE_SHA256 and sha256(READBACK) == IMAGE_SHA256,
            "BIN or independent board readback differs from the 15 deg/s trial")
    return {"bin": str(BIN), "readback": str(READBACK),
            "bytes": BIN.stat().st_size, "sha256": IMAGE_SHA256}


def uint(fields: dict, key: str) -> int:
    value = fields.get(key, "")
    require(re.fullmatch(r"[0-9]+", value) is not None,
            "Malformed unsigned field: " + key)
    number = int(value)
    require(number <= 0xffffffff, "Unsigned field out of range: " + key)
    return number


def elapsed(new: int, old: int) -> int:
    return (new - old) & 0xffffffff


def float32_bits(fields: dict, key: str) -> float:
    value = fields.get(key, "")
    require(re.fullmatch(r"[0-9A-Fa-f]{8}", value) is not None,
            "Malformed float32 bits: " + key)
    decoded = struct.unpack("<f", struct.pack("<I", int(value, 16)))[0]
    require(math.isfinite(decoded), "Nonfinite float32: " + key)
    return decoded


def decode_pair(pair: dict, follow: dict, received_ns: int) -> dict:
    """Preserve bits and timestamps; S3 fresh=0 is expected with timeout=0."""
    seq, now = uint(pair, "seq"), uint(pair, "now")
    pq, pr, vq, vr = (uint(pair, key) for key in ("pq", "pr", "vq", "vr"))
    pos, vel = float32_bits(pair, "pos"), float32_bits(pair, "vel")
    ordering = all(elapsed(b, a) <= MAX_AGE_MS for a, b in
                   ((pq, pr), (pr, vq), (vq, vr)))
    ages_ok = elapsed(now, pr) <= MAX_AGE_MS and elapsed(now, vr) <= MAX_AGE_MS
    follow_seq = uint(follow, "seq")
    # The reader and FOLLOW snapshots are queried successively, so either can
    # be one property cycle ahead. Timestamps still gate actual recency.
    follow_near = elapsed(follow_seq, seq) <= 3 or elapsed(seq, follow_seq) <= 3
    concurrent = (uint(follow, "phase") == 2 and uint(follow, "reader") == 2
                  and uint(follow, "fresh") == 1 and uint(follow, "fault") == 0)
    return {"at_ns": received_ns, "seq": seq, "follow_seq": follow_seq,
            "s3_fresh_reported": uint(pair, "fresh"), "now_ms": now,
            "pos_bits": pair["pos"], "vel_bits": pair["vel"],
            "position_rev": pos, "velocity_rev_s": vel,
            "pq_ms": pq, "pr_ms": pr, "vq_ms": vq, "vr_ms": vr,
            "phase": uint(follow, "phase"), "reader": uint(follow, "reader"),
            "follow_fresh": uint(follow, "fresh"),
            "output_allowed": uint(follow, "output_allowed"),
            "cmd_torque": uint(follow, "cmd_torque"),
            "fault": uint(follow, "fault"),
            "new_fresh_pair": bool(seq and ordering and ages_ok and follow_near
                                   and concurrent),
            "position_timing_ordered": ordering, "position_velocity_age_ok": ages_ok,
            "follow_sequence_near": follow_near}


def read_only_stopped(fields: dict) -> bool:
    # Cancellation may end as STOPPED (3); COMPLETE (7) is also terminal.
    return (uint(fields, "phase") == 3
            and uint(fields, "reader") in (3, 7)
            and uint(fields, "output_allowed") == 0
            and uint(fields, "cmd_torque") == 0
            and uint(fields, "fault") == 0)


def offline_check() -> None:
    fields = {"seq": "7", "now": "110", "fresh": "0", "pos": "403AE148",
              "vel": "3D800000", "pq": "90", "pr": "93", "vq": "100",
              "vr": "103", "out": "0"}
    follow = {"seq": "7", "phase": "2", "reader": "2", "fresh": "1",
              "fault": "0", "output_allowed": "0", "cmd_torque": "0"}
    row = decode_pair(fields, follow, 123)
    require(row["new_fresh_pair"] and row["s3_fresh_reported"] == 0
            and abs(row["velocity_rev_s"] - 0.0625) < 1e-9,
            "Freshness/float32 decoder regression")
    follow["fresh"] = "0"
    require(not decode_pair(fields, follow, 123)["new_fresh_pair"],
            "Follow freshness gate regression")
    stopped = {"phase": "3", "reader": "3", "output_allowed": "0",
               "cmd_torque": "0", "fault": "0"}
    require(read_only_stopped(stopped), "Read-only STOPPED gate regression")
    stopped["reader"] = "7"
    require(read_only_stopped(stopped), "Read-only COMPLETE gate regression")
    stopped["fault"] = "1"
    require(not read_only_stopped(stopped), "Faulted stop accepted")
    print("offline parser/freshness/stop check PASS; no serial or motor access", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true")
    parser.add_argument("--run", action="store_true",
                        help="one authorized observation with operator and 24 V present")
    parser.add_argument("--out-dir", type=Path,
                        help="new, exclusive evidence directory below firmware/build on H:")
    args = parser.parse_args()
    if args.offline_check:
        require(not args.run and args.out_dir is None,
                "Offline check cannot be combined with live arguments")
        offline_check()
        return
    require(args.run and args.out_dir is not None,
            "No hardware action. Pass --run and a new --out-dir for one live capture")
    image = image_check()  # Before serial port access or an evidence directory is made.
    out = args.out_dir.resolve()
    require(out.drive.upper() == "H:" and BUILD.resolve() in out.parents,
            "Evidence directory must be below firmware/build on H:")
    require(not out.exists() and out.parent.is_dir(),
            "Evidence directory must be new; no automatic replay")
    out.mkdir()
    temp = out / "tmp"
    temp.mkdir()
    for key in ("TEMP", "TMP", "TMPDIR", "PYTHONPYCACHEPREFIX"):
        os.environ[key] = str(temp)
    sys.dont_write_bytecode = True
    require(TRANSPORT.is_file(), "Existing fixed ST-LINK transport missing")
    spec = importlib.util.spec_from_file_location("s4_r43_readonly_transport", TRANSPORT)
    require(spec is not None and spec.loader is not None, "Transport could not load")
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    try:
        import winsound
    except ImportError as exc:
        raise RuntimeError("Audible cues require Windows") from exc

    report = {"result": "STARTED_DO_NOT_REPEAT", "image": image,
              "started_utc": utc(), "motor_control_commands": 0,
              "target_rotor_rev": TARGET_ROTOR_REV,
              "nominal_joint_deg_at_target": TARGET_ROTOR_REV * 45.0,
              "nominal_ratio_is_unverified": True,
              "operator_motion_and_hold_confirmation": None,
              "events_file": str(out / "events.jsonl")}
    summary_path = out / "summary.json"
    summary_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
    events = (out / "events.jsonl").open("x", encoding="utf-8", buffering=1)
    link = None
    observe_attempted = stop_attempted = False
    samples: list[dict] = []
    last_seq = None

    def event(kind: str, **fields) -> None:
        events.write(json.dumps({"utc": utc(), "monotonic_ns": time.monotonic_ns(),
                                 "kind": kind, **fields}, ensure_ascii=False) + "\n")
        events.flush()

    def query(command: str) -> tuple[str, dict, int]:
        nonlocal observe_attempted, stop_attempted
        require(command in COMMANDS, "Command outside fixed read-only whitelist")
        if command == "followobserve":
            require(not observe_attempted, "followobserve may be sent only once")
            observe_attempted = True  # Mark before the write; uncertain delivery is not retried.
        if command == "followstop":
            require(observe_attempted and not stop_attempted,
                    "followstop may be sent only once after observe")
            stop_attempted = True
        sent_ns = time.monotonic_ns()
        wire = (command + "\n").encode("ascii")
        require(link.device.write(wire) == len(wire), "Incomplete serial write: " + command)
        for _ in range(3):
            raw = link.device.read_until(b"\n", 1025)
            received_ns = time.monotonic_ns()
            line = raw.decode("ascii", errors="backslashreplace").rstrip("\r\n")
            event("serial", command=command, sent_ns=sent_ns,
                  received_ns=received_ns, raw_hex=raw.hex(), reply=line)
            require(raw.endswith(b"\n") and len(raw) <= 1024,
                    "Missing/oversize reply: " + command)
            if line == WARNING:
                continue
            require(line.startswith("OK ") or line.startswith("ERR "),
                    "Unexpected serial reply: " + line)
            return line, base.PARSER.parse(line) if line.startswith("OK ") else {}, received_ns
        raise RuntimeError("No bounded reply after unsolicited warning")

    def follow() -> dict:
        line, fields, _ = query("followstatus")
        require(line.startswith("OK "), "followstatus failed: " + line)
        return fields

    def output_off(fields: dict) -> None:
        require(uint(fields, "output_allowed") == 0
                and uint(fields, "cmd_torque") == 0 and uint(fields, "fault") == 0,
                "FOLLOW output/fault guard changed")

    def sample() -> dict:
        nonlocal last_seq
        line, pair, pair_ns = query("s3feedback")
        require(line.startswith("OK s3feedback "), "s3feedback failed")
        state = follow()
        row = decode_pair(pair, state, pair_ns)
        event("sample", **row)
        require(uint(pair, "out") == 0, "S3 output guard changed")
        output_off(state)
        require(row["phase"] == 2 and row["reader"] == 2,
                "Observation left read-only phase")
        if row["new_fresh_pair"]:
            if last_seq is not None:
                advance = elapsed(row["seq"], last_seq)
                require(advance < 0x80000000,
                        "Feedback sequence regressed")
                if advance == 0:
                    return {}
            last_seq = row["seq"]
            samples.append(row)
            return row
        return {}

    def cue(name: str, frequency: int, duration_ms: int) -> int:
        start_ns = time.monotonic_ns()
        event("operator_cue", cue=name, cue_start_ns=start_ns)
        print(name, flush=True)
        winsound.Beep(frequency, duration_ms)
        event("operator_cue_end", cue=name)
        return start_ns

    try:
        binding = base.BINDING.binding_probe()
        require(binding.get("serial_verified") is True
                and len(binding.get("ports", [])) == 1
                and binding["ports"][0]["DeviceID"].upper() == PORT,
                "Expected unique fixed ST-LINK COM10 binding absent")
        report["binding"] = binding
        link = base.Link(PORT, [])  # Existing no-reset DTR/RTS-low transport.
        require(query("ping")[0] == "OK pong", "Unexpected ping")
        line, status, _ = query("status")
        require(line.startswith("OK ") and uint(status, "output_allowed") == 0,
                "Platform output not off")
        for key in ("uart_errors", "rx_overflows", "tx_timeouts", "scheduler_overruns"):
            require(uint(status, key) == 0, "Platform counter nonzero: " + key)
        line, shoulder, _ = query("shoulder")
        require(line.startswith("OK ") and shoulder.get("bus") == "shoulder"
                and uint(shoulder, "started") == 1
                and uint(shoulder, "start_error") == 0,
                "Shoulder CAN did not start")
        for key in ("drop", "invalid", "fov", "err", "bus_off"):
            require(uint(shoulder, key) == 0, "Shoulder CAN counter nonzero: " + key)
        line, hb, _ = query("shoulderfeedback")
        require(line.startswith("OK ") and uint(hb, "hb") == 1
                and uint(hb, "state") == 1 and hb.get("error") == "00000000"
                and uint(hb, "fault") == 0, "No healthy IDLE heartbeat")
        _, clock, _ = query("status")
        hb_age = elapsed(uint(clock, "uptime_ms"), uint(hb, "hb_ms"))
        require(hb_age <= MAX_AGE_MS, "IDLE heartbeat stale")
        cold = follow()
        require(all(uint(cold, key) == value for key, value in
                    {"phase": 1, "reader": 1, "seq": 0, "zero": 0,
                     "output_allowed": 0, "cmd_torque": 0, "fault": 0}.items()),
                "Not cold SAFE_OFF / unused reader")
        report["preflight"] = {"platform": clock, "shoulder": shoulder,
                               "heartbeat": hb, "heartbeat_age_ms": hb_age,
                               "follow": cold}
        line, _, observe_ns = query("followobserve")
        require(line == "OK FOLLOW observe_started output_allowed=0",
                "followobserve rejected: " + line)
        report["observe_started_ns"] = observe_ns
        deadline = observe_ns + MAX_CAPTURE_NS
        origin = None
        while time.monotonic_ns() - observe_ns < STILL_NS:
            row = sample()
            if row and origin is None:
                origin = row
            require(time.monotonic_ns() < deadline, "20 s observation cap reached")
            time.sleep(0.01)
        require(origin is not None, "No fresh starting rotor pair in first 2 s")
        report["origin"] = {"seq": origin["seq"],
                            "position_rev": origin["position_rev"],
                            "pos_bits": origin["pos_bits"], "pq_ms": origin["pq_ms"]}
        for index in range(3):
            cue("SHORT_BEEP_MOVE_SMOOTHLY" if index == 0 else "SHORT_BEEP", 1200, 130)
            if index < 2:
                time.sleep(0.05)
        move_start = time.monotonic_ns()
        event("motion_window", start_ns=move_start,
              requested_duration_s="8-12", note="Operator movement is not independently verified")
        target_row = None
        while time.monotonic_ns() - move_start < MOVE_NS and time.monotonic_ns() < deadline:
            row = sample()
            if row and row["position_rev"] - origin["position_rev"] >= TARGET_ROTOR_REV:
                target_row = row
                break
            time.sleep(0.01)
        report["target_reached"] = target_row is not None
        report["target_row"] = target_row
        long_ns = cue("LONG_BEEP_STOP_AND_HOLD", 1800, 500)
        report["long_cue_start_ns"] = long_ns
        report["long_cue_reason"] = ("first_fresh_pair_at_target" if target_row
                                      else "12s_or_20s_cap_without_target")
        while time.monotonic_ns() - long_ns < HOLD_NS and time.monotonic_ns() < deadline:
            sample()
            time.sleep(0.01)
        report["capture_elapsed_s"] = (time.monotonic_ns() - observe_ns) / 1e9
        require(report["capture_elapsed_s"] <= 20.5, "Observation cap overrun")
        line, _, _ = query("followstop")
        report["stop_reply"] = line
        require(line.startswith("OK FOLLOW "), "followstop not accepted: " + line)
        stop_deadline = time.monotonic_ns() + 2_000_000_000
        while time.monotonic_ns() < stop_deadline:
            stopped = follow()
            if uint(stopped, "phase") == 3:
                require(read_only_stopped(stopped), "STOPPED status incomplete")
                report["stopped"] = stopped
                break
            require(uint(stopped, "output_allowed") == 0,
                    "Output became allowed during stop")
            time.sleep(0.05)
        else:
            raise RuntimeError("STOPPED not seen within 2 s")
        report["post_status"] = query("status")[1]
        report["post_shoulder"] = query("shoulder")[1]
        report["post_heartbeat"] = query("shoulderfeedback")[1]
        require(uint(report["post_status"], "output_allowed") == 0,
                "Post-stop platform output allowed")
        require(all(uint(report["post_shoulder"], key) == uint(shoulder, key)
                    for key in ("drop", "invalid", "fov", "err", "bus_off")),
                "Shoulder CAN error counter changed")
        report["result"] = ("TRACE_CAPTURED_TARGET_REACHED" if target_row
                            else "TRACE_CAPTURED_TARGET_NOT_REACHED")
    except BaseException as exc:
        report["result"] = "FAIL_OR_INCOMPLETE"
        report["reason"] = str(exc)
        if link is not None and observe_attempted and not stop_attempted:
            try:
                report["cleanup_followstop_reply"] = query("followstop")[0]
            except BaseException as stop_exc:
                report["cleanup_followstop_error"] = str(stop_exc)
        event("operator_cue", cue="SUPPORT_ARM_AND_TURN_OFF_24V_IF_STOP_UNCONFIRMED",
              reason=str(exc))
        print("ABORT: support the arm; if stop is unconfirmed, turn off 24 V", flush=True)
        raise
    finally:
        if link is not None:
            link.close()
        events.close()
        report["observe_attempted"] = observe_attempted
        report["followstop_attempted"] = stop_attempted
        report["samples_total_distinct_fresh"] = len(samples)
        report["samples_file"] = str(out / "events.jsonl")
        report["finished_utc"] = utc()
        summary_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                encoding="utf-8")
        print(json.dumps({"result": report["result"], "reason": report.get("reason"),
                          "samples": len(samples), "summary": str(summary_path)},
                         ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
