"""One bounded R4-3 single-posture effort comparison; --run can command torque.

The operator keeps the same elbow, load, support point and grip through a
roughly one-second zero-target READY baseline and three-second assisted hold.
Audible cues mark the comparison phases; they do not prove hand effort or
physical torque. Do not release the arm. This script never flashes, resets,
switches the supply, retries a run, or declares a gravity-effect pass.

This host-only variant reuses capture_once.py's H-drive image verification and
the existing fixed ST-LINK VCP transport. Firmware and its safety limits are
unchanged. Each live run needs a new cold boot and an empty evidence directory.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
import re
import statistics
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
from capture_once import on_h, require, utc, verify_image  # noqa: E402


HERE = Path(__file__).resolve().parent
FW = HERE.parents[2]
TRANSPORT = FW / "build/r4-1-serial-observe.py"
PREPARE_DEADLINE_NS = 19_000_000_000  # firmware prepare TTL is 20 s
BASELINE_NS = 1_000_000_000
ASSIST_NS = 3_000_000_000
DEG_PER_URAD = 180.0 / (math.pi * 1_000_000.0)
MIN_DEG = 20.5  # host margin inside the unchanged firmware 20-30 degree window
MAX_DEG = 29.5
CUE_DEG = 21.0  # cue on first entry into the broad interior; no narrow target
CUE_MAX_DEG = 27.0  # leave time to stop before the 30 degree firmware edge
COMMANDS = frozenset({"followprepare", "followstatus", "followangle",
                      "followstart", "followstop", "shoulderfeedback",
                      "shoulder", "status", "s3reader"})
WARNING = "WARN FOLLOW ending soon; support arm; torque will ramp down"


def interior(angle_deg: float) -> bool:
    return MIN_DEG < angle_deg < MAX_DEG


def summarize_angles(rows: list[dict]) -> dict:
    require(rows, "No fresh posture samples")
    angles = [row["angle_deg"] for row in rows]
    return {"samples": len(rows), "median_deg": statistics.median(angles),
            "min_deg": min(angles), "max_deg": max(angles),
            "first_seq": rows[0]["seq"], "last_seq": rows[-1]["seq"]}


def offline_check() -> None:
    require(interior(22.0) and interior(28.5) and CUE_MAX_DEG > CUE_DEG
            and not interior(20.0) and not interior(20.5)
            and not interior(29.5) and not interior(30.0),
            "Host interior check escaped the firmware window")
    baseline = summarize_angles([{"seq": 1, "angle_deg": 22.0},
                                 {"seq": 2, "angle_deg": 22.1}])
    assisted = summarize_angles([{"seq": 3, "angle_deg": 22.2},
                                 {"seq": 4, "angle_deg": 22.3}])
    require(baseline["median_deg"] == 22.05
            and assisted["median_deg"] == 22.25
            and ASSIST_NS + 500_000_000 < 9_000_000_000,
            "Descriptive comparison or 10 s session budget invalid")
    # A large descriptive angle difference is not an automatic effect pass.
    require(summarize_angles([{"seq": 5, "angle_deg": 22.0},
                              {"seq": 6, "angle_deg": 24.0}])["max_deg"] == 24.0,
            "Descriptive angle summary failed")
    print("offline single-posture check PASS; no serial or motor access", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true",
                        help="check host-only comparison helpers; no serial access")
    parser.add_argument("--run", action="store_true",
                        help="one authorized powered session")
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
    require(all((args.bin, args.readback, args.image_sha256, args.out_dir)),
            "Live run requires BIN, readback, SHA256 and out-dir")
    bin_path, readback = on_h(args.bin), on_h(args.readback)
    out = on_h(args.out_dir)
    require((FW / "build").resolve() in out.parents,
            "Evidence output directory must be under firmware/build on H:")
    require(out.is_dir(), "Create the H-drive evidence directory before running")
    temp = out / "tmp"
    temp.mkdir(exist_ok=True)
    for key in ("TEMP", "TMP", "TMPDIR", "PYTHONPYCACHEPREFIX"):
        os.environ[key] = str(temp)
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

    summary_path = out / "r4-3-effect-summary.json"
    events_path = out / "r4-3-effect-events.jsonl"
    report: dict = {
        "result": "STARTED_DO_NOT_REPEAT", "mode": "single_posture_effort",
        "image": image, "started_utc": utc(), "events_file": str(events_path),
        "samples": [],
        "operator_observation": {
            "same_elbow_load_grip_and_support": None,
            "zero_target_effort_or_force": None,
            "assisted_effort_or_force": None,
            "assistance_clearly_reduced_effort": None,
            "self_motion_oscillation_or_boundary_contact": None,
            "note": "Fill from the operator after the run; never infer force from command state"
        },
    }
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

    def active_angle_row() -> dict:
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
        require(interior(angle_deg), "Fresh joint angle reached the host edge margin")
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
        print("STOP_RAMP: keep supporting the arm until STOPPED and IDLE", flush=True)
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

        print("ZERO_READY: after three beeps, lift slowly; stop and support at the long beep",
              flush=True)
        event("operator_cue", cue="LIFT_SLOWLY_UNTIL_LONG_BEEP",
              firmware_window_deg=[20.0, 30.0])
        for count in range(3):
            winsound.Beep(1200, 130)
            if count < 2:
                time.sleep(0.05)
        ready = None
        hold_cue_sent = False
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
                require(deg < MAX_DEG, "Manual move approached the 30 degree edge")
                if not hold_cue_sent:
                    require(deg <= CUE_MAX_DEG,
                            "Manual move passed the broad hold cue range")
                if not hold_cue_sent and deg >= CUE_DEG:
                    hold_cue_sent = True
                    report["hold_cue"] = {"at_ns": time.monotonic_ns(),
                                          "angle_deg": deg,
                                          "seq": base.number(f, "seq")}
                    event("operator_cue", cue="LONG_BEEP_STOP_AND_HOLD",
                          angle_deg=deg, seq=base.number(f, "seq"))
                    print("LONG_BEEP: stop moving; keep the same grip and support point",
                          flush=True)
                    winsound.Beep(1800, 420)
                    continue
                if hold_cue_sent and phase == 6 and base.number(f, "reader") == 2 \
                        and interior(deg):
                    ready = f
                    break
            time.sleep(0.07)
        require(ready is not None,
                "Fresh firmware READY inside the window absent before prepare TTL")
        report["ready"] = ready
        # The baseline and a bounded command reply must still fit the 20 s
        # firmware prepare TTL; no fixed, narrow posture-timing gate is used.
        require(time.monotonic_ns() + BASELINE_NS + 400_000_000
                < prep_at + 20_000_000_000,
                "READY too late for zero-target comparison before prepare TTL")
        print("BASELINE: same grip and support, zero target for about 1 second",
              flush=True)
        winsound.Beep(700, 100)
        baseline_at = time.monotonic_ns()
        event("operator_cue", cue="ZERO_TARGET_BASELINE_START", marker_ns=baseline_at)
        baseline_rows = []
        baseline_seq = -1
        while time.monotonic_ns() - baseline_at < BASELINE_NS:
            f = status()
            require(all(base.number(f, key) == value for key, value in {
                "phase": 6, "reader": 2, "fresh": 1, "zero": 1,
                "fault": 0, "output_allowed": 0, "cmd_torque": 0,
            }.items()), "READY zero-target baseline became invalid")
            require(f.get("joint_mrad") != "na", "Baseline angle absent")
            deg = int(f["joint_mrad"]) * 180.0 / (math.pi * 1000.0)
            require(interior(deg), "Baseline reached host edge margin")
            seq = base.number(f, "seq")
            if seq > baseline_seq:
                baseline_seq = seq
                baseline_rows.append({"at_ns": time.monotonic_ns(),
                                      "seq": seq, "angle_deg": deg})
            time.sleep(0.065)
        require(len(baseline_rows) >= 4,
                "Too few fresh READY zero-target samples")
        report["ready_zero_target_baseline"] = {
            **summarize_angles(baseline_rows),
            "duration_s": (time.monotonic_ns() - baseline_at) / 1_000_000_000,
            "note": "ClosedLoop READY with zero torque target; hand force not measured",
        }
        event("operator_cue", cue="ZERO_TARGET_BASELINE_END")
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
        print("ASSIST: higher beep begins about 3 seconds of assisted comparison; keep support",
              flush=True)
        winsound.Beep(1500, 170)
        assisted_at = time.monotonic_ns()
        event("operator_cue", cue="ASSISTED_COMPARISON_START",
              marker_ns=assisted_at,
              note="Command state observed; physical torque and effort not measured")
        assisted_rows: list[dict] = []
        while time.monotonic_ns() - assisted_at < ASSIST_NS:
            row = active_angle_row()
            if row:
                assisted_rows.append(row)
            time.sleep(0.055)
        require(len(assisted_rows) >= 8,
                "Insufficient assisted fresh posture samples")
        report["assisted_posture"] = {
            **summarize_angles(assisted_rows),
            "duration_s": (time.monotonic_ns() - assisted_at) / 1_000_000_000,
        }
        report["baseline_assisted_median_delta_deg"] = abs(
            report["assisted_posture"]["median_deg"]
            - report["ready_zero_target_baseline"]["median_deg"])
        report["angle_delta_is_descriptive_only"] = True
        event("operator_cue", cue="ASSISTED_COMPARISON_END")
        stop_and_observe()
        shoulder_line, shoulder_after, _ = query("shoulder")
        require(shoulder_line.startswith("OK ") and all(
            base.number(shoulder_after, key) == base.number(shoulder_before, key)
            for key in ("drop", "invalid", "fov", "err", "bus_off")),
            "Shoulder CAN error counter changed")
        report["shoulder_after"] = shoulder_after
        report["result"] = "CONTROL_TRACE_READY_EFFECT_PENDING_OPERATOR"
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
                          "evidence": str(summary_path)}, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
