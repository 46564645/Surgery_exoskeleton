"""One bounded R4-3 effort repeat with cached speed/target telemetry.

The operator keeps the same elbow, load, support point and grip through a
roughly one-second zero-target READY baseline and three-second assisted hold.
The opt-in closeout uses a two-second READY baseline and an OFF/ON IDLE
comparison before the same bounded FOLLOW session. --direct-idle explicitly
skips the OFF movement comparison and waits for operator-completed ON IDLE.
With --measure-only, the operator stops and supports at any posture inside the
host's 20.5-29.5 degree margin. The long beep marks validated zero-target
READY; the subsequent two-second baseline checks that the posture is held.
The opt-in --operator-triggered mode instead takes RAM zero at supported
vertical, then waits for the operator's message after holding a chosen angle.
It omits the host posture/angle-change gates and the READY force baseline;
firmware must still accept followstart. It compares about three seconds of
FOLLOW with about two seconds after active stop at the same gauge point.
Audible cues mark the comparison phases; they do not prove hand effort or
physical torque. Do not release the arm. This script never flashes, resets,
switches the supply, retries a run, or declares a gravity-effect pass.

This host-only variant reuses capture_once.py's H-drive image verification and
the existing fixed ST-LINK VCP transport. The added s3feedback/s3properties
queries read cached position, velocity and target torque; they do not read
mechanical force or independently measure speed. Firmware and its safety
limits are unchanged. Each live run needs a new cold boot and an empty
evidence directory.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
import re
import statistics
import struct
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
CLOSEOUT_BASELINE_NS = 2_000_000_000
CLOSEOUT_IDLE_WINDOW_NS = 8_000_000_000
CLOSEOUT_DIRECT_IDLE_MAX_NS = 120_000_000_000
CLOSEOUT_POWER_WAIT_NS = 5_000_000_000
CLOSEOUT_PREPARE_GATE_S = 120.0  # cross-chat human response; before prepare TTL starts
CLOSEOUT_STOP_BY_NS = 6_500_000_000  # aim 0.5 s before the start+7 s stop bound
CLOSEOUT_QUERY_CUTOFF_NS = 6_000_000_000  # leave one bounded query and stop reply
CLOSEOUT_SERIAL_TIMEOUT_S = 0.08  # closeout-only VCP read/write timeout
CLOSEOUT_PUSH_NS = 900_000_000
CLOSEOUT_SETTLE_NS = 750_000_000
CLOSEOUT_CUE_DEG = 24.5
CLOSEOUT_CUE_MAX_DEG = 25.5
CLOSEOUT_REPOSITION_SPAN_DEG = 2.0
CLOSEOUT_BASELINE_LATEST_NS = 16_500_000_000  # 2 s baseline and followstart margin before 20 s TTL
CLOSEOUT_READY_GUARD_MAX_NS = 500_000_000  # abort a stalled host validation loop
CLOSEOUT_SAME_POSTURE_MAX_DELTA_DEG = 0.5  # screen nominal angle; video still judges actual posture/force
CLOSEOUT_READY_SPAN_DEG = 0.25  # at 20-30 deg, ideal sin(q) changes by at most about 1.2%
CLOSEOUT_READY_PEAK_SPEED_DEG_S = 2.5  # above run3's 1.72 deg/s; below firmware's 15 deg/s safety gate
DEG_PER_MRAD = 180.0 / (math.pi * 1000.0)
DEG_PER_URAD = 180.0 / (math.pi * 1_000_000.0)
MIN_DEG = 20.5  # host margin inside the unchanged firmware 20-30 degree window
MAX_DEG = 29.5
CUE_DEG = 21.0  # cue on first entry into the broad interior; no narrow target
CUE_MAX_DEG = 27.0  # leave time to stop before the 30 degree firmware edge
COMMANDS = frozenset({"followprepare", "followstatus", "followangle",
                      "followstart", "followstop", "shoulderfeedback",
                      "shoulder", "status", "s3reader", "s3feedback",
                      "s3properties"})
WARNING = "WARN FOLLOW ending soon; support arm; torque will ramp down"
GAIN36_IMAGE_SHA256 = "6dd40cfe39dab38a1a5a7b0cb07d4b65b4c658a45e74d226fc67bd9e54f5a180"
GAIN36_SPEED25_IMAGE_SHA256 = "97898853fd5a630808d33aaec631229d48d1ee4912497091fe964856f9008677"
GAIN36_SPEED30_IMAGE_SHA256 = "0e41af6e5d07483bbc8cadf167c65de6810164012f14c62e98a8ecf7326126b5"
GAIN36_SPEED40_IMAGE_SHA256 = "2fe8248317b179931918518c452c6317c974bb67297883f107cc4f3b6bcf6f8c"


def select_target_cap_mnm(target_cap_mnm: int, image_sha256: str | None) -> int:
    require(target_cap_mnm in (20, 40), "Target cap must be 20 or 40 mNm")
    if target_cap_mnm == 40:
        require(image_sha256 is not None
                and image_sha256.lower() in (GAIN36_IMAGE_SHA256,
                                             GAIN36_SPEED25_IMAGE_SHA256,
                                             GAIN36_SPEED30_IMAGE_SHA256,
                                             GAIN36_SPEED40_IMAGE_SHA256),
                "40 mNm target guard requires the authorized gain36 image SHA256")
    return target_cap_mnm


def unsigned(fields: dict, key: str) -> int:
    value = fields.get(key, "")
    require(re.fullmatch(r"[0-9]+", value) is not None, "Bad field: " + key)
    return int(value)


def closeout_query_budget(now_ns: int, deadline_ns: int | None,
                          stop_attempted: bool, command: str) -> None:
    """Do not begin another active telemetry query after the stop reserve."""
    if deadline_ns is not None and not stop_attempted and command != "followstop":
        require(now_ns < deadline_ns,
                "Active serial query reserve exhausted; attempt stop immediately")


def closeout_idle_guard(query, powered: bool) -> dict:
    """Read only. Never starts the S4 reader or changes a motor mode."""
    follow_line, follow, _ = query("followstatus")
    status_line, platform, _ = query("status")
    require(follow_line.startswith("OK ") and status_line.startswith("OK "),
            "IDLE status query failed")
    require(all(unsigned(follow, key) == value for key, value in {
        "phase": 1, "reader": 1, "seq": 0, "zero": 0, "fault": 0,
        "output_allowed": 0, "cmd_torque": 0,
    }.items()) and unsigned(platform, "output_allowed") == 0,
            "IDLE/SAFE_OFF or output permission changed before prepare")
    result = {"phase": 1, "reader": 1, "output_allowed": 0, "cmd_torque": 0}
    if powered:
        hb_line, hb, _ = query("shoulderfeedback")
        require(hb_line.startswith("OK "), "Powered IDLE heartbeat query failed")
        require(unsigned(hb, "hb") == 1, "Powered IDLE heartbeat not yet received")
        require(unsigned(hb, "state") == 1 and unsigned(hb, "fault") == 0
                and hb.get("error") == "00000000", "Powered IDLE heartbeat unhealthy")
        # Pair the heartbeat with a status sampled after its reply. Reading the
        # clock first can underflow when a new CAN heartbeat arrives between
        # the two asynchronous VCP queries (observed in closeout run3).
        clock_line, clock, _ = query("status")
        require(clock_line.startswith("OK ") and unsigned(clock, "output_allowed") == 0,
                "Powered IDLE output permission changed after heartbeat")
        age = (unsigned(clock, "uptime_ms") - unsigned(hb, "hb_ms")) & 0xffffffff
        require(age <= 150, "Powered IDLE heartbeat not yet fresh")
        result["heartbeat_age_ms"] = age
    return result


def closeout_before_prepare(query, gate, abort_requested, cue, event,
                            now_ns, sleep, report, prepare):
    """One operator-gated OFF/ON IDLE comparison on the same MCU cold boot.

    The OFF stage deliberately makes no heartbeat query. The only motion command
    in this function is prepare(), after every powered IDLE and operator gate.
    """
    require(gate("CONFIRM_24V_OFF_SUPPORTED", CLOSEOUT_PREPARE_GATE_S),
            "Operator did not confirm 24 V OFF and independent support")
    report["power_off_basis"] = "operator confirmation; MCU has no 24 V input"
    off_first = closeout_idle_guard(query, False)
    event("video_sync", pattern="two_short_beeps", video_reference=report["metrology"]["video_reference"])
    cue("video_sync")
    print("24V OFF IDLE: AFTER ONE LOW BEEP, the 8-second window starts. Slowly lift forward at the marked support point and return to vertical WITHIN this window. TWO LOW BEEPS mark its end; no later movement counts. Keep support; N/Esc aborts", flush=True)
    cue("off_move")
    off_start = now_ns()
    event("operator_cue", cue="OFF_IDLE_MOVE_START", marker_ns=off_start,
          duration_s=CLOSEOUT_IDLE_WINDOW_NS / 1e9)
    while now_ns() - off_start < CLOSEOUT_IDLE_WINDOW_NS:
        require(not abort_requested(), "Operator aborted 24 V OFF comparison")
        closeout_idle_guard(query, False)
        sleep(0.12)
    off_end = now_ns()
    event("operator_cue", cue="OFF_IDLE_MOVE_END", marker_ns=off_end)
    cue("off_move_end")
    print("24V OFF IDLE END: two low beeps; stop this comparison and support the arm vertical. Press Y only if the lift and return were completed during the 8 seconds", flush=True)
    report["off_idle"] = {"first": off_first, "duration_s": (off_end - off_start) / 1e9,
                          "force_measured_by_script": False}
    require(gate("OFF_MOVE_DONE_RETURNED_ZERO", CLOSEOUT_PREPARE_GATE_S),
            "OFF comparison or vertical return was not confirmed")
    require(gate("CONFIRM_24V_ON_SUPPORTED", CLOSEOUT_PREPARE_GATE_S),
            "Operator did not confirm 24 V ON and independent support")
    report["power_on_operator_confirmed"] = True
    event("operator_cue", cue="WAIT_FOR_POWERED_IDLE_HEARTBEAT")
    power_start = now_ns()
    ready = None
    while now_ns() - power_start < CLOSEOUT_POWER_WAIT_NS:
        require(not abort_requested(), "Operator aborted powered IDLE wait")
        # During driver startup a heartbeat can be absent; SAFE_OFF must remain.
        closeout_idle_guard(query, False)
        try:
            ready = closeout_idle_guard(query, True)
        except RuntimeError as exc:
            ready = None
            if str(exc) not in ("Powered IDLE heartbeat not yet received",
                                "Powered IDLE heartbeat not yet fresh"):
                raise
        sleep(0.12)
    require(ready is not None, "No fresh healthy powered IDLE heartbeat within 5 s")
    print("24V ON IDLE, still NO prepare: AFTER ONE HIGH BEEP, the 8-second window starts. Repeat the SAME slow forward lift and return to vertical WITHIN this window. TWO HIGH BEEPS mark its end; no later movement counts. Unusual drag: STOP PUSH, support, cut 24V and press N/Esc immediately", flush=True)
    cue("on_move")
    on_start = now_ns()
    event("operator_cue", cue="ON_IDLE_MOVE_START", marker_ns=on_start,
          duration_s=CLOSEOUT_IDLE_WINDOW_NS / 1e9)
    on_samples = 0
    while now_ns() - on_start < CLOSEOUT_IDLE_WINDOW_NS:
        require(not abort_requested(), "Operator aborted powered IDLE comparison")
        closeout_idle_guard(query, True)
        on_samples += 1
        sleep(0.12)
    on_end = now_ns()
    event("operator_cue", cue="ON_IDLE_MOVE_END", marker_ns=on_end)
    cue("on_move_end")
    print("24V ON IDLE END: two high beeps; stop this comparison and support the arm vertical. Press Y only if powered IDLE felt acceptable and the lift and return were completed during the 8 seconds", flush=True)
    report["on_idle"] = {"samples": on_samples, "duration_s": (on_end - on_start) / 1e9,
                         "force_measured_by_script": False}
    shoulder_line, shoulder, _ = query("shoulder")
    require(shoulder_line.startswith("OK ") and all(
        unsigned(shoulder, key) == 0 for key in ("drop", "invalid", "fov", "err", "bus_off")),
        "CAN error during powered IDLE comparison")
    report["shoulder_before"] = shoulder
    print("FINAL IDLE GATE: Y authorizes followprepare immediately after one last read-only status check; N/Esc cancels without prepare", flush=True)
    require(gate("IDLE_ACCEPTABLE_SAME_POINT_RETURNED_ZERO", CLOSEOUT_PREPARE_GATE_S),
            "Operator declined powered IDLE comparison or vertical return")
    report["idle_comparison_operator_accepted"] = True
    closeout_idle_guard(query, True)
    event("operator_cue", cue="CLOSEOUT_PREPARE_AUTHORIZED_BY_OPERATOR")
    return prepare()


def closeout_direct_idle_before_prepare(query, gate, completion_key,
                                        abort_requested, cue, event, now_ns,
                                        sleep, report, prepare):
    """Explicit direct-IDLE branch; only operator completion ends the ON comparison."""
    require(gate("CONFIRM_24V_OFF_SUPPORTED", CLOSEOUT_PREPARE_GATE_S),
            "Operator did not confirm cold 24 V OFF and independent support")
    report["power_off_basis"] = "operator confirmation; MCU has no 24 V input"
    closeout_idle_guard(query, False)
    report["off_idle"] = {"comparison_skipped": True,
                          "reason": "explicit --direct-idle; no OFF lift or force reading"}
    event("operator_cue", cue="OFF_IDLE_COMPARISON_SKIPPED")
    event("video_sync", pattern="two_short_beeps",
          video_reference=report["metrology"]["video_reference"])
    cue("video_sync")
    require(gate("CONFIRM_24V_ON_SUPPORTED", CLOSEOUT_PREPARE_GATE_S),
            "Operator did not confirm 24 V ON and independent support")
    report["power_on_operator_confirmed"] = True
    event("operator_cue", cue="WAIT_FOR_POWERED_IDLE_HEARTBEAT")
    power_start = now_ns()
    ready = None
    while now_ns() - power_start < CLOSEOUT_POWER_WAIT_NS:
        require(not abort_requested(), "Operator aborted powered IDLE wait")
        closeout_idle_guard(query, False)
        try:
            ready = closeout_idle_guard(query, True)
        except RuntimeError as exc:
            ready = None
            if str(exc) not in ("Powered IDLE heartbeat not yet received",
                                "Powered IDLE heartbeat not yet fresh"):
                raise
        sleep(0.12)
    require(ready is not None, "No fresh healthy powered IDLE heartbeat within 5 s")

    print("DIRECT 24V ON IDLE, still NO prepare: AFTER ONE HIGH BEEP, slowly lift forward at the marked point, read the force tool, and return fully to supported vertical. THEN press Y to finish; TWO HIGH BEEPS confirm the end. N/Esc aborts; maximum 120 seconds. Unusual drag: stop pushing, support, cut 24V, press N/Esc", flush=True)
    cue("on_move")
    # Discard a Y typed before/during the start cue; N/Esc remains an abort.
    require(completion_key(clear=True) is not False,
            "Operator aborted before direct powered IDLE comparison")
    on_start = now_ns()
    event("operator_cue", cue="ON_IDLE_MOVE_START", marker_ns=on_start,
          max_duration_s=CLOSEOUT_DIRECT_IDLE_MAX_NS / 1e9,
          completion_basis="operator Y after forward lift and return to vertical")
    on_samples = 0
    while True:
        if now_ns() - on_start >= CLOSEOUT_DIRECT_IDLE_MAX_NS:
            event("operator_cue", cue="ON_IDLE_COMPLETION_TIMEOUT",
                  elapsed_s=(now_ns() - on_start) / 1e9)
            raise RuntimeError("Direct powered IDLE completion timed out before Y")
        choice = completion_key()
        require(choice is not False, "Operator aborted direct powered IDLE comparison")
        closeout_idle_guard(query, True)
        on_samples += 1
        if now_ns() - on_start >= CLOSEOUT_DIRECT_IDLE_MAX_NS:
            event("operator_cue", cue="ON_IDLE_COMPLETION_TIMEOUT",
                  elapsed_s=(now_ns() - on_start) / 1e9)
            raise RuntimeError("Direct powered IDLE completion timed out before Y")
        if choice is True:
            on_end = now_ns()
            event("operator_cue", cue="ON_IDLE_MOVE_END", marker_ns=on_end,
                  elapsed_s=(on_end - on_start) / 1e9,
                  completion_basis="operator Y; physical return not sensed by script")
            cue("on_move_end")
            print("DIRECT 24V ON IDLE END: two high beeps; keep the arm supported vertically. Your Y recorded completion, not an automatic position or force measurement", flush=True)
            report["on_idle"] = {
                "samples": on_samples, "duration_s": (on_end - on_start) / 1e9,
                "completion_confirmed_by_operator": True,
                "physical_return_sensed_by_script": False,
                "force_measured_by_script": False,
            }
            break
        sleep(0.12)

    shoulder_line, shoulder, _ = query("shoulder")
    require(shoulder_line.startswith("OK ") and all(
        unsigned(shoulder, key) == 0 for key in
        ("drop", "invalid", "fov", "err", "bus_off")),
        "CAN error during direct powered IDLE comparison")
    report["shoulder_before"] = shoulder
    print("FINAL IDLE GATE: Y means powered IDLE felt acceptable and authorizes followprepare after one last read-only health check; N/Esc cancels without prepare", flush=True)
    require(gate("IDLE_ACCEPTABLE_SAME_POINT_RETURNED_ZERO", CLOSEOUT_PREPARE_GATE_S),
            "Operator declined direct powered IDLE comparison")
    report["idle_comparison_operator_accepted"] = True
    closeout_idle_guard(query, True)
    event("operator_cue", cue="CLOSEOUT_PREPARE_AUTHORIZED_BY_OPERATOR")
    return prepare()


def closeout_static_idle_before_prepare(query, report, prepare, before_prepare=None):
    """One powered static check after an independently observed cold SAFE_OFF."""
    report["off_idle"] = {"comparison_skipped": True,
                          "reason": "operator requested direct force A/B; cold SAFE_OFF recorded separately"}
    report["on_idle"] = {"movement_skipped": True,
                         "reason": "operator requested no repeated IDLE lift"}
    report["powered_static_idle"] = closeout_idle_guard(query, True)
    shoulder_line, shoulder, _ = query("shoulder")
    require(shoulder_line.startswith("OK ") and all(
        unsigned(shoulder, key) == 0 for key in
        ("drop", "invalid", "fov", "err", "bus_off")),
        "CAN error before direct force A/B")
    report["shoulder_before"] = shoulder
    if before_prepare is not None:
        before_prepare()
        closeout_idle_guard(query, True)
    return prepare()


def wait_for_operator_trigger(prep_at, status, key_ready, get_key, event,
                              now_ns, sleep):
    """Wait for the human's hold message, without a host posture gate."""
    deadline = prep_at + 18_500_000_000  # leave a reply margin inside firmware's 20 s TTL
    while now_ns() < deadline:
        f = status()
        require(unsigned(f, "phase") in (5, 6)
                and unsigned(f, "zero") == 1
                and unsigned(f, "fault") == 0
                and unsigned(f, "output_allowed") == 0,
                "Preparation ended before operator trigger")
        while key_ready():
            key = get_key().lower()
            if key in ("n", "\x1b"):
                raise RuntimeError("Operator cancelled before support")
            if key == "y":
                trigger = {
                    "at_ns": now_ns(),
                    "phase_at_trigger": unsigned(f, "phase"),
                    "joint_mrad_at_trigger": f.get("joint_mrad"),
                    "basis": "operator message relayed by host keyboard Y",
                }
                event("operator_trigger_received", **trigger)
                return f, trigger
        sleep(0.06)
    raise RuntimeError("No operator trigger before prepare TTL margin")


def closeout_active_tail(start_ns, ramp_in_ms, sample, abort_requested, cue, event,
                          now_ns, sleep, stop, report, measure_only=False,
                          operator_triggered=False):
    """Bounded comparison, optional small push, then stop."""
    stop_by = min(start_ns + CLOSEOUT_STOP_BY_NS,
                  start_ns + ramp_in_ms * 1_000_000 - 2_000_000_000)
    reposition_budget = 500_000_000 if measure_only else CLOSEOUT_PUSH_NS + CLOSEOUT_SETTLE_NS
    require(stop_by > start_ns + ASSIST_NS + reposition_budget,
            "No bounded closeout time before automatic ramp")
    assisted_at = now_ns()
    event("operator_cue", cue="ASSISTED_COMPARISON_START", marker_ns=assisted_at,
          note="Operator force/feeling is separate from cached torque target")
    rows = []
    nonzero_targets = 0
    max_abs_assisted_delta_deg = 0.0
    reserve_ns = 500_000_000 if measure_only else 2_000_000_000
    while now_ns() - assisted_at < ASSIST_NS:
        require(not abort_requested(), "Operator requested active abort")
        require(now_ns() < stop_by - reserve_ns,
                "Assisted comparison exhausted the stop margin" if measure_only else
                "Assisted comparison exhausted the reposition/stop margin")
        row, raw, ramp_ms = sample("assist")
        require(now_ns() < stop_by - reserve_ns,
                "Slow assisted sample exhausted stop margin" if measure_only else
                "Slow assisted sample exhausted reposition/stop margin")
        require(ramp_ms > 2000, "Automatic ramp too near during comparison")
        if row:
            if measure_only and not operator_triggered:
                max_abs_assisted_delta_deg = max(
                    max_abs_assisted_delta_deg,
                    abs(row["angle_deg"]
                        - report["ready_zero_target_baseline"]["median_deg"]))
            rows.append(row)
        if raw["reported_input_torque_target_nm"] > 0.0005:
            nonzero_targets += 1
        sleep(0.055)
    if not operator_triggered:
        require(len(rows) >= 8 and nonzero_targets >= 4,
                "Insufficient fresh posture or nonzero target samples")
    report["assisted_posture"] = ({**summarize_angles(rows),
                                   "duration_s": (now_ns() - assisted_at) / 1e9}
                                  if rows else {"duration_s": (now_ns() - assisted_at) / 1e9,
                                                "fresh_angle_samples": 0})
    report["assisted_target_nonzero_samples"] = nonzero_targets
    if not operator_triggered:
        report["baseline_assisted_median_delta_deg"] = abs(
            report["assisted_posture"]["median_deg"]
            - report["ready_zero_target_baseline"]["median_deg"])
        report["angle_delta_is_descriptive_only"] = True
    event("operator_cue", cue="ASSISTED_COMPARISON_END")
    if measure_only:
        report["nominal_same_posture_screen"] = (
            {"skipped": True, "reason": "operator_triggered_force_ab"}
            if operator_triggered else {
                "passed": max_abs_assisted_delta_deg
                          <= CLOSEOUT_SAME_POSTURE_MAX_DELTA_DEG,
                "max_delta_from_baseline_median_deg": CLOSEOUT_SAME_POSTURE_MAX_DELTA_DEG,
                "observed_max_abs_delta_deg": max_abs_assisted_delta_deg,
                "note": "A larger support-stage angle difference is recorded, not an abort; force readings at different postures cannot establish same-posture unloading. Video must also confirm arm and gauge position",
            })
        report["reposition"] = {"skipped": True, "reason": "user_scope"}
        event("operator_cue", cue="REPOSITION_SKIPPED_USER_SCOPE")
        require(now_ns() < stop_by, "Active stop deadline reached")
        report["closeout_stop_call_offset_s"] = (now_ns() - start_ns) / 1e9
        stop()
        return
    require(now_ns() + CLOSEOUT_PUSH_NS + CLOSEOUT_SETTLE_NS + 500_000_000 < stop_by,
            "No time for bounded push and stop")
    event("operator_cue", cue="GENTLE_REPOSITION_WITH_SUPPORT",
          duration_s=CLOSEOUT_PUSH_NS / 1e9)
    print("PUSH: gently reposition near 25 degrees, within 20.5-29.5; keep supporting the arm", flush=True)
    cue("push")
    move_at = now_ns()
    push_rows = []
    while now_ns() - move_at < CLOSEOUT_PUSH_NS:
        require(not abort_requested(), "Operator requested active abort")
        require(now_ns() < stop_by - 500_000_000, "Stop margin exhausted during push")
        row, _, ramp_ms = sample("reposition_push")
        require(now_ns() < stop_by - 500_000_000,
                "Slow push sample exhausted stop margin")
        require(ramp_ms > 2000, "Automatic ramp too near during push")
        if row:
            require(abs(row["angle_deg"] - report["assisted_posture"]["median_deg"])
                    <= CLOSEOUT_REPOSITION_SPAN_DEG,
                    "Reposition exceeded small neighboring posture span")
            push_rows.append(row)
        sleep(0.055)
    event("operator_cue", cue="STOP_REPOSITION_AND_HOLD_WITH_SUPPORT",
          duration_s=CLOSEOUT_SETTLE_NS / 1e9)
    print("HOLD: stop the push and keep supporting at the new posture", flush=True)
    cue("hold")
    hold_at = now_ns()
    hold_rows = []
    while now_ns() - hold_at < CLOSEOUT_SETTLE_NS:
        require(not abort_requested(), "Operator requested active abort")
        require(now_ns() < stop_by - 250_000_000, "Stop margin exhausted during hold")
        row, _, ramp_ms = sample("reposition_hold")
        require(now_ns() < stop_by - 250_000_000,
                "Slow hold sample exhausted stop margin")
        require(ramp_ms > 2000, "Automatic ramp too near during hold")
        if row:
            require(abs(row["angle_deg"] - report["assisted_posture"]["median_deg"])
                    <= CLOSEOUT_REPOSITION_SPAN_DEG,
                    "Reposition hold exceeded small neighboring posture span")
            hold_rows.append(row)
        sleep(0.055)
    require(push_rows and hold_rows, "No fresh feedback across reposition and hold")
    report["reposition"] = {
        "push": summarize_angles(push_rows), "hold": summarize_angles(hold_rows),
        "angle_change_deg": (statistics.median(row["angle_deg"] for row in hold_rows)
                             - report["assisted_posture"]["median_deg"]),
        "operator_blocking_or_support_return_observation": None,
        "note": "Qualitative P0.2 observation belongs to the operator/video, not angle/target telemetry",
    }
    require(now_ns() < stop_by, "Active stop deadline reached")
    report["closeout_stop_call_offset_s"] = (now_ns() - start_ns) / 1e9
    stop()


def cleanup_stop_once(prepared: bool, stop_attempted: bool, query):
    """At most one post-prepare stop request, including a timed-out reply."""
    if not prepared or stop_attempted:
        return stop_attempted, None
    stop_attempted = True
    try:
        return stop_attempted, query("followstop")[0]
    except BaseException as exc:
        return stop_attempted, exc


def interior(angle_deg: float) -> bool:
    return MIN_DEG < angle_deg < MAX_DEG


def summarize_angles(rows: list[dict]) -> dict:
    require(rows, "No fresh posture samples")
    angles = [row["angle_deg"] for row in rows]
    return {"samples": len(rows), "median_deg": statistics.median(angles),
            "min_deg": min(angles), "max_deg": max(angles),
            "first_seq": rows[0]["seq"], "last_seq": rows[-1]["seq"]}


def float32_bits(fields: dict, key: str) -> float:
    bits = fields.get(key, "")
    require(re.fullmatch(r"[0-9A-Fa-f]{8}", bits) is not None,
            "Malformed float32 bits: " + key)
    value = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    require(math.isfinite(value), "Nonfinite float32: " + key)
    return value


def closeout_raw_guard(row: dict, ready_zero: bool, target_cap_mnm: int) -> None:
    require(row["control_mode"] == 1 and row["input_mode"] == 1,
            "Torque/input mode readback is not 1/1")
    require(all(row[key] <= 150 for key in
                ("position_age_ms", "velocity_age_ms", "target_request_age_ms",
                 "mode_request_age_ms", "input_request_age_ms"))
            and abs(row["seq"] - row["target_seq"]) <= 1,
            "Cached READY/FOLLOW properties are stale or unpaired")
    torque = row["reported_input_torque_target_nm"]
    # Match s3_feedback.c zero_verified: the cached float32 target is exactly zero.
    require((torque == 0.0 if ready_zero else
              0.0 <= torque <= target_cap_mnm / 1000.0 + 0.000001),
            f"Drive-reported target outside READY zero or FOLLOW {target_cap_mnm} mNm bound")
    # s3properties fresh/out use the disabled S3 site and are not S4 gates.


def closeout_ready_angle(fields: dict, allow_lower_text_margin: bool = False) -> float:
    require(all(unsigned(fields, key) == value for key, value in {
        "phase": 6, "reader": 2, "fresh": 1, "zero": 1, "fault": 0,
        "output_allowed": 0, "cmd_torque": 0,
    }.items()) and fields.get("joint_mrad", "na") != "na",
            "Early READY lost fresh zero-target closed-loop status")
    deg = int(fields["joint_mrad"]) * DEG_PER_MRAD
    if allow_lower_text_margin:
        # The positive integer-mrad console value is truncated, not rounded.
        # A READY just above the firmware's 20 degree edge may print below 20.
        require(deg + DEG_PER_MRAD >= 20.0 and deg < MAX_DEG,
                "READY angle left the firmware window or host upper margin")
    else:
        require(interior(deg), "Early READY angle left the host interior")
    return deg


def closeout_measure_final_ready(fields: dict, hold_deg: float) -> float:
    """Recheck zero-target READY and the supported posture just before start."""
    deg = closeout_ready_angle(fields)
    require(abs(deg - hold_deg) <= CLOSEOUT_SAME_POSTURE_MAX_DELTA_DEG,
            "Final READY left the supported measurement posture")
    return deg


def closeout_wait_for_ready_at_cue(prep_at, status, heartbeat, raw_sample,
                                   abort_requested, event, beep, now_ns, sleep,
                                   report, target_cap_mnm=20):
    """Follow an early READY only while its zero target and closed loop stay proven."""
    hold_cue_sent = False
    early_ready = None
    while now_ns() - prep_at < PREPARE_DEADLINE_NS:
        require(now_ns() - prep_at < CLOSEOUT_BASELINE_LATEST_NS,
                "No time for 2 s READY baseline and followstart before prepare TTL")
        require(not abort_requested(), "Operator requested preparation abort")
        f = status()
        phase = unsigned(f, "phase")
        if early_ready is not None:
            require(phase == 6, "Early READY phase regressed")
            if not hold_cue_sent:
                require(now_ns() - early_ready["last_validated_at_ns"]
                        <= CLOSEOUT_READY_GUARD_MAX_NS,
                        "Early READY host telemetry cadence lost")
        require(phase in (5, 6) and unsigned(f, "zero") == 1
                and unsigned(f, "fault") == 0
                and unsigned(f, "output_allowed") == 0
                and unsigned(f, "cmd_torque") == 0,
                "Preparation left safe phase or faulted")
        if phase == 6:
            deg = closeout_ready_angle(f)
        elif unsigned(f, "fresh") == 1 and f.get("joint_mrad") != "na":
            deg = int(f["joint_mrad"]) * 180.0 / (math.pi * 1000.0)
        else:
            sleep(0.07)
            continue
        require(deg < MAX_DEG, "Manual move approached the 30 degree edge")
        if not hold_cue_sent:
            require(deg <= CLOSEOUT_CUE_MAX_DEG,
                    "Manual move passed the closeout hold cue range")
            if phase == 6 and (deg < CLOSEOUT_CUE_DEG or early_ready is not None):
                first_deg = deg
                first_seq = unsigned(f, "seq")
                observed_at = now_ns()
                hb_age = heartbeat(8)
                raw = raw_sample("ready_early_zero_target")
                closeout_raw_guard(raw, True, target_cap_mnm)
                f = status()  # Recheck after the composite read while the arm may move.
                deg = closeout_ready_angle(f)
                require(now_ns() - observed_at <= CLOSEOUT_READY_GUARD_MAX_NS
                        and (early_ready is None or now_ns() - early_ready["last_validated_at_ns"]
                             <= CLOSEOUT_READY_GUARD_MAX_NS),
                        "Early READY telemetry validation took too long")
                require(deg <= CLOSEOUT_CUE_MAX_DEG,
                        "Manual move passed the closeout hold cue during READY check")
                require(now_ns() - prep_at < CLOSEOUT_BASELINE_LATEST_NS,
                        "Early READY check exhausted the baseline/followstart margin")
                if early_ready is None:
                    early_ready = {"first_angle_deg": first_deg,
                                   "first_seq": first_seq,
                                   "first_at_ns": observed_at,
                                   "validated_samples": 0,
                                   "first_reported_target_nm": raw["reported_input_torque_target_nm"],
                                   "first_control_mode": raw["control_mode"],
                                   "first_input_mode": raw["input_mode"]}
                    report["early_ready"] = early_ready
                    event("operator_cue", cue="EARLY_READY_ZERO_VALIDATED_KEEP_LIFTING_SLOWLY",
                          angle_deg=first_deg, heartbeat_age_ms=hb_age,
                          note="Closed-loop state 8, modes 1/1, zero target; no assist permission")
                early_ready["validated_samples"] += 1
                early_ready["last_angle_deg"] = deg
                early_ready["last_heartbeat_age_ms"] = hb_age
                early_ready["last_validated_at_ns"] = now_ns()
                event("early_ready_zero_sample", angle_deg=deg,
                      seq=unsigned(f, "seq"), heartbeat_age_ms=hb_age)
            if deg >= CLOSEOUT_CUE_DEG:
                hold_cue_sent = True
                report["hold_cue"] = {"at_ns": now_ns(), "angle_deg": deg,
                                      "seq": unsigned(f, "seq")}
                event("operator_cue", cue="LONG_BEEP_STOP_AND_HOLD",
                      angle_deg=deg, seq=unsigned(f, "seq"))
                print("LONG_BEEP: stop moving; keep the same grip and support point",
                      flush=True)
                beep(1800, 420)
                continue
        if hold_cue_sent and phase == 6 and unsigned(f, "reader") == 2 \
                and interior(deg):
            require(CLOSEOUT_CUE_DEG <= deg <= CLOSEOUT_CUE_MAX_DEG,
                    "Arm left the closeout hold cue range before baseline")
            require(now_ns() - prep_at < CLOSEOUT_BASELINE_LATEST_NS,
                    "READY arrived too late for closeout baseline")
            return f
        sleep(0.07)
    raise RuntimeError("Fresh firmware READY inside the window absent before prepare TTL")


def closeout_wait_for_stable_interval(prep_at, status, heartbeat, raw_sample,
                                      abort_requested, event, beep, now_ns, sleep,
                                      report, settle_first=False, target_cap_mnm=20):
    """Validate zero-target READY at a supported interior posture once."""
    settle_at = None
    settle_min = None
    settle_max = None
    while now_ns() - prep_at < PREPARE_DEADLINE_NS:
        require(now_ns() - prep_at < CLOSEOUT_BASELINE_LATEST_NS,
                "No time for 2 s READY baseline and followstart before prepare TTL")
        require(not abort_requested(), "Operator requested preparation abort")
        f = status()
        phase = unsigned(f, "phase")
        require(phase in (5, 6) and unsigned(f, "zero") == 1
                and unsigned(f, "fault") == 0
                and unsigned(f, "output_allowed") == 0
                and unsigned(f, "cmd_torque") == 0,
                "Preparation left safe phase or faulted")
        if phase == 5:
            settle_at = None
            if unsigned(f, "fresh") == 1 and f.get("joint_mrad") != "na":
                require(int(f["joint_mrad"]) * DEG_PER_MRAD < MAX_DEG,
                        "Manual lift approached the upper host margin before READY")
            sleep(0.07)
            continue

        checked_at = now_ns()
        closeout_ready_angle(f, allow_lower_text_margin=True)
        hb_age = heartbeat(8)
        raw = raw_sample("ready_interval_validation")
        closeout_raw_guard(raw, True, target_cap_mnm)
        require(math.isfinite(raw["position_rev"])
                and math.isfinite(raw["velocity_rev_s"]),
                "READY posture feedback is not finite")
        f = status()  # Recheck after the composite read while the arm may move.
        deg = closeout_ready_angle(f, allow_lower_text_margin=True)
        observed_at = now_ns()
        require(observed_at - checked_at <= CLOSEOUT_READY_GUARD_MAX_NS,
                "READY interval telemetry validation took too long")
        if deg <= MIN_DEG:
            settle_at = None
            event("interval_ready_below_measurement_margin", angle_deg=deg,
                  seq=raw["seq"], heartbeat_age_ms=hb_age)
            sleep(0.07)
            continue
        if settle_first:
            if settle_at is None:
                settle_at = observed_at
                settle_min = settle_max = deg
            else:
                settle_min = min(settle_min, deg)
                settle_max = max(settle_max, deg)
            if settle_max - settle_min > CLOSEOUT_READY_SPAN_DEG:
                settle_at = observed_at
                settle_min = settle_max = deg
            if (observed_at - settle_at < 1_000_000_000
                    or abs(raw["velocity_rev_s"] * 45.0)
                    > CLOSEOUT_READY_PEAK_SPEED_DEG_S):
                sleep(0.07)
                continue
            report["settle_before_cue"] = {
                "duration_s": (observed_at - settle_at) / 1e9,
                "angle_span_deg": settle_max - settle_min,
                "peak_speed_limit_deg_s": CLOSEOUT_READY_PEAK_SPEED_DEG_S,
            }
        report["interval_ready_validation"] = {
            "angle_deg": deg,
            "raw_seq": raw["seq"],
            "abs_nominal_joint_velocity_deg_s": abs(raw["velocity_rev_s"] * 45.0),
            "host_measurement_entry_deg": [MIN_DEG, MAX_DEG],
            "note": "READY, closed-loop heartbeat, torque mode and zero target validated; hold/force judged during baseline and from video",
        }
        report["hold_cue"] = {"at_ns": observed_at,
                              "angle_deg": deg, "seq": unsigned(f, "seq")}
        event("operator_cue", cue="LONG_BEEP_VALID_READY_ALREADY_SETTLED",
              angle_deg=deg, seq=unsigned(f, "seq"),
              heartbeat_age_ms=hb_age)
        print("LONG_BEEP: valid zero-target READY; keep holding the same gauge point",
              flush=True)
        beep(1800, 420)
        return f
    raise RuntimeError("Interior READY absent before prepare TTL")


def closeout_baseline_standstill(rows: list[dict], measure_only: bool = False) -> dict:
    """Screen obvious movement over 2 s; force stability is judged from video."""
    require(rows, "No cached rotor feedback in READY baseline")
    positions = [row["position_rev"] for row in rows]
    # The 8:1 nominal rotor-to-joint conversion gives 45 joint degrees/rev.
    # Run3's 1 s READY baseline spanned 0.057 deg with 1.72 deg/s peak.
    # The firmware's 0.001 rev / 0.02 rev/s thresholds apply to a 200 ms
    # READY entry dwell, so they must not be extrapolated over this whole 2 s.
    span_deg = (max(positions) - min(positions)) * 45.0
    peak_speed_deg_s = max(abs(row["velocity_rev_s"]) for row in rows) * 45.0
    if measure_only:
        require(span_deg <= CLOSEOUT_SAME_POSTURE_MAX_DELTA_DEG,
                "READY baseline moved before followstart")
    else:
        require(span_deg <= CLOSEOUT_READY_SPAN_DEG
                and peak_speed_deg_s <= CLOSEOUT_READY_PEAK_SPEED_DEG_S,
                "READY baseline moved before followstart")
    result = {"nominal_joint_angle_span_deg": span_deg,
              "peak_abs_nominal_joint_velocity_deg_s": peak_speed_deg_s,
              "basis": "host 2 s posture screen; run3 noise and same-posture force comparison; cached feedback, not a force or video measurement"}
    if measure_only:
        result["host_span_limit_deg"] = CLOSEOUT_SAME_POSTURE_MAX_DELTA_DEG
        result["host_peak_speed_limit_deg_s"] = None
    else:
        result["limits_deg_and_deg_s"] = [CLOSEOUT_READY_SPAN_DEG,
                                           CLOSEOUT_READY_PEAK_SPEED_DEG_S]
    return result


def offline_check() -> None:
    require(float32_bits({"vel": "3D800000"}, "vel") == 0.0625
            and float32_bits({"tq": "3C23D70A"}, "tq") > 0.009,
            "Raw telemetry float32 decoding failed")
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
    closeout_offline_check()
    print("offline single-posture/closeout/direct-IDLE flow check PASS; no serial or motor access", flush=True)


def closeout_offline_check() -> None:
    require(select_target_cap_mnm(20, None) == 20
            and select_target_cap_mnm(40, GAIN36_IMAGE_SHA256) == 40
            and select_target_cap_mnm(40, GAIN36_SPEED25_IMAGE_SHA256) == 40
            and select_target_cap_mnm(40, GAIN36_SPEED30_IMAGE_SHA256) == 40
            and select_target_cap_mnm(40, GAIN36_SPEED40_IMAGE_SHA256) == 40,
            "Explicit target cap selection failed")
    for invalid_cap, invalid_hash in ((40, None), (40, "0" * 64), (60, GAIN36_IMAGE_SHA256)):
        try:
            select_target_cap_mnm(invalid_cap, invalid_hash)
        except RuntimeError:
            pass
        else:
            raise RuntimeError("Unauthorized target cap passed image gate")
    guarded_raw = {
        "control_mode": 1, "input_mode": 1,
        "position_age_ms": 20, "velocity_age_ms": 20,
        "target_request_age_ms": 20, "mode_request_age_ms": 20,
        "input_request_age_ms": 20, "seq": 5, "target_seq": 5,
        "reported_input_torque_target_nm": 0.0,
    }
    for cap, valid_target, invalid_target in ((20, 0.020000, 0.02001),
                                              (40, 0.040000, 0.04001)):
        closeout_raw_guard(guarded_raw, True, cap)
        closeout_raw_guard({**guarded_raw,
                            "reported_input_torque_target_nm": valid_target}, False, cap)
        for ready_zero, target in ((True, valid_target), (False, invalid_target)):
            try:
                closeout_raw_guard({**guarded_raw,
                                    "reported_input_torque_target_nm": target},
                                   ready_zero, cap)
            except RuntimeError:
                pass
            else:
                raise RuntimeError("READY zero or FOLLOW target bound was not enforced")
    # Reproduce closeout run3: uptime=49233 was read before hb_ms advanced to
    # 49237 on the next reply. A later status at 49255 makes the age 18 ms.
    race_commands = []
    def raced_idle_query(command):
        race_commands.append(command)
        if command == "followstatus":
            return "OK FOLLOW", {key: str(value) for key, value in {
                "phase": 1, "reader": 1, "seq": 0, "zero": 0, "fault": 0,
                "output_allowed": 0, "cmd_torque": 0,
            }.items()}, 0
        if command == "status":
            uptime = 49233 if race_commands.count("status") == 1 else 49255
            return "OK status", {"uptime_ms": str(uptime), "output_allowed": "0"}, 0
        if command == "shoulderfeedback":
            return "OK shoulderfeedback", {"hb": "1", "state": "1", "fault": "0",
                                           "error": "00000000", "hb_ms": "49237"}, 0
        raise AssertionError("Unexpected race repro command: " + command)
    race_result = closeout_idle_guard(raced_idle_query, True)
    require(race_result["heartbeat_age_ms"] == 18
            and race_commands == ["followstatus", "status",
                                  "shoulderfeedback", "status"],
            "Run3 asynchronous heartbeat repro was not measured after the heartbeat")

    def truly_stale_query(command):
        if command == "status":
            return "OK status", {"uptime_ms": "49255", "output_allowed": "0"}, 0
        if command == "shoulderfeedback":
            return "OK shoulderfeedback", {"hb": "1", "state": "1", "fault": "0",
                                           "error": "00000000", "hb_ms": "49000"}, 0
        return raced_idle_query(command)
    try:
        closeout_idle_guard(truly_stale_query, True)
    except RuntimeError as exc:
        require("heartbeat not yet fresh" in str(exc),
                "Truly stale powered IDLE heartbeat had the wrong failure")
    else:
        raise RuntimeError("255 ms old heartbeat passed the unchanged 150 ms gate")

    status_reads = 0
    def output_flip_query(command):
        nonlocal status_reads
        if command == "status":
            status_reads += 1
            return "OK status", {"uptime_ms": "49255",
                                 "output_allowed": "0" if status_reads == 1 else "1"}, 0
        if command == "shoulderfeedback":
            return "OK shoulderfeedback", {"hb": "1", "state": "1", "fault": "0",
                                           "error": "00000000", "hb_ms": "49237"}, 0
        return raced_idle_query(command)
    try:
        closeout_idle_guard(output_flip_query, True)
    except RuntimeError as exc:
        require("output permission changed after heartbeat" in str(exc),
                "Post-heartbeat output permission failure was not detected")
    else:
        raise RuntimeError("Post-heartbeat output permission change passed IDLE guard")

    class FakeClock:
        def __init__(self):
            self.ns = 0

        def now(self):
            return self.ns

        def sleep(self, seconds):
            self.ns += int(seconds * 1e9)

    class FakeVcp:
        def __init__(self, clock, bad_heartbeat=False, bad_after_on_s=None):
            self.clock = clock
            self.powered = False
            self.bad_heartbeat = bad_heartbeat
            self.bad_after_on_ns = (None if bad_after_on_s is None
                                    else int(bad_after_on_s * 1e9))
            self.power_on_ns = None
            self.commands = []
            self.power_on_command_index = None

        def query(self, command):
            self.commands.append(command)
            now_ms = self.clock.now() // 1_000_000
            if command == "followstatus":
                fields = {key: str(value) for key, value in {
                    "phase": 1, "reader": 1, "seq": 0, "zero": 0, "fault": 0,
                    "output_allowed": 0, "cmd_torque": 0,
                }.items()}
                return "OK FOLLOW", fields, self.clock.now()
            if command == "status":
                return "OK status", {"uptime_ms": str(now_ms), "output_allowed": "0"}, self.clock.now()
            if command == "shoulderfeedback":
                late_bad = (self.bad_after_on_ns is not None
                            and self.power_on_ns is not None
                            and self.clock.now() - self.power_on_ns >= self.bad_after_on_ns)
                fields = {"hb": "1" if self.powered else "0",
                          "state": "2" if self.bad_heartbeat or late_bad else "1",
                          "fault": "0", "error": "00000000",
                          "hb_ms": str(max(0, now_ms - 10))}
                return "OK shoulderfeedback", fields, self.clock.now()
            if command == "shoulder":
                return "OK shoulder", {key: "0" for key in
                                       ("drop", "invalid", "fov", "err", "bus_off")}, self.clock.now()
            if command == "followprepare":
                return "OK FOLLOW preparing output_allowed=0", {}, self.clock.now()
            if command == "followstart":
                return "OK FOLLOW started; expires_in_ms=10000; ramp_in_ms=9000; support arm before ramp", {}, self.clock.now()
            if command == "followstop":
                return "OK FOLLOW ramping_down; support arm", {}, self.clock.now()
            raise AssertionError("Unexpected fake command: " + command)

    def scenario(bad_heartbeat=False, decline_idle=False, bad_after_on_s=None):
        clock = FakeClock()
        vcp = FakeVcp(clock, bad_heartbeat, bad_after_on_s)
        report = {"metrology": {"video_reference": None},
                  "ready_zero_target_baseline": {"median_deg": 25.0},
                  "operator_observation": {"assistance_clearly_reduced_effort": None}}
        events = []
        def gate(label, timeout_s):
            del timeout_s
            if label == "CONFIRM_24V_ON_SUPPORTED":
                vcp.power_on_command_index = len(vcp.commands)
                vcp.powered = True
                vcp.power_on_ns = clock.now()
            return not (decline_idle and label == "IDLE_ACCEPTABLE_SAME_POINT_RETURNED_ZERO")
        def event(kind, **values):
            events.append((kind, values))
        def prepare():
            return vcp.query("followprepare")
        return clock, vcp, report, events, gate, event, prepare

    def completion_after(clock, delay_s, answer=True):
        armed_ns = None
        def poll(clear=False):
            nonlocal armed_ns
            if clear:
                armed_ns = clock.now()
                return None
            require(armed_ns is not None, "Fake direct IDLE completion was not armed")
            if delay_s is not None and clock.now() - armed_ns >= int(delay_s * 1e9):
                return answer
            return None
        return poll

    # The explicit direct branch skips the OFF lift and awaits the operator's
    # completed powered-IDLE Y while repeatedly checking healthy SAFE_OFF.
    clock, vcp, report, events, gate, event, prepare = scenario()
    direct_cues = []
    prep_reply, _, _ = closeout_direct_idle_before_prepare(
        vcp.query, gate, completion_after(clock, 2.4), lambda: False,
        direct_cues.append, event, clock.now, clock.sleep, report, prepare)
    direct_markers = {details["cue"]: details["marker_ns"]
                      for kind, details in events if kind == "operator_cue"
                      and details.get("cue") in ("ON_IDLE_MOVE_START", "ON_IDLE_MOVE_END")}
    require(prep_reply.startswith("OK FOLLOW preparing")
            and report["off_idle"]["comparison_skipped"] is True
            and report["on_idle"]["completion_confirmed_by_operator"] is True
            and 2.4 <= report["on_idle"]["duration_s"] < 8.0
            and len(direct_markers) == 2
            and direct_cues == ["video_sync", "on_move", "on_move_end"]
            and vcp.commands.count("followprepare") == 1
            and not any(c in vcp.commands for c in ("followstart", "followstop", "followobserve"))
            and "shoulderfeedback" not in vcp.commands[:vcp.power_on_command_index],
            "Direct IDLE fake success skipped OFF or completed/guarded incorrectly")

    for label, delay_s, answer, bad_after_on_s, expected in (
        ("operator N", 0.6, False, None, "Operator aborted direct powered IDLE"),
        ("timeout", None, True, None, "completion timed out before Y"),
        ("powered IDLE fault", 2.4, True, 6.0, "heartbeat unhealthy"),
    ):
        clock, vcp, report, events, gate, event, prepare = scenario(
            bad_after_on_s=bad_after_on_s)
        try:
            closeout_direct_idle_before_prepare(
                vcp.query, gate, completion_after(clock, delay_s, answer),
                lambda: False, lambda _: None, event, clock.now, clock.sleep,
                report, prepare)
        except RuntimeError as exc:
            require(expected in str(exc), "Wrong direct IDLE failure for " + label)
        else:
            raise RuntimeError("Direct IDLE " + label + " unexpectedly reached prepare")
        require(not any(c in vcp.commands for c in
                        ("followprepare", "followstart", "followstop", "followobserve"))
                and "on_idle" not in report,
                "Direct IDLE " + label + " sent a motion command or recorded completion")

    clock, vcp, report, events, gate, event, prepare = scenario(bad_heartbeat=True)
    try:
        closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                                clock.now, clock.sleep, report, prepare)
    except RuntimeError as exc:
        require("heartbeat unhealthy" in str(exc), "Wrong abnormal-IDLE failure")
    else:
        raise RuntimeError("Abnormal powered IDLE unexpectedly reached prepare")
    attempted, _ = cleanup_stop_once(False, False, vcp.query)
    require(not attempted and not any(c in vcp.commands for c in
                                     ("followprepare", "followstart", "followstop"))
            and "shoulderfeedback" not in vcp.commands[:vcp.power_on_command_index],
            "OFF/abnormal-IDLE path sent a motion command or required an OFF heartbeat")

    clock, vcp, report, events, gate, event, prepare = scenario(decline_idle=True)
    try:
        closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                                clock.now, clock.sleep, report, prepare)
    except RuntimeError as exc:
        require("Operator declined" in str(exc), "Wrong operator-rejection failure")
    else:
        raise RuntimeError("Operator rejection unexpectedly reached prepare")
    require(not any(c in vcp.commands for c in ("followprepare", "followstart", "followstop")),
            "Operator rejection sent a motion command")

    clock, vcp, report, events, gate, event, prepare = scenario()
    idle_cues = []
    prep_reply, _, _ = closeout_before_prepare(vcp.query, gate, lambda: False, idle_cues.append, event,
                                                 clock.now, clock.sleep, report, prepare)
    require(prep_reply.startswith("OK FOLLOW preparing")
            and report["shoulder_before"]["err"] == "0"
            and idle_cues == ["video_sync", "off_move", "off_move_end",
                              "on_move", "on_move_end"],
            "Fake prepare or shoulder snapshot not accepted")
    idle_markers = {details["cue"]: details["marker_ns"]
                    for kind, details in events if kind == "operator_cue"
                    and details.get("cue") in ("OFF_IDLE_MOVE_START", "OFF_IDLE_MOVE_END",
                                               "ON_IDLE_MOVE_START", "ON_IDLE_MOVE_END")}
    require(len(idle_markers) == 4
            and idle_markers["OFF_IDLE_MOVE_END"] - idle_markers["OFF_IDLE_MOVE_START"]
            >= CLOSEOUT_IDLE_WINDOW_NS
            and idle_markers["ON_IDLE_MOVE_END"] - idle_markers["ON_IDLE_MOVE_START"]
            >= CLOSEOUT_IDLE_WINDOW_NS,
            "OFF/ON IDLE start/end markers were not logged around both 8 s windows")
    vcp.query("followstart")
    start_ns = clock.now()
    seq = 0
    def sample(stage):
        nonlocal seq
        seq += 1
        row = {"seq": seq, "angle_deg": 25.0 + seq * 0.001, "stage": stage}
        raw = {"reported_input_torque_target_nm": 0.014}
        ramp_ms = 9000 - (clock.now() - start_ns) // 1_000_000
        return row, raw, ramp_ms
    closeout_active_tail(start_ns, 9000, sample, lambda: False, lambda _: None,
                         event, clock.now, clock.sleep,
                         lambda: vcp.query("followstop"), report)
    require([vcp.commands.count(c) for c in ("followprepare", "followstart", "followstop")]
            == [1, 1, 1] and report["closeout_stop_call_offset_s"] < 7.0
            and "hold" in report["reposition"],
            "Normal fake closeout did not stop once before start+7 s")

    # Explicit measurement-only scope: assisted comparison goes directly to stop.
    clock, vcp, report, events, gate, event, prepare = scenario()
    closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                            clock.now, clock.sleep, report, prepare)
    vcp.query("followstart")
    start_ns = clock.now()
    seq = 0
    stages = []
    move_cues = []
    def measure_sample(stage):
        nonlocal seq
        seq += 1
        stages.append(stage)
        return ({"seq": seq, "angle_deg": 25.0 + seq * 0.001},
                {"reported_input_torque_target_nm": 0.014},
                9000 - (clock.now() - start_ns) // 1_000_000)
    closeout_active_tail(start_ns, 9000, measure_sample, lambda: False,
                         move_cues.append, event, clock.now, clock.sleep,
                         lambda: vcp.query("followstop"), report, True)
    require([vcp.commands.count(c) for c in ("followprepare", "followstart", "followstop")]
            == [1, 1, 1] and report["closeout_stop_call_offset_s"] < 7.0
            and report["reposition"] == {"skipped": True, "reason": "user_scope"}
            and report["nominal_same_posture_screen"]["passed"] is True
            and stages and set(stages) == {"assist"} and not move_cues
            and not any(v.get("cue") in ("GENTLE_REPOSITION_WITH_SUPPORT",
                                         "STOP_REPOSITION_AND_HOLD_WITH_SUPPORT")
                        for _, v in events),
            "Measurement-only fake path repositioned or missed its single stop")

    clock, vcp, report, events, gate, event, prepare = scenario()
    closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                            clock.now, clock.sleep, report, prepare)
    vcp.query("followstart")
    start_ns = clock.now()
    def moved_measurement(stage):
        require(stage == "assist", "Unexpected moved-measurement stage")
        return ({"seq": 1, "angle_deg": 26.0},
                {"reported_input_torque_target_nm": 0.014}, 9000)
    closeout_active_tail(start_ns, 9000, moved_measurement, lambda: False,
                         lambda _: None, event, clock.now, clock.sleep,
                         lambda: vcp.query("followstop"), report, True)
    require(vcp.commands.count("followstop") == 1
            and report["nominal_same_posture_screen"]["passed"] is False
            and report["nominal_same_posture_screen"]["observed_max_abs_delta_deg"] == 1.0,
            "Moved assisted posture was not marked incomparable or stopped normally")

    clock, vcp, report, events, gate, event, prepare = scenario()
    closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                            clock.now, clock.sleep, report, prepare)
    vcp.query("followstart")
    start_ns = clock.now()
    def measure_fault(stage):
        require(stage == "assist", "Measurement-only fault reached reposition")
        raise RuntimeError("simulated measure-only feedback fault")
    try:
        closeout_active_tail(start_ns, 9000, measure_fault, lambda: False,
                             lambda _: None, event, clock.now, clock.sleep,
                             lambda: vcp.query("followstop"), report, True)
    except RuntimeError as exc:
        require("feedback fault" in str(exc), "Wrong measurement-only feedback failure")
    else:
        raise RuntimeError("Measurement-only feedback fault unexpectedly passed")
    attempted, _ = cleanup_stop_once(True, False, vcp.query)
    attempted, _ = cleanup_stop_once(True, attempted, vcp.query)
    require(vcp.commands.count("followstop") == 1,
            "Measurement-only feedback failure did not stop exactly once")

    clock, vcp, report, events, gate, event, prepare = scenario()
    closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                            clock.now, clock.sleep, report, prepare)
    vcp.query("followstart")
    start_ns = clock.now()
    seq = 0
    stop_state = {"attempted": False}
    def failed_stop():
        stop_state["attempted"] = True  # mirrors stop_and_observe before its query
        vcp.query("followstop")
        raise RuntimeError("simulated measure-only stop reply failure")
    try:
        closeout_active_tail(start_ns, 9000, measure_sample, lambda: False,
                             lambda _: None, event, clock.now, clock.sleep,
                             failed_stop, report, True)
    except RuntimeError as exc:
        require("stop reply failure" in str(exc), "Wrong measurement-only stop failure")
    else:
        raise RuntimeError("Measurement-only stop failure unexpectedly passed")
    attempted, _ = cleanup_stop_once(True, stop_state["attempted"], vcp.query)
    require(attempted and vcp.commands.count("followstop") == 1,
            "Measurement-only stop failure retried followstop")

    # A composite telemetry read may finish after the loop's entry check.
    # It must not proceed with more push/hold work; the caller sends one stop.
    clock, vcp, report, events, gate, event, prepare = scenario()
    closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                            clock.now, clock.sleep, report, prepare)
    vcp.query("followstart")
    start_ns = clock.now()
    seq = 0
    def slow_sample(stage):
        nonlocal seq
        seq += 1
        if stage == "reposition_hold":
            clock.sleep(2.4)
        return ({"seq": seq, "angle_deg": 25.0 + seq * 0.001},
                {"reported_input_torque_target_nm": 0.014},
                9000 - (clock.now() - start_ns) // 1_000_000)
    try:
        closeout_active_tail(start_ns, 9000, slow_sample, lambda: False,
                             lambda _: None, event, clock.now, clock.sleep,
                             lambda: vcp.query("followstop"), report)
    except RuntimeError as exc:
        require("Slow hold sample" in str(exc), "Wrong slow-sample stop decision")
    else:
        raise RuntimeError("Slow composite sample unexpectedly continued")
    attempted, _ = cleanup_stop_once(True, False, vcp.query)
    require(attempted and vcp.commands.count("followstop") == 1
            and clock.now() - start_ns < 7_000_000_000,
            "Slow-sample path did not attempt one reserved stop")

    clock = FakeClock()
    closeout_query_budget(clock.now(), clock.now() + CLOSEOUT_QUERY_CUTOFF_NS,
                          False, "s3feedback")
    clock.sleep(CLOSEOUT_QUERY_CUTOFF_NS / 1e9)
    try:
        closeout_query_budget(clock.now(), CLOSEOUT_QUERY_CUTOFF_NS,
                              False, "s3properties")
    except RuntimeError as exc:
        require("query reserve" in str(exc), "Wrong serial query reserve failure")
    else:
        raise RuntimeError("Telemetry query began after stop reserve")
    closeout_query_budget(clock.now(), CLOSEOUT_QUERY_CUTOFF_NS,
                          False, "followstop")

    clock, vcp, report, events, gate, event, prepare = scenario()
    closeout_before_prepare(vcp.query, gate, lambda: False, lambda _: None, event,
                            clock.now, clock.sleep, report, prepare)
    vcp.query("followstart")
    start_ns = clock.now()
    def fault_sample(stage):
        del stage
        raise RuntimeError("simulated feedback fault")
    try:
        closeout_active_tail(start_ns, 9000, fault_sample, lambda: False,
                             lambda _: None, event, clock.now, clock.sleep,
                             lambda: vcp.query("followstop"), report)
    except RuntimeError as exc:
        require("simulated feedback fault" in str(exc), "Wrong active failure")
    else:
        raise RuntimeError("Active fake fault unexpectedly passed")
    attempted, reply = cleanup_stop_once(True, False, vcp.query)
    attempted, _ = cleanup_stop_once(True, attempted, vcp.query)
    require(attempted and isinstance(reply, str)
            and vcp.commands.count("followstop") == 1,
            "Post-prepare failure did not attempt exactly one stop")

    # Run5 first observed READY at 367 mrad (~21.03 deg), before the 24.5 deg
    # hold cue. Exercise the same closeout branch with a fake clock/link.
    def ready_scenario(raw_override=None, bad_heartbeat=False, bad_fresh=False,
                        start_ns=0, slow_raw=False, slow_beep=False,
                        post_cue_mrad=428):
        clock = FakeClock()
        clock.ns = start_ns
        commands = ["followprepare"]
        events = []
        beeps = []
        angles_mrad = iter((367, 367, 385, 385, 428, 428, post_cue_mrad))
        last_angle = post_cue_mrad
        def status():
            nonlocal last_angle
            commands.append("followstatus")
            clock.sleep(0.025)
            last_angle = next(angles_mrad, last_angle)
            fields = {key: str(value) for key, value in {
                "phase": 6, "reader": 2, "fresh": 0 if bad_fresh else 1,
                "zero": 1, "fault": 0, "output_allowed": 0,
                "cmd_torque": 0, "joint_mrad": last_angle, "seq": len(commands),
            }.items()}
            return fields
        def heartbeat(state):
            commands.extend(("shoulderfeedback", "status"))
            clock.sleep(0.025)
            require(state == 8 and not bad_heartbeat,
                    "Required fresh closed-loop heartbeat absent")
            return 80
        def raw_sample(stage):
            require(stage == "ready_early_zero_target", "Wrong early READY raw stage")
            commands.extend(("s3feedback", "s3properties"))
            clock.sleep(0.6 if slow_raw else 0.025)
            row = {"control_mode": 1, "input_mode": 1,
                   "position_age_ms": 20, "velocity_age_ms": 20,
                   "target_request_age_ms": 20, "mode_request_age_ms": 20,
                   "input_request_age_ms": 20, "seq": 10, "target_seq": 11,
                   "reported_input_torque_target_nm": 0.0}
            row.update(raw_override or {})
            return row
        def event(kind, **values):
            events.append((kind, values))
        def beep(frequency, duration):
            beeps.append((frequency, duration))
            clock.sleep(0.6 if slow_beep else duration / 1000.0)
        def query(command):
            commands.append(command)
            return "OK FOLLOW idle_submitted; observing_stop; support arm", {}, clock.now()
        return clock, commands, events, beeps, status, heartbeat, raw_sample, event, beep, query

    clock, commands, events, beeps, status, hb, raw, event, beep, query = ready_scenario()
    ready_report = {}
    early_ready = closeout_wait_for_ready_at_cue(
        0, status, hb, raw, lambda: False, event, beep,
        clock.now, clock.sleep, ready_report)
    require(unsigned(early_ready, "phase") == 6
            and unsigned(early_ready, "joint_mrad") == 428
            and ready_report["early_ready"]["validated_samples"] >= 2
            and 21.0 < ready_report["early_ready"]["first_angle_deg"] < 21.1
            and 24.5 <= ready_report["hold_cue"]["angle_deg"] <= 25.5
            and beeps == [(1800, 420)]
            and commands.count("followprepare") == 1
            and not any(c in commands for c in ("followstart", "followstop"))
            and any(v.get("cue") == "EARLY_READY_ZERO_VALIDATED_KEEP_LIFTING_SLOWLY"
                    for _, v in events),
            "Run5 early READY did not safely reach the closeout hold cue")
    clock, commands, events, beeps, status, hb, raw, event, beep, query = ready_scenario(
        slow_beep=True)
    require(unsigned(closeout_wait_for_ready_at_cue(
        0, status, hb, raw, lambda: False, event, beep,
        clock.now, clock.sleep, {}), "phase") == 6,
        "Long hold cue falsely failed the moving READY telemetry cadence")
    clock, commands, events, beeps, status, hb, raw, event, beep, query = ready_scenario(
        post_cue_mrad=455)
    try:
        closeout_wait_for_ready_at_cue(
            0, status, hb, raw, lambda: False, event, beep,
            clock.now, clock.sleep, {})
    except RuntimeError as exc:
        require("left the closeout hold cue range" in str(exc),
                "Post-cue drift had the wrong failure")
    else:
        raise RuntimeError("Post-cue drift reached the 25 degree baseline")
    stable = closeout_baseline_standstill([
        {"position_rev": 3.000000, "velocity_rev_s": 0.038147},
        {"position_rev": 3.001262, "velocity_rev_s": 0.001},
    ])
    require(0.056 < stable["nominal_joint_angle_span_deg"] < 0.058
            and 1.71 < stable["peak_abs_nominal_joint_velocity_deg_s"] < 1.73,
            "Previously observed run3 READY baseline should pass the 2 s screen")
    try:
        closeout_baseline_standstill([
            {"position_rev": 3.0000, "velocity_rev_s": 0.001},
            {"position_rev": 3.0060, "velocity_rev_s": 0.001},
        ])
    except RuntimeError as exc:
        require("baseline moved" in str(exc), "Moving baseline had wrong failure")
    else:
        raise RuntimeError("Moving baseline reached followstart")
    try:
        closeout_baseline_standstill([
            {"position_rev": 3.0000, "velocity_rev_s": 0.001},
            {"position_rev": 3.0001, "velocity_rev_s": 0.060},
        ])
    except RuntimeError as exc:
        require("baseline moved" in str(exc), "Fast baseline had wrong failure")
    else:
        raise RuntimeError("Fast baseline reached followstart")
    measured = closeout_baseline_standstill([
        {"position_rev": 3.0000, "velocity_rev_s": 0.001},
        {"position_rev": 3.0060, "velocity_rev_s": 0.060},
    ], measure_only=True)
    require(measured["host_span_limit_deg"] == 0.5
            and measured["host_peak_speed_limit_deg_s"] is None,
            "Measurement-only baseline retained an extra host speed gate")
    try:
        closeout_baseline_standstill([
            {"position_rev": 3.0000, "velocity_rev_s": 0.001},
            {"position_rev": 3.0120, "velocity_rev_s": 0.001},
        ], measure_only=True)
    except RuntimeError as exc:
        require("baseline moved" in str(exc), "Measurement baseline had wrong failure")
    else:
        raise RuntimeError("Measurement baseline drift reached followstart")

    for label, override, bad_hb, bad_fresh, start_ns, slow_raw, expected in (
        ("mode", {"control_mode": 0}, False, False, 0, False, "mode readback"),
        ("target", {"reported_input_torque_target_nm": 0.00005}, False, False, 0, False,
         "target outside READY zero"),
        ("stale", {"mode_request_age_ms": 151}, False, False, 0, False,
         "properties are stale"),
        ("heartbeat", None, True, False, 0, False, "closed-loop heartbeat absent"),
        ("feedback", None, False, True, 0, False, "lost fresh zero-target"),
        ("slow_query", None, False, False, 0, True, "validation took too long"),
        ("TTL", None, False, False, CLOSEOUT_BASELINE_LATEST_NS - 40_000_000, False,
         "baseline/followstart margin"),
    ):
        clock, commands, events, beeps, status, hb, raw, event, beep, query = ready_scenario(
            override, bad_hb, bad_fresh, start_ns, slow_raw)
        try:
            closeout_wait_for_ready_at_cue(
                0, status, hb, raw, lambda: False, event, beep,
                clock.now, clock.sleep, {})
        except RuntimeError as exc:
            require(expected in str(exc), "Wrong early READY failure: " + label)
        else:
            raise RuntimeError("Invalid early READY reached the hold cue: " + label)
        attempted, _ = cleanup_stop_once(True, False, query)
        attempted, _ = cleanup_stop_once(True, attempted, query)
        require(commands.count("followprepare") == 1
                and commands.count("followstart") == 0
                and commands.count("followstop") == 1
                and not any(v.get("cue") == "LONG_BEEP_STOP_AND_HOLD" for _, v in events),
                "Early READY failure did not stop once before start: " + label)

    # Measurement-only: a valid READY within the host margin can cue the hold
    # immediately. The two-second baseline and final READY still gate start.
    def interval_scenario(angles_mrad, mode=1, heartbeat_ok=True,
                          speed_deg_s=0.1, moving_position=False):
        clock = FakeClock()
        angles = iter(angles_mrad)
        angle_mrad = angles_mrad[-1]
        commands = ["followprepare"]
        events = []
        beeps = []
        raw_seq = 0
        def status():
            nonlocal angle_mrad
            commands.append("followstatus")
            clock.sleep(0.025)
            angle_mrad = next(angles, angle_mrad)
            return {key: str(value) for key, value in {
                "phase": 6, "reader": 2, "fresh": 1, "zero": 1,
                "fault": 0, "output_allowed": 0, "cmd_torque": 0,
                "joint_mrad": angle_mrad, "seq": len(commands),
            }.items()}
        def heartbeat(state):
            commands.extend(("shoulderfeedback", "status"))
            clock.sleep(0.025)
            require(state == 8 and heartbeat_ok,
                    "Required fresh closed-loop heartbeat absent")
            return 30
        def raw_sample(stage):
            nonlocal raw_seq
            require(stage == "ready_interval_validation",
                    "Wrong interval READY raw stage")
            commands.extend(("s3feedback", "s3properties"))
            clock.sleep(0.025)
            raw_seq += 1
            return {
                "control_mode": mode, "input_mode": 1,
                "position_age_ms": 20, "velocity_age_ms": 20,
                "target_request_age_ms": 20, "mode_request_age_ms": 20,
                "input_request_age_ms": 20, "seq": raw_seq,
                "target_seq": raw_seq + 1,
                "reported_input_torque_target_nm": 0.0,
                "position_rev": angle_mrad * DEG_PER_MRAD / 45.0
                + (raw_seq * 0.02 if moving_position else 0.0),
                "velocity_rev_s": speed_deg_s / 45.0,
            }
        def event(kind, **values):
            events.append((kind, values))
        def beep(frequency, duration):
            beeps.append((frequency, duration))
            clock.sleep(duration / 1000.0)
        def query(command):
            commands.append(command)
            return "OK FOLLOW idle_submitted; observing_stop; support arm", {}, clock.now()
        return clock, commands, events, beeps, status, heartbeat, raw_sample, event, beep, query

    for angle_mrad in (361, 384, 505):  # about 20.7, 22.0, and 28.9 degrees
        scenario_values = [357] * 4 + [angle_mrad] * 40
        clock, commands, events, beeps, status, hb, raw, event, beep, query = interval_scenario(
            scenario_values)
        interval_report = {}
        ready = closeout_wait_for_stable_interval(
            0, status, hb, raw, lambda: False, event, beep,
            clock.now, clock.sleep, interval_report)
        require(unsigned(ready, "phase") == 6
                and interior(interval_report["hold_cue"]["angle_deg"])
                and interval_report["interval_ready_validation"]["raw_seq"] == 3
                and clock.now() < 1_000_000_000
                and beeps == [(1800, 420)]
                and commands.count("followprepare") == 1
                and not any(c in commands for c in ("followstart", "followstop"))
                and any(kind == "interval_ready_below_measurement_margin"
                        for kind, _ in events),
                "Interior READY was not validated before the baseline")

    for label, values, mode, heartbeat_ok, speed, moving, expected in (
        ("lower edge", [357] * 160, 1, True, 0.1, False,
         "No time for 2 s READY baseline"),
        ("upper edge", [520] * 40, 1, True, 0.1, False,
         "host upper margin"),
        ("mode", [384] * 40, 0, True, 0.1, False, "mode readback"),
        ("heartbeat", [384] * 40, 1, False, 0.1, False,
         "closed-loop heartbeat absent"),
    ):
        clock, commands, events, beeps, status, hb, raw, event, beep, query = interval_scenario(
            values, mode, heartbeat_ok, speed, moving)
        try:
            closeout_wait_for_stable_interval(
                0, status, hb, raw, lambda: False, event, beep,
                clock.now, clock.sleep, {})
        except RuntimeError as exc:
            require(expected in str(exc), "Wrong interval failure for " + label)
        else:
            raise RuntimeError("Invalid interval " + label + " reached measurement")
        attempted, _ = cleanup_stop_once(True, False, query)
        attempted, _ = cleanup_stop_once(True, attempted, query)
        require(attempted and commands.count("followstart") == 0
                and commands.count("followstop") == 1 and not beeps,
                "Interval " + label + " was not stopped before output")
    valid_final = {key: str(value) for key, value in {
        "phase": 6, "reader": 2, "fresh": 1, "zero": 1,
        "fault": 0, "output_allowed": 0, "cmd_torque": 0,
        "joint_mrad": 384,
    }.items()}
    hold_deg = 384 * DEG_PER_MRAD
    require(closeout_measure_final_ready(valid_final, hold_deg) == hold_deg,
            "Valid final READY rejected")
    for label, changes in (("fault", {"phase": "4", "fault": "1"}),
                           ("posture", {"joint_mrad": "395"})):
        invalid = {**valid_final, **changes}
        try:
            closeout_measure_final_ready(invalid, hold_deg)
        except RuntimeError:
            pass
        else:
            raise RuntimeError("Invalid final READY reached followstart: " + label)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true",
                        help="check host-only comparison helpers; no serial access")
    parser.add_argument("--run", action="store_true",
                        help="one authorized powered session")
    parser.add_argument("--closeout", action="store_true",
                        help="operator-gated 24 V OFF/ON IDLE and bounded S4 closeout; requires --run")
    parser.add_argument("--direct-idle", action="store_true",
                        help="with --run --closeout: skip OFF lift, wait for operator-completed powered IDLE")
    parser.add_argument("--static-idle", action="store_true",
                        help="with --run --closeout --measure-only: use powered static IDLE, no repeated IDLE lift")
    parser.add_argument("--operator-triggered", action="store_true",
                        help="with --static-idle --measure-only: operator message triggers followstart; no host posture-stability gate")
    parser.add_argument("--measure-only", action="store_true",
                        help="with --run --closeout: measure at a supported interior posture, then stop without reposition")
    parser.add_argument("--bin", type=Path)
    parser.add_argument("--readback", type=Path)
    parser.add_argument("--image-sha256")
    parser.add_argument("--target-cap-mnm", type=int, choices=(20, 40), default=20,
                        help="host readback bound; 40 requires the authorized gain36 image SHA256")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--expected-port", default="COM10")
    parser.add_argument("--video-reference", help="operator's video label for UTC/audio event alignment")
    parser.add_argument("--support-lever-arm-mm", type=float,
                        help="operator-measured perpendicular distance from shoulder axis to the gauge force line, mm")
    parser.add_argument("--force-tool-label", help="operator's force gauge label; no readings are inferred")
    args = parser.parse_args()
    if args.offline_check:
        require(not args.run and not args.direct_idle and not args.static_idle
                and not args.measure_only and not args.operator_triggered,
                "Offline check cannot select a powered session")
        offline_check()
        return
    target_cap_mnm = select_target_cap_mnm(args.target_cap_mnm, args.image_sha256)
    require(args.run, "No serial commands sent; pass --run for one live session")
    require(not args.closeout or args.run, "--closeout requires --run")
    require(not args.direct_idle or args.closeout,
            "--direct-idle requires --run --closeout")
    require(not args.static_idle or (args.closeout and args.measure_only and not args.direct_idle),
            "--static-idle requires --run --closeout --measure-only, without --direct-idle")
    require(not args.operator_triggered or (args.static_idle and args.measure_only),
            "--operator-triggered requires --static-idle --measure-only")
    require(not args.measure_only or args.closeout,
            "--measure-only requires --run --closeout")
    require(args.closeout or target_cap_mnm == 20,
            "40 mNm target guard requires --closeout")
    require(args.closeout or all(value is None for value in
                                 (args.video_reference, args.support_lever_arm_mm,
                                  args.force_tool_label)),
            "Video/measurement metadata requires --closeout")
    require(args.support_lever_arm_mm is None or
             (math.isfinite(args.support_lever_arm_mm)
              and 0.0 < args.support_lever_arm_mm <= 2000.0),
             "Support lever arm must be a finite measured distance in (0, 2000] mm")
    if args.closeout:
        require((args.static_idle or bool(args.video_reference and args.video_reference.strip()))
                and args.support_lever_arm_mm is not None
                and bool(args.force_tool_label and args.force_tool_label.strip()),
                "Closeout requires video reference, measured lever arm and force-tool label")
    require(all(value is None or len(value) <= 120 for value in
                (args.video_reference, args.force_tool_label)),
            "Video/force-tool labels must be at most 120 characters")
    require(all((args.bin, args.readback, args.image_sha256, args.out_dir)),
            "Live run requires BIN, readback, SHA256 and out-dir")
    bin_path, readback = on_h(args.bin), on_h(args.readback)
    out = on_h(args.out_dir)
    require((FW / "build").resolve() in out.parents,
            "Evidence output directory must be under firmware/build on H:")
    require(out.is_dir(), "Create the H-drive evidence directory before running")
    if args.closeout:
        require(not any(out.iterdir()),
                "Closeout evidence directory must be empty before the cold boot")
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
        if args.closeout:
            import msvcrt
    except ImportError as exc:
        raise RuntimeError("Audible cues and closeout keyboard gate require Windows") from exc

    summary_path = out / "r4-3-effect-summary.json"
    events_path = out / "r4-3-effect-events.jsonl"
    report: dict = {
        "result": "STARTED_DO_NOT_REPEAT",
        "mode": ("direct_powered_idle_effect_reposition_closeout" if args.direct_idle
                 else "off_on_idle_effect_reposition_closeout" if args.closeout
                 else "single_posture_effort_speed_trace"),
        "image": image, "target_cap_mnm_host_guard": target_cap_mnm,
        "started_utc": utc(), "events_file": str(events_path),
        "samples": [], "raw_samples": [],
        "operator_observation": {
            "same_elbow_load_grip_and_support": None,
            "zero_target_effort_or_force": None,
            "assisted_effort_or_force": None,
            "assistance_clearly_reduced_effort": None,
            "self_motion_oscillation_or_boundary_contact": None,
            "note": "Fill from the operator after the run; never infer force from command state"
        },
    }
    if args.measure_only:
        report["mode"] = ("operator_triggered_force_ab_closeout" if args.operator_triggered
                          else "static_powered_idle_force_ab_closeout" if args.static_idle
                          else "direct_powered_idle_measure_only_closeout" if args.direct_idle
                          else "off_on_idle_measure_only_closeout")
        report["reposition"] = {"skipped": True, "reason": "user_scope"}
        report["posture_selection"] = {
            "firmware_window_deg": [20.0, 30.0],
            "host_measurement_entry_deg": None if args.operator_triggered else [MIN_DEG, MAX_DEG],
            "host_runtime_margin_deg": None if args.operator_triggered else [MIN_DEG, MAX_DEG],
            "selection": ("Operator sends a message after holding a chosen posture; firmware alone decides if followstart is permitted"
                          if args.operator_triggered else
                          "Operator stops and supports inside the 20.5-29.5 degree host margin; long beep confirms validated zero-target READY, then the baseline checks the held posture"),
        }
    if args.closeout:
        report["metrology"] = {
            "video_reference": args.video_reference,
            "support_lever_arm_mm_operator_measured": args.support_lever_arm_mm,
            "support_lever_arm_definition": "perpendicular distance from shoulder axis to gauge force line",
            "force_tool_label": args.force_tool_label,
            "force_resolution_N_operator_reported": 0.1,
            "force_readings_collected_by_script": False,
            "time_alignment": ("Each event has UTC and host monotonic_ns; no video was confirmed"
                               if args.static_idle and not args.video_reference else
                               "Each event has UTC and host monotonic_ns; two short video-sync beeps are logged"),
        }
        report["operator_observation"].update({
            "off_idle_effort_or_force": None,
            "on_idle_effort_or_force": None,
            "reposition_obvious_blocking": None,
            "reposition_support_returned": None,
            "post_run_video_and_force_review_pending": True,
        })
    with summary_path.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    events = events_path.open("x", encoding="utf-8", buffering=1)
    link = None
    prepared = started = stop_attempted = False
    closeout_active_query_deadline_ns = None
    last_seq = -1
    last_sample_ms = -1

    def event(kind: str, **values) -> None:
        events.write(json.dumps({"utc": utc(), "monotonic_ns": time.monotonic_ns(),
                                 "kind": kind, **values}, ensure_ascii=False) + "\n")
        events.flush()

    def abort_requested() -> bool:
        if not args.closeout:
            return False
        while msvcrt.kbhit():
            key = msvcrt.getwch().lower()
            if key in ("n", "\x1b"):
                event("operator_abort_key", key="ESC" if key == "\x1b" else "N")
                return True
        return False

    def direct_idle_completion_key(clear: bool = False) -> bool | None:
        """Nonblocking Y completion / N-Esc abort while read-only guards continue."""
        require(args.direct_idle, "Direct IDLE completion key is direct-only")
        while msvcrt.kbhit():
            key = msvcrt.getwch().lower()
            if key in ("n", "\x1b"):
                event("operator_direct_idle_key", key="ESC" if key == "\x1b" else "N",
                      accepted=False)
                return False
            if key == "y" and not clear:
                event("operator_direct_idle_key", key="Y", accepted=True)
                return True
        return None

    def gate(label: str, timeout_s: float) -> bool:
        require(args.closeout, "Operator gate is closeout-only")
        # Discard early keypresses so a stale Y cannot authorize a later step.
        while msvcrt.kbhit():
            msvcrt.getwch()
        event("operator_gate_open", gate=label, timeout_s=timeout_s)
        print(f"GATE {label}: press Y to confirm, N/Esc to stop; timeout {timeout_s:g}s", flush=True)
        deadline = time.monotonic_ns() + int(timeout_s * 1e9)
        while time.monotonic_ns() < deadline:
            if msvcrt.kbhit():
                key = msvcrt.getwch().lower()
                if key in ("y", "n", "\x1b"):
                    accepted = key == "y"
                    event("operator_gate_result", gate=label, accepted=accepted,
                          key="ESC" if key == "\x1b" else key.upper())
                    return accepted
            time.sleep(0.03)
        event("operator_gate_result", gate=label, accepted=False, key="TIMEOUT")
        return False

    def cue(kind: str) -> None:
        patterns = {
            "video_sync": ((950, 110), (1450, 110)),
            "off_move": ((600, 130),),
            "off_move_end": ((600, 100), (600, 100)),
            "on_move": ((950, 130),),
            "on_move_end": ((950, 100), (950, 100)),
            "push": ((1650, 100),),
            "hold": ((800, 100),),
        }
        for index, (frequency, duration) in enumerate(patterns[kind]):
            if index:
                time.sleep(0.08)
            winsound.Beep(frequency, duration)

    def query(command: str) -> tuple[str, dict, int]:
        require(command in COMMANDS, "Command outside bounded R4-3 whitelist")
        if args.closeout:
            closeout_query_budget(time.monotonic_ns(), closeout_active_query_deadline_ns,
                                  stop_attempted, command)
        raw_command = (command + "\n").encode("ascii")
        sent_ns = time.monotonic_ns()
        if args.closeout and command == "followstop":
            report["followstop_host_dispatch_ns"] = sent_ns
            if "started_monotonic_ns" in report:
                report["followstop_host_dispatch_offset_s"] = (
                    sent_ns - report["started_monotonic_ns"]) / 1e9
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

    def raw_sample(stage: str) -> dict:
        feedback_line, feedback, received_ns = query("s3feedback")
        properties_line, properties, target_received_ns = query("s3properties")
        require(feedback_line.startswith("OK s3feedback ")
                and properties_line.startswith("OK s3properties "),
                "Cached raw telemetry unavailable")
        pos_rev = float32_bits(feedback, "pos")
        vel_rev_s = float32_bits(feedback, "vel")
        target_nm = float32_bits(properties, "tq")
        now_ms = base.number(feedback, "now")
        position_received_ms = base.number(feedback, "pr")
        velocity_received_ms = base.number(feedback, "vr")
        target_now_ms = base.number(properties, "now")
        target_requested_ms = base.number(properties, "zq")
        row = {
            "stage": stage, "at_ns": received_ns,
            "target_at_ns": target_received_ns,
            "seq": base.number(feedback, "seq"),
            "target_seq": base.number(properties, "seq"),
            "now_ms": now_ms,
            "position_bits": feedback["pos"], "velocity_bits": feedback["vel"],
            "position_rev": pos_rev, "velocity_rev_s": vel_rev_s,
            "nominal_joint_velocity_deg_s": vel_rev_s * 45.0,
            "position_requested_ms": base.number(feedback, "pq"),
            "position_received_ms": position_received_ms,
            "velocity_requested_ms": base.number(feedback, "vq"),
            "velocity_received_ms": velocity_received_ms,
            "position_age_ms": (now_ms - position_received_ms) & 0xffffffff,
            "velocity_age_ms": (now_ms - velocity_received_ms) & 0xffffffff,
            "target_bits": properties["tq"],
            "reported_input_torque_target_nm": target_nm,
            "target_now_ms": target_now_ms,
            "target_requested_ms": target_requested_ms,
            "target_request_age_ms": (target_now_ms - target_requested_ms) & 0xffffffff,
            "s3_fresh_reported": base.number(feedback, "fresh"),
            "s3_output_reported": base.number(feedback, "out"),
            "note": "Cached rotor feedback; nominal 8:1 conversion. Target is not measured torque.",
        }
        if args.closeout:
            mode_requested_ms = base.number(properties, "mq")
            input_requested_ms = base.number(properties, "iq")
            row.update({
                "control_mode": base.number(properties, "cm"),
                "input_mode": base.number(properties, "im"),
                "mode_requested_ms": mode_requested_ms,
                "input_requested_ms": input_requested_ms,
                "mode_request_age_ms": (target_now_ms - mode_requested_ms) & 0xffffffff,
                "input_request_age_ms": (target_now_ms - input_requested_ms) & 0xffffffff,
                "s3_properties_fresh_reported": base.number(properties, "fresh"),
            })
        report["raw_samples"].append(row)
        return row

    def closeout_active_sample(stage: str):
        f = status()
        active_status(f)
        ramp_ms = base.number(f, "ramp_in_ms")
        require(ramp_ms > 2000, "Automatic ramp too near for closeout")
        raw = raw_sample(stage)
        closeout_raw_guard(raw, False, target_cap_mnm)
        row = active_angle_row()
        return row, raw, ramp_ms

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
        if not args.operator_triggered:
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
        if args.closeout:
            link.device.timeout = CLOSEOUT_SERIAL_TIMEOUT_S
            link.device.write_timeout = CLOSEOUT_SERIAL_TIMEOUT_S
        cold = status()
        require(all(base.number(cold, key) == value for key, value in {
            "phase": 1, "reader": 1, "seq": 0, "zero": 0,
            "output_allowed": 0, "cmd_torque": 0, "fault": 0,
        }.items()), "Not cold SAFE_OFF with unused reader")
        report["cold"] = cold
        def begin_prepare():
            nonlocal prepared
            prep_at = time.monotonic_ns()
            prepared = True  # a timed-out reply may still have reached the MCU
            prep_line, _, _ = query("followprepare")
            require(prep_line == "OK FOLLOW preparing output_allowed=0",
                    "followprepare not accepted")
            return prep_at

        if args.static_idle:
            def host_sync_before_zero():
                print("HOST SYNC: remain vertical and supported. The host will start RAM zero; do not lift until three short beeps", flush=True)
                require(gate("HOST_SYNC_BEFORE_RAM_ZERO", 120.0),
                        "Host did not release RAM zero")
            prep_at = closeout_static_idle_before_prepare(
                query, report, begin_prepare,
                before_prepare=host_sync_before_zero if args.operator_triggered else None)
            shoulder_before = report["shoulder_before"]
        elif args.direct_idle:
            prep_at = closeout_direct_idle_before_prepare(
                query, gate, direct_idle_completion_key, abort_requested, cue,
                event, time.monotonic_ns, time.sleep, report, begin_prepare)
            shoulder_before = report["shoulder_before"]
        elif args.closeout:
            prep_at = closeout_before_prepare(query, gate, abort_requested, cue, event,
                                              time.monotonic_ns, time.sleep, report,
                                              begin_prepare)
            shoulder_before = report["shoulder_before"]
        else:
            report["idle_heartbeat_age_ms_before"] = heartbeat(1)
            shoulder_line, shoulder_before, _ = query("shoulder")
            require(shoulder_line.startswith("OK ") and all(
                base.number(shoulder_before, key) == 0 for key in
                ("drop", "invalid", "fov", "err", "bus_off")),
                "Shoulder CAN has pre-session errors")
            report["shoulder_before"] = shoulder_before
            prep_at = begin_prepare()
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

        if args.operator_triggered:
            while msvcrt.kbhit():
                key = msvcrt.getwch().lower()
                if key in ("n", "\x1b"):
                    raise RuntimeError("Operator cancelled before positioning cue")
            event("operator_trigger_buffer_cleared_before_positioning_cue")
            print("POSITION: after three short beeps, move slowly to the interior of the original 20-30 degree range. Hold the gauge point, then send a message; the host will trigger support. No long-beep reaction or host angle-change gate", flush=True)
            event("operator_cue", cue="THREE_SHORT_BEEPS_MOVE_THEN_MESSAGE",
                  firmware_window_deg=[20.0, 30.0],
                  note="RAM zero already captured at vertical; operator message triggers followstart")
            for count in range(3):
                winsound.Beep(1200, 130)
                if count < 2:
                    time.sleep(0.05)
            while msvcrt.kbhit():
                key = msvcrt.getwch().lower()
                if key in ("n", "\x1b"):
                    raise RuntimeError("Operator cancelled during positioning cue")
            event("operator_trigger_gate_open_after_three_beeps")
            ready, report["operator_trigger"] = wait_for_operator_trigger(
                prep_at, status, msvcrt.kbhit, msvcrt.getwch, event,
                time.monotonic_ns, time.sleep)
            report["ready_at_operator_trigger"] = ready
            print("OPERATOR TRIGGER RECEIVED: sending followstart; firmware will accept only if its own READY and safety checks pass", flush=True)
        else:
            if args.measure_only:
                print("ZERO_READY: after three beeps, lift slowly into 20.5-29.5 degrees, STOP there immediately and hold the same gauge point; long beep confirms valid READY",
                      flush=True)
                event("operator_cue", cue="LIFT_SLOWLY_STOP_AND_SUPPORT_IN_INTERVAL",
                      firmware_window_deg=[20.0, 30.0],
                      host_measurement_entry_deg=[MIN_DEG, MAX_DEG])
            else:
                print("ZERO_READY: after three beeps, lift slowly; stop and support at the long beep",
                      flush=True)
                event("operator_cue", cue="LIFT_SLOWLY_UNTIL_LONG_BEEP",
                      firmware_window_deg=[20.0, 30.0])
            for count in range(3):
                winsound.Beep(1200, 130)
                if count < 2:
                    time.sleep(0.05)
            if args.measure_only:
                ready = closeout_wait_for_stable_interval(
                    prep_at, status, heartbeat, raw_sample, abort_requested, event,
                    winsound.Beep, time.monotonic_ns, time.sleep, report,
                    settle_first=args.static_idle, target_cap_mnm=target_cap_mnm)
            elif args.closeout:
                ready = closeout_wait_for_ready_at_cue(
                    prep_at, status, heartbeat, raw_sample, abort_requested, event,
                    winsound.Beep, time.monotonic_ns, time.sleep, report,
                    target_cap_mnm=target_cap_mnm)
            else:
                ready = None
                hold_cue_sent = False
                while time.monotonic_ns() - prep_at < PREPARE_DEADLINE_NS:
                    require(not abort_requested(), "Operator requested preparation abort")
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
            baseline_ns = (BASELINE_NS if args.static_idle else
                           CLOSEOUT_BASELINE_NS if args.closeout else BASELINE_NS)
            # The baseline and a bounded command reply must still fit the 20 s
            # firmware prepare TTL; no fixed, narrow posture-timing gate is used.
            require(time.monotonic_ns() + baseline_ns + 400_000_000
                    < prep_at + 20_000_000_000,
                    "READY too late for zero-target comparison before prepare TTL")
            if args.closeout:
                report["ready_closed_loop_hb_age_ms"] = heartbeat(8)
            if args.static_idle:
                print("SETTLED: keep holding the same gauge point; one second zero-target stability check before motor support", flush=True)
            elif args.closeout:
                print("BASELINE: same grip and support, zero target for about 2 seconds", flush=True)
            else:
                print("BASELINE: same grip and support, zero target for about 1 second", flush=True)
            winsound.Beep(700, 100)
            baseline_at = time.monotonic_ns()
            if args.closeout:
                require(baseline_at - prep_at < CLOSEOUT_BASELINE_LATEST_NS,
                        "No time for 2 s READY baseline and followstart before prepare TTL")
            event("operator_cue", cue="ZERO_TARGET_BASELINE_START", marker_ns=baseline_at)
            baseline_rows = []
            baseline_raw_rows = []
            baseline_seq = -1
            while time.monotonic_ns() - baseline_at < baseline_ns:
                require(not abort_requested(), "Operator requested READY abort")
                f = status()
                require(all(base.number(f, key) == value for key, value in {
                    "phase": 6, "reader": 2, "fresh": 1, "zero": 1,
                    "fault": 0, "output_allowed": 0, "cmd_torque": 0,
                }.items()), "READY zero-target baseline became invalid")
                require(f.get("joint_mrad") != "na", "Baseline angle absent")
                deg = int(f["joint_mrad"]) * 180.0 / (math.pi * 1000.0)
                require(interior(deg), "Baseline reached host edge margin")
                if args.measure_only:
                    require(abs(deg - report["hold_cue"]["angle_deg"])
                            <= CLOSEOUT_SAME_POSTURE_MAX_DELTA_DEG,
                            "READY baseline left the supported measurement posture")
                elif args.closeout:
                    require(CLOSEOUT_CUE_DEG <= deg <= CLOSEOUT_CUE_MAX_DEG,
                            "READY baseline left the 25 degree hold range")
                seq = base.number(f, "seq")
                if seq > baseline_seq:
                    baseline_seq = seq
                    baseline_rows.append({"at_ns": time.monotonic_ns(),
                                          "seq": seq, "angle_deg": deg})
                raw = raw_sample("ready_zero_target")
                if args.closeout:
                    closeout_raw_guard(raw, True, target_cap_mnm)
                    baseline_raw_rows.append(raw)
                time.sleep(0.065)
            require(len(baseline_rows) >= 4,
                    "Too few fresh READY zero-target samples")
            if args.closeout:
                report["ready_baseline_standstill"] = closeout_baseline_standstill(
                    baseline_raw_rows, measure_only=args.measure_only)
            report["ready_zero_target_baseline"] = {
                **summarize_angles(baseline_rows),
                "duration_s": (time.monotonic_ns() - baseline_at) / 1_000_000_000,
                "note": "ClosedLoop READY with zero torque target; hand force not measured",
            }
            event("operator_cue", cue="ZERO_TARGET_BASELINE_END")
            if args.measure_only:
                require(not abort_requested(), "Operator requested READY abort")
                report["final_ready_closed_loop_hb_age_ms"] = heartbeat(8)
                final_ready = status()
                report["final_ready_angle_deg"] = closeout_measure_final_ready(
                    final_ready, report["hold_cue"]["angle_deg"])
                event("final_ready_validated", angle_deg=report["final_ready_angle_deg"],
                      seq=unsigned(final_ready, "seq"))
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
        if args.closeout:
            closeout_active_query_deadline_ns = start_at + CLOSEOUT_QUERY_CUTOFF_NS
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
        if args.closeout:
            closeout_active_tail(start_at, start_ramp_in_ms, closeout_active_sample,
                                  abort_requested, cue, event, time.monotonic_ns,
                                  time.sleep, stop_and_observe, report,
                                  args.measure_only, args.operator_triggered)
        else:
            assisted_at = time.monotonic_ns()
            event("operator_cue", cue="ASSISTED_COMPARISON_START",
                  marker_ns=assisted_at,
                  note="Command state observed; physical torque and effort not measured")
            assisted_rows: list[dict] = []
            while time.monotonic_ns() - assisted_at < ASSIST_NS:
                raw_sample("assist")
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
        if args.static_idle:
            event("operator_cue", cue="MOTOR_SUPPORT_OFF_READ_FORCE_NOW",
                  note="Software STOPPED and IDLE observed; hold the same posture and gauge point")
            print("MOTOR SUPPORT OFF: one low beep, hold the same posture and read the gauge for 2 seconds; two low beeps mark the end. Keep supporting the arm", flush=True)
            winsound.Beep(700, 250)
            unassisted_at = time.monotonic_ns()
            off_samples = 0
            while time.monotonic_ns() - unassisted_at < 2_000_000_000:
                stopped = status()
                require(all(base.number(stopped, key) == value for key, value in {
                    "phase": 3, "reader": 7, "fault": 0,
                    "output_allowed": 0, "cmd_torque": 0,
                }.items()), "Motor support did not remain stopped during force reading")
                off_samples += 1
                time.sleep(0.15)
            report["post_stop_force_window"] = {
                "duration_s": (time.monotonic_ns() - unassisted_at) / 1e9,
                "status_samples": off_samples,
                "force_reading_collected_by_script": False,
            }
            event("operator_cue", cue="POST_STOP_FORCE_READING_END")
            winsound.Beep(700, 100)
            winsound.Beep(700, 100)
        if args.closeout:
            event("operator_cue", cue="STOPPED_KEEP_SUPPORT_CUT_24V",
                  note="Software STOPPED and IDLE are not physical unloading proof")
            print("STOPPED/IDLE observed: keep supporting the arm and switch 24 V OFF; report the physical power state separately", flush=True)
            report["power_off_after_stop_operator_confirmed_in_script"] = False
            report["power_off_after_stop_note"] = (
                "Physical power-off is not sensed; retain separate operator confirmation")
        if args.operator_triggered and report.get("assisted_target_nonzero_samples", 0) < 4:
            report["result"] = "STOPPED_WITH_INSUFFICIENT_NONZERO_TARGET_EFFECT_UNVERIFIED"
        else:
            report["result"] = ("CONTROL_TRACE_READY_CLOSEOUT_PENDING_VIDEO_AND_OPERATOR"
                                if args.closeout else "CONTROL_TRACE_READY_EFFECT_PENDING_OPERATOR")
    except BaseException as exc:
        report["result"] = "FAIL_OR_INCOMPLETE"
        report["reason"] = str(exc)
        if link is not None and prepared and not stop_attempted:
            stop_attempted, stop_result = cleanup_stop_once(prepared, stop_attempted, query)
            if isinstance(stop_result, BaseException):
                report["emergency_followstop_error"] = str(stop_result)
            else:
                report["emergency_followstop_reply"] = stop_result
        if link is not None and prepared:
            try:
                raw_sample("after_abort_stop_attempt")
            except BaseException as telemetry_exc:
                report["after_abort_telemetry_error"] = str(telemetry_exc)
        if args.measure_only:
            event("operator_cue", cue="TWO_LOW_BEEPS_ABORT_STOP_AND_SUPPORT",
                  reason=str(exc))
            try:
                winsound.Beep(450, 130)
                winsound.Beep(450, 130)
            except BaseException as cue_exc:
                report["abort_tone_error"] = str(cue_exc)
        if args.closeout and not prepared:
            event("operator_cue", cue="NO_PREPARE_SUPPORT_AND_TURN_OFF_24V",
                  reason=str(exc))
            print("ABORT BEFORE PREPARE: stop pushing, support arm, turn off 24 V", flush=True)
        else:
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
