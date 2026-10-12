"""One R4-3 shoulder FOLLOW session; running this script can command motor torque.

Use --mode low and then --mode high-drift on separate cold boots. Slowly lift
from vertical until a long beep tells the operator to hold; telemetry, not a
precise hand-set angle, selects the two postures. High-drift then cues a slow
downward reposition and a separate release beep. The
audible release cue is a timestamp marker, not proof that the hand let go.
No flash, reset, supply switching, or automatic retry is performed here.
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
import statistics
import sys
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent
FW = HERE.parents[2]
TRANSPORT = FW / "build/r4-1-serial-observe.py"
PREPARE_DEADLINE_NS = 19_000_000_000  # firmware preparation TTL is 20 s
SESSION_NS = 10_000_000_000
REPOSITION_CUE_NS = 800_000_000
RELEASE_DEADLINE_NS = 3_000_000_000
SETTLE_NS = 500_000_000
DRIFT_NS = 5_000_000_000
DEG_PER_URAD = 180.0 / (math.pi * 1_000_000.0)
COMMANDS = frozenset({"followprepare", "followstatus", "followangle",
                      "followstart", "followstop", "shoulderfeedback",
                      "shoulder", "status", "s3reader"})
WARNING = "WARN FOLLOW ending soon; support arm; torque will ramp down"


def initial_cue_allowed(mode: str, angle_deg: float) -> bool:
    return (20.5 <= angle_deg <= 21.1 if mode == "low"
            else 25.5 <= angle_deg <= 26.4)


def initial_cue_ceiling(mode: str) -> float:
    return 21.1 if mode == "low" else 26.4


def reposition_cue_allowed(angle_deg: float) -> bool:
    return 20.5 < angle_deg <= 23.5


def require(ok: bool, message: str) -> None:
    if not ok:
        raise RuntimeError(message)


def utc() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def on_h(path: Path) -> Path:
    result = path.resolve()
    require(result.drive.upper() == "H:", f"Path must be on H: {result}")
    return result


def sha256(path: Path) -> str:
    require(path.is_file(), f"File missing: {path}")
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def verify_image(bin_path: Path, readback: Path, expected: str) -> dict:
    require(re.fullmatch(r"[0-9a-f]{64}", expected) is not None,
            "Expected SHA256 must be 64 lowercase hex digits")
    build = (FW / "build").resolve()
    require(build in bin_path.parents and build in readback.parents,
            "Trial BIN and independent flash readback must be under firmware/build")
    require(bin_path.stat().st_size == readback.stat().st_size,
            "Trial BIN/readback lengths differ")
    got_bin, got_readback = sha256(bin_path), sha256(readback)
    require(got_bin == expected and got_readback == expected,
            "Trial BIN/readback SHA256 does not match the announced image")
    return {"bin": str(bin_path), "readback": str(readback),
            "bytes": bin_path.stat().st_size, "sha256": expected}


def analyze_drift(rows: list[dict], release_ns: int) -> dict:
    """Ten disjoint half-second bins after a half-second release buffer."""
    start = release_ns + SETTLE_NS
    bins: list[dict] = []
    for index in range(10):
        left = start + index * SETTLE_NS
        right = left + SETTLE_NS
        points = [row for row in rows if left <= row["at_ns"] < right]
        require(len(points) >= 2 and len({row["seq"] for row in points}) >= 2,
                f"Insufficient distinct fresh samples in 0.5 s bin {index}")
        require(max(row["at_ns"] for row in points)
                - min(row["at_ns"] for row in points) >= 80_000_000,
                f"Fresh samples are too clustered in 0.5 s bin {index}")
        values = [row["angle_deg"] for row in points]
        bins.append({"index": index, "samples": len(points),
                     "first_seq": points[0]["seq"], "last_seq": points[-1]["seq"],
                     "median_deg": statistics.median(values),
                     "min_deg": min(values), "max_deg": max(values)})
    origin = bins[0]["median_deg"]
    end_delta = bins[-1]["median_deg"] - origin
    max_delta = max(abs(item["median_deg"] - origin) for item in bins)
    margin = min(min(item["min_deg"] - 20.0, 30.0 - item["max_deg"])
                 for item in bins)
    numeric_pass = (abs(end_delta) <= 0.5 and max_delta <= 0.75
                    and margin > 0.0)
    return {"bins": bins, "first_last_delta_deg": end_delta,
            "max_median_deviation_deg": max_delta,
            "min_software_boundary_margin_deg": margin,
            "numeric_pass": numeric_pass,
            "operator_release_and_no_retouch_confirmed": None,
            "oscillation_or_self_motion_operator_observation": None}


def offline_check() -> None:
    release = 1_000_000_000
    rows = []
    for n in range(50):
        rows.append({"at_ns": release + SETTLE_NS + n * 100_000_000,
                     "seq": n + 1, "angle_deg": 21.4 + n * 0.002})
    require(analyze_drift(rows, release)["numeric_pass"],
            "Synthetic stationary trace should pass")
    rows[-1]["angle_deg"] = 23.0
    # A single outlier need not change the last median; move a whole bin.
    for row in rows[-5:]:
        row["angle_deg"] = 23.0
    require(not analyze_drift(rows, release)["numeric_pass"],
            "Synthetic drifting trace should fail")
    require(initial_cue_allowed("low", 20.6)
            and initial_cue_allowed("high-drift", 25.8)
            and not initial_cue_allowed("low", 21.3)
            and not initial_cue_allowed("high-drift", 26.5)
            and reposition_cue_allowed(23.0)
            and not reposition_cue_allowed(20.0),
            "Telemetry cues escaped their bounded angle intervals")
    print("offline drift analysis PASS; no serial or motor access", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true",
                        help="exercise drift math only; no serial access")
    parser.add_argument("--run", action="store_true",
                        help="one authorized powered session")
    parser.add_argument("--mode", choices=("low", "high-drift"))
    parser.add_argument("--bin", type=Path)
    parser.add_argument("--readback", type=Path)
    parser.add_argument("--image-sha256")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--expected-port", default="COM10")
    args = parser.parse_args()
    if args.offline_check:
        require(not args.run, "Offline check cannot run a powered session")
        offline_check()
        return
    require(args.run, "No serial commands sent; pass --run for one live session")
    require(all((args.mode, args.bin, args.readback, args.image_sha256,
                 args.out_dir)), "Live run requires mode, BIN, readback, SHA256 and out-dir")
    bin_path, readback = on_h(args.bin), on_h(args.readback)
    out = on_h(args.out_dir)
    require((FW / "build").resolve() in out.parents,
            "Evidence output directory must be under firmware/build on H:")
    require(out.is_dir(), "Create the H-drive evidence directory before running")
    temp = out / "tmp"
    temp.mkdir(exist_ok=True)
    for key in ("TEMP", "TMP", "TMPDIR", "PYTHONPYCACHEPREFIX"):
        os.environ[key] = str(temp)
    sys.dont_write_bytecode = True
    image = verify_image(bin_path, readback, args.image_sha256)
    require(TRANSPORT.is_file(), "Known fixed ST-LINK VCP transport is missing")
    spec = importlib.util.spec_from_file_location("s4_r43_vcp", TRANSPORT)
    require(spec is not None and spec.loader is not None, "VCP module cannot load")
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    try:
        import winsound
    except ImportError as exc:
        raise RuntimeError("Audible operator cues require Windows") from exc

    prefix = "r4-3-" + args.mode
    summary_path = out / (prefix + "-summary.json")
    events_path = out / (prefix + "-events.jsonl")
    report: dict = {"result": "STARTED_DO_NOT_REPEAT", "mode": args.mode,
                    "image": image, "started_utc": utc(),
                    "events_file": str(events_path), "samples": [],
                    "operator_effort_and_touch_observation": None}
    with summary_path.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    events = events_path.open("x", encoding="utf-8", buffering=1)
    link = None
    prepared = started = stop_attempted = False
    last_seq = -1
    last_sample_ms = -1

    def event(kind: str, **values) -> None:
        events.write(json.dumps({"utc": utc(), "monotonic_ns": time.monotonic_ns(),
                                 "kind": kind, **values}, ensure_ascii=False) + "\n")
        events.flush()

    def query(command: str) -> tuple[str, dict, int]:
        require(command in COMMANDS, "Command outside bounded R4-3 whitelist")
        raw_command = (command + "\n").encode("ascii")
        sent_ns = time.monotonic_ns()
        require(link.device.write(raw_command) == len(raw_command),
                "Incomplete serial write: " + command)
        for _ in range(3):
            raw = link.device.read_until(b"\n", 1025)
            received_ns = time.monotonic_ns()
            line = raw.decode("ascii", errors="backslashreplace").rstrip("\r\n")
            event("serial", command=command, sent_ns=sent_ns,
                  received_ns=received_ns, raw_hex=raw.hex(), reply=line)
            require(raw.endswith(b"\n") and len(raw) <= 1024,
                    "Incomplete/oversize serial reply: " + command)
            if line == WARNING:
                report["warning_count"] = report.get("warning_count", 0) + 1
                continue
            require(line.startswith("OK ") or line.startswith("ERR "),
                    "Unexpected serial reply: " + line)
            return line, base.PARSER.parse(line), received_ns
        raise RuntimeError("No bounded reply after warning: " + command)

    def status() -> dict:
        line, fields, _ = query("followstatus")
        require(line.startswith("OK "), "followstatus failed")
        return fields

    def active_status(fields: dict) -> None:
        require(all(base.number(fields, key) == value for key, value in {
            "phase": 7, "reader": 2, "fresh": 1, "fault": 0,
            "output_allowed": 1, "cmd_torque": 1,
            "ramping_down": 0, "stop": 0,
        }.items()), "FOLLOW no longer active with fresh feedback")
        require(base.number(fields, "ramp_in_ms") > 250,
                "Ramp too near to count active observation")

    def angle_row() -> dict:
        nonlocal last_seq, last_sample_ms
        active_status(status())
        line, fields, received_ns = query("followangle")
        require(line.startswith("OK ") and all(
            base.number(fields, key) == value for key, value in {
                "phase": 7, "reader": 2, "fresh": 1, "angle_valid": 1,
                "output_allowed": 1, "ramping_down": 0, "cmd_torque": 1,
            }.items()), "Angle reply is not fresh active FOLLOW")
        require(fields.get("joint_urad", "na") != "na",
                "Fresh joint angle is unavailable")
        seq = base.number(fields, "seq")
        sample_ms = base.number(fields, "sample_ms")
        age_ms = base.number(fields, "age_ms")
        require(seq >= last_seq and sample_ms >= last_sample_ms and age_ms <= 150,
                "Feedback sequence/time regressed or became stale")
        if seq == last_seq:
            return {}
        require(sample_ms > last_sample_ms,
                "New sequence reuses an old position sample")
        last_seq, last_sample_ms = seq, sample_ms
        angle_deg = int(fields["joint_urad"]) * DEG_PER_URAD
        require(20.5 < angle_deg < 29.5,
                "Fresh joint angle reached the host's 0.5 degree edge margin")
        row = {"at_ns": received_ns, "utc": utc(), "seq": seq,
               "sample_ms": sample_ms, "age_ms": age_ms,
               "joint_urad": int(fields["joint_urad"]),
               "angle_deg": angle_deg}
        report["samples"].append(row)
        return row

    def heartbeat(expected_state: int) -> int:
        hb_line, hb, _ = query("shoulderfeedback")
        clock_line, clock, _ = query("status")
        age = (base.number(clock, "uptime_ms") - base.number(hb, "hb_ms")) & 0xffffffff
        require(hb_line.startswith("OK ") and clock_line.startswith("OK ")
                and base.number(hb, "hb") == 1
                and base.number(hb, "state") == expected_state
                and hb.get("error") == "00000000"
                and base.number(hb, "fault") == 0 and age <= 150,
                "Required fresh motor heartbeat absent")
        return age

    def stop_and_observe() -> None:
        nonlocal stop_attempted
        stop_attempted = True
        line, _, _ = query("followstop")
        report["stop_reply"] = line
        require(line == "OK FOLLOW ramping_down; support arm",
                "Explicit normal withdrawal was not accepted")
        event("operator_cue", cue="SUPPORT_ARM_DURING_RAMP")
        print("STOP_RAMP: support the arm until STOPPED and IDLE", flush=True)
        deadline = time.monotonic_ns() + 3_000_000_000
        saw_ramp = False
        while time.monotonic_ns() < deadline:
            f = status()
            phase = base.number(f, "phase")
            require(base.number(f, "fault") == 0,
                    "Fault during normal withdrawal")
            if phase == 8 and base.number(f, "ramping_down") == 1:
                saw_ramp = True
            if phase == 3:
                require(all(base.number(f, key) == value for
                        key, value in {"reader": 7, "stop": 1,
                                     "output_allowed": 0, "cmd_torque": 0}.items()),
                        "STOPPED/reader stop evidence incomplete")
                report["ramp_phase_seen"] = saw_ramp
                reader_line, reader, _ = query("s3reader")
                require(reader_line.startswith("OK ")
                        and base.number(reader, "state") == 7
                        and base.number(reader, "reason") == 0
                        and base.number(reader, "observed") == 1,
                        "No fresh IDLE observed in reader tail window")
                report["stopped"] = f
                report["reader_after"] = reader
                report["idle_heartbeat_age_ms"] = heartbeat(1)
                return
            require(phase in (8, 9), "Unexpected phase during withdrawal")
            time.sleep(0.07)
        raise RuntimeError("STOPPED not observed within 3 s; operator must cut 24 V")

    try:
        binding = base.BINDING.binding_probe()
        require(binding.get("serial_verified") is True
                and len(binding["ports"]) == 1
                and binding["ports"][0]["DeviceID"].upper()
                == args.expected_port.upper(),
                "Fixed ST-LINK VCP identity/expected COM port mismatch")
        report["binding"] = binding
        link = base.Link(binding["ports"][0]["DeviceID"], [])
        cold = status()
        require(all(base.number(cold, key) == value for key, value in {
            "phase": 1, "reader": 1, "seq": 0, "zero": 0,
            "output_allowed": 0, "cmd_torque": 0, "fault": 0,
        }.items()), "Not cold SAFE_OFF with unused reader")
        report["cold"] = cold
        report["idle_heartbeat_age_ms_before"] = heartbeat(1)
        shoulder_line, shoulder_before, _ = query("shoulder")
        require(shoulder_line.startswith("OK ") and all(
            base.number(shoulder_before, key) == 0 for key in
            ("drop", "invalid", "fov", "err", "bus_off")),
            "Shoulder CAN has pre-session errors")
        report["shoulder_before"] = shoulder_before

        prep_at = time.monotonic_ns()
        prepared = True
        prep_line, _, _ = query("followprepare")
        require(prep_line == "OK FOLLOW preparing output_allowed=0",
                "followprepare not accepted")
        zero_deadline = prep_at + 4_000_000_000
        while time.monotonic_ns() < zero_deadline:
            f = status()
            require(base.number(f, "phase") == 5
                    and base.number(f, "fault") == 0
                    and base.number(f, "output_allowed") == 0
                    and base.number(f, "cmd_torque") == 0,
                    "Unexpected output during RAM-zero preparation")
            if base.number(f, "zero") == 1:
                report["zero"] = f
                break
            time.sleep(0.07)
        else:
            raise RuntimeError("RAM vertical zero not obtained within 4 s")
        initial_lo, initial_hi = ((20.5, 24.5) if args.mode == "low"
                                  else (25.5, 29.0))
        event("operator_cue", cue="LIFT_SLOWLY_UNTIL_LONG_BEEP",
              accept_lower_deg=initial_lo, accept_upper_deg=initial_hi)
        print("ZERO_READY: lift VERY slowly from vertical (about 4 deg/s); stop at the START of the long beep",
              flush=True)
        for count in range(3):
            winsound.Beep(1200, 130)
            if count < 2:
                time.sleep(0.05)
        ready = None
        move_cue_sent = False
        while time.monotonic_ns() - prep_at < PREPARE_DEADLINE_NS:
            f = status()
            phase = base.number(f, "phase")
            require(phase in (5, 6) and base.number(f, "zero") == 1
                    and base.number(f, "fault") == 0
                    and base.number(f, "output_allowed") == 0
                    and base.number(f, "cmd_torque") == 0,
                    "Preparation left safe phase or faulted")
            if base.number(f, "fresh") == 1 and f.get("joint_mrad") != "na":
                deg = int(f["joint_mrad"]) * 180.0 / (math.pi * 1000.0)
                require(deg < 29.0, "Manual move approached the 30 degree edge")
                require(deg <= initial_hi,
                        "Manual move passed the selected posture interval")
                require(move_cue_sent or deg <= initial_cue_ceiling(args.mode),
                        "Early audio cue was missed; stop and move more slowly next cold boot")
                if not move_cue_sent and initial_cue_allowed(args.mode, deg):
                    move_cue_sent = True
                    report["initial_hold_cue"] = {
                        "at_ns": time.monotonic_ns(), "angle_deg": deg,
                        "seq": base.number(f, "seq")}
                    event("operator_cue", cue="LONG_BEEP_STOP_AND_HOLD",
                          angle_deg=deg, seq=base.number(f, "seq"))
                    print(f"LONG_BEEP: stop at the start of this tone and hold near {deg:.1f} degrees",
                          flush=True)
                    winsound.Beep(1800, 420)
                    # Re-query after the blocking tone. READY observed before
                    # the cue is not evidence that the arm stayed still after it.
                    continue
                if (move_cue_sent and phase == 6
                        and base.number(f, "reader") == 2
                        and initial_lo <= deg <= initial_hi):
                    ready = f
                    break
            time.sleep(0.07)
        require(ready is not None,
                "Fresh firmware READY at initial posture absent before 19 s TTL")
        report["ready"] = ready
        require(time.monotonic_ns() - prep_at < 17_000_000_000,
                "READY too late for 1.5 s no-compensation baseline before prepare TTL")
        event("operator_cue", cue="READY_ZERO_TORQUE_BASELINE_START",
              lower_deg=initial_lo, upper_deg=initial_hi)
        print("BASELINE: keep the same hand position for 1.5 s with zero torque target",
              flush=True)
        winsound.Beep(700, 80)
        baseline_at = time.monotonic_ns()
        baseline_rows = []
        baseline_seq = -1
        while time.monotonic_ns() - baseline_at < 1_500_000_000:
            f = status()
            require(all(base.number(f, key) == value for key, value in {
                "phase": 6, "reader": 2, "fresh": 1, "zero": 1,
                "fault": 0, "output_allowed": 0, "cmd_torque": 0,
            }.items()), "READY zero-target baseline became invalid")
            require(f.get("joint_mrad") != "na", "Baseline angle absent")
            deg = int(f["joint_mrad"]) * 180.0 / (math.pi * 1000.0)
            require(initial_lo <= deg <= initial_hi,
                    "Baseline posture left its comparison interval")
            seq = base.number(f, "seq")
            if seq > baseline_seq:
                baseline_seq = seq
                baseline_rows.append({"at_ns": time.monotonic_ns(),
                                      "seq": seq, "angle_deg": deg})
            time.sleep(0.065)
        require(len(baseline_rows) >= 5, "Too few fresh READY baseline samples")
        require(max(row["angle_deg"] for row in baseline_rows)
                - min(row["angle_deg"] for row in baseline_rows) <= 0.5,
                "READY baseline did not remain at one posture")
        report["ready_zero_target_baseline"] = {
            "duration_s": (time.monotonic_ns() - baseline_at) / 1_000_000_000,
            "samples": len(baseline_rows),
            "median_deg": statistics.median(row["angle_deg"] for row in baseline_rows),
            "first_seq": baseline_rows[0]["seq"],
            "last_seq": baseline_rows[-1]["seq"],
            "effort_report_pending_operator": True,
            "note": "ClosedLoop READY with zero torque target; hand effort not measured by firmware",
        }
        event("operator_cue", cue="READY_ZERO_TORQUE_BASELINE_END")
        require(time.monotonic_ns() - prep_at < PREPARE_DEADLINE_NS,
                "Prepare TTL margin expired before followstart")
        started = True
        start_line, _, start_at = query("followstart")
        start_match = re.fullmatch(
            r"OK FOLLOW started; expires_in_ms=([0-9]+); "
            r"ramp_in_ms=([0-9]+); support arm before ramp", start_line)
        require(start_match is not None, "followstart reply malformed")
        start_remaining_ms = int(start_match.group(1))
        start_ramp_in_ms = int(start_match.group(2))
        require(9900 <= start_remaining_ms <= 10000
                and start_ramp_in_ms == start_remaining_ms - 1000,
                "10 s session / 1 s ramp budget was not accepted")
        report["start_reply"] = start_line
        report["start_remaining_ms"] = start_remaining_ms
        report["start_ramp_in_ms"] = start_ramp_in_ms
        report["started_monotonic_ns"] = start_at
        event("operator_cue", cue="FOLLOW_STARTED")
        print("FOLLOW_STARTED: hold the same posture for assisted comparison",
              flush=True)
        active_deadline = start_at + 400_000_000
        while time.monotonic_ns() < active_deadline:
            f = status()
            require(base.number(f, "phase") == 7
                    and base.number(f, "fault") == 0
                    and base.number(f, "ramping_down") == 0,
                    "FOLLOW faulted before first active sample")
            if base.number(f, "cmd_torque") == 1:
                active_status(f)
                break
            time.sleep(0.035)
        else:
            raise RuntimeError("No active torque command within 0.4 s")

        if args.mode == "low":
            low_rows: list[dict] = []
            deadline = start_at + 2_200_000_000
            while time.monotonic_ns() < deadline:
                row = angle_row()
                if row:
                    low_rows.append(row)
                    require(initial_lo < row["angle_deg"] <= initial_hi,
                            "Low posture left its comparison interval")
                time.sleep(0.055)
            require(len(low_rows) >= 8,
                    "Insufficient low-posture active fresh samples")
            report["low_posture"] = {
                "samples": len(low_rows),
                "median_deg": statistics.median(row["angle_deg"] for row in low_rows),
                "first_seq": low_rows[0]["seq"], "last_seq": low_rows[-1]["seq"],
                "operator_effort_comparison": None,
            }
            report["low_posture"]["baseline_assisted_delta_deg"] = abs(
                report["low_posture"]["median_deg"]
                - report["ready_zero_target_baseline"]["median_deg"])
            require(report["low_posture"]["baseline_assisted_delta_deg"] <= 0.5,
                    "Low baseline and assisted medians differ by more than 0.5 degree")
        else:
            high_rows = []
            while time.monotonic_ns() - start_at < REPOSITION_CUE_NS:
                row = angle_row()
                if row:
                    high_rows.append(row)
                    require(initial_lo <= row["angle_deg"] <= initial_hi,
                            "Start posture left high comparison interval before move cue")
                time.sleep(0.055)
            require(len(high_rows) >= 3, "No distinct active high-posture samples")
            report["high_assisted_hold"] = {
                "samples": len(high_rows),
                "median_deg": statistics.median(row["angle_deg"] for row in high_rows),
                "operator_effort_comparison": None,
            }
            report["high_assisted_hold"]["baseline_assisted_delta_deg"] = abs(
                report["high_assisted_hold"]["median_deg"]
                - report["ready_zero_target_baseline"]["median_deg"])
            require(report["high_assisted_hold"]["baseline_assisted_delta_deg"] <= 0.5,
                    "High baseline and assisted medians differ by more than 0.5 degree")
            event("operator_cue", cue="SLOW_REPOSITION_DOWN_UNTIL_LONG_BEEP")
            print("MOVE_NOW: gently lower the arm until the next long beep, then hold", flush=True)
            winsound.Beep(1000, 120)
            target_rows: list[dict] = []
            release_ns = None
            reposition_cue_sent = False
            while time.monotonic_ns() - start_at < RELEASE_DEADLINE_NS:
                row = angle_row()
                if row:
                    if not reposition_cue_sent and reposition_cue_allowed(row["angle_deg"]):
                        reposition_cue_sent = True
                        report["reposition_hold_cue"] = {
                            "at_ns": time.monotonic_ns(),
                            "angle_deg": row["angle_deg"], "seq": row["seq"]}
                        event("operator_cue", cue="LONG_BEEP_STOP_REPOSITION_AND_HOLD",
                              angle_deg=row["angle_deg"], seq=row["seq"])
                        print(f"LONG_BEEP: stop lowering and hold near {row['angle_deg']:.1f} degrees",
                              flush=True)
                        winsound.Beep(1550, 350)
                    if reposition_cue_sent and 20.5 < row["angle_deg"] <= 24.5:
                        target_rows.append(row)
                        target_rows = [point for point in target_rows
                                       if point["at_ns"] >= row["at_ns"] - 500_000_000]
                        if (len(target_rows) >= 3
                                and target_rows[-1]["at_ns"] - target_rows[0]["at_ns"]
                                >= 200_000_000
                                and max(point["angle_deg"] for point in target_rows)
                                - min(point["angle_deg"] for point in target_rows) <= 0.5):
                            f = status()
                            active_status(f)
                            require(base.number(f, "ramp_in_ms") >= 5800,
                                    "Less than 5.5 s observation plus stop margin")
                            winsound.Beep(1800, 90)
                            release_ns = time.monotonic_ns()
                            event("operator_cue", cue="RELEASE_NOW", marker_ns=release_ns,
                                  note="Audible cue end, not verified physical release")
                            print("RELEASE_NOW: let go, do not touch for 5.5 seconds", flush=True)
                            break
                    else:
                        target_rows.clear()
                time.sleep(0.045)
            require(release_ns is not None,
                    "Stable reposition after long-beep cue not reached by 3 s")
            report["release_cue_monotonic_ns"] = release_ns
            report["release_cue_utc"] = utc()
            report["release_marker_scope"] = (
                "Timestamp at end of audible cue; user must confirm actual release "
                "and no retouch separately")
            window_end = release_ns + SETTLE_NS + DRIFT_NS
            while time.monotonic_ns() < window_end:
                angle_row()
                time.sleep(0.045)
            report["drift"] = analyze_drift(report["samples"], release_ns)
            require(report["drift"]["numeric_pass"],
                    "Five-second numeric drift criterion failed")

        stop_and_observe()
        shoulder_line, shoulder_after, _ = query("shoulder")
        require(shoulder_line.startswith("OK ") and all(
            base.number(shoulder_after, key) == base.number(shoulder_before, key)
            for key in ("drop", "invalid", "fov", "err", "bus_off")),
            "Shoulder CAN error counter changed")
        report["shoulder_after"] = shoulder_after
        report["result"] = ("LOW_ACTIVE_OBSERVED_PENDING_OPERATOR"
                            if args.mode == "low" else
                            "DRIFT_NUMERIC_PASS_PENDING_OPERATOR")
    except BaseException as exc:
        report["result"] = "FAIL_OR_INCOMPLETE"
        report["reason"] = str(exc)
        if link is not None and prepared and not stop_attempted:
            stop_attempted = True
            try:
                report["emergency_followstop_reply"] = query("followstop")[0]
            except BaseException as stop_exc:
                report["emergency_followstop_error"] = str(stop_exc)
        event("operator_cue", cue="SUPPORT_ARM_AND_TURN_OFF_24V_IF_STOP_UNCONFIRMED",
              reason=str(exc))
        print("ABORT: support arm; if stop is not confirmed, turn off 24 V", flush=True)
        raise
    finally:
        if link is not None:
            link.close()
        events.close()
        report["prepared"] = prepared
        report["followstart_attempted"] = started
        report["followstop_attempted"] = stop_attempted
        report["finished_utc"] = utc()
        summary_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                encoding="utf-8")
        print(json.dumps({"result": report["result"], "reason": report.get("reason"),
                          "mode": args.mode, "evidence": str(summary_path)},
                         ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
