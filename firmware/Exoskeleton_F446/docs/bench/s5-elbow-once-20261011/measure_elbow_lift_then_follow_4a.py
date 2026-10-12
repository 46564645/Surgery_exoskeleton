#!/usr/bin/env python3
"""Lift an output-off elbow to 20 degrees, then read force during R4 4 A FOLLOW.

The operator supplies the physical extended (0-degree) starting pose. The
first high tone means stop lifting. R4 captures its RAM zero at the actual
stopped angle (20-30 degrees); a later low tone means the nonzero FOLLOW
current target is active and the operator can read F_on. F_off is not repeated.
This script records the 4 A candidate BIN identity; it does not verify board Flash.
"""

import argparse
import io
import json
import re
import sys
import time
from datetime import datetime
from pathlib import Path

from elbow_once import Link, TrialError, open_vcp, require
from measure_elbow_force_once import elbow_sample, shortest_ticks, s5_status


TICKS_PER_TURN = 8192  # C620 single-turn feedback; reported elbow coupling 1:1.
CANDIDATE_BIN_SHA256 = "600b6724d3d6e04a332ab71b8b1cb9565cb72ee457872c78cee70cc8d9631e84"
EXPECTED_CURRENT_TARGET_MA = 4000  # Offline candidate build setting; not measured current.
TARGET_DEG = 20.0
STOPPED_MAX_DEG = 30.0  # Host stop window; R4 remains +/-5 degrees from its RAM zero.
LIFT_TIMEOUT_S = 120.0
SETTLE_S = 0.7  # Time for the operator to stop after the high tone; not a gate.
READY_TIMEOUT_S = 2.3  # R4 prepare timeout is 2.5 s.
TARGET_TIMEOUT_S = 0.8
READING_S = 2.2
STOP_TIMEOUT_S = 2.0
POLL_S = 0.08


def physical_angle(sample, zero_tick):
    return abs(shortest_ticks(sample["tick"], zero_tick)) * 360.0 / TICKS_PER_TURN


def run_once(link, beep, clock=time.monotonic, sleep=time.sleep):
    """Run at most one R4 session; the operator need not answer during output."""
    error = None
    zero_tick = None
    stop_cue = active_cue = None
    settle_angle = None
    prepare_attempted = False
    start_attempts = 0
    start_outcome = "not_attempted"
    stop_reply = None
    final = {}
    try:
        mode = s5_status(link)
        require(mode.get("phase") == "1" and mode.get("output_allowed") == "0",
                "R4 elbow-only session is not IDLE with output off")
        zero = elbow_sample(link)
        require(zero["output_allowed"] == 0, "output is enabled at physical zero")
        zero_tick = zero["tick"]
        link.record(direction="marker", name="OPERATOR_EXTENDED_ZERO",
                    angle_ticks=zero_tick, feedback=zero,
                    source="operator_confirmed_physical_zero_1_to_1")
        print("Zero captured. Two low tones: start slowly flexing the elbow.",
              flush=True)
        beep(650, 100)
        sleep(0.12)
        beep(650, 100)
        link.record(direction="marker", name="START_LIFT_TWO_LOW_TONES")

        lift_deadline = clock() + LIFT_TIMEOUT_S
        while True:
            sample = elbow_sample(link)
            require(sample["output_allowed"] == 0,
                    "output unexpectedly enabled before FOLLOW")
            angle = physical_angle(sample, zero_tick)
            require(angle <= STOPPED_MAX_DEG,
                    f"20-degree cue was missed before 30 degrees: {angle:.2f}")
            if angle >= TARGET_DEG:
                stop_cue = {"angle_deg": round(angle, 3), "feedback": sample}
                link.record(direction="marker", name="HIGH_STOP_TONE_20_DEG",
                            **stop_cue)
                print(f"HIGH TONE: crossed 20 degrees ({angle:.2f}); stop lifting.",
                      flush=True)
                beep(2200, 300)
                break
            require(clock() < lift_deadline, "20-degree lift timed out")
            sleep(POLL_S)

        sleep(SETTLE_S)
        settled = elbow_sample(link)
        settle_angle = physical_angle(settled, zero_tick)
        require(settled["output_allowed"] == 0
                and TARGET_DEG <= settle_angle <= STOPPED_MAX_DEG,
                f"stopped elbow angle is outside 20-30 degrees: {settle_angle:.2f}")
        link.record(direction="marker", name="STOPPED_POSE_FOR_RAM_ZERO",
                    angle_deg=round(settle_angle, 3), feedback=settled)

        # R4 itself waits for stationary feedback before declaring READY.
        prepare_attempted = True
        require(link.ask("followprepare") == "OK S5 preparing output_allowed=0",
                "R4 followprepare refused")
        ready_deadline = clock() + READY_TIMEOUT_S
        while True:
            mode = s5_status(link)
            require(mode.get("phase") != "8", "R4 fault while preparing")
            if mode.get("phase") == "3":
                require(mode.get("gate") == "5" and mode.get("zero_el") == "1"
                        and mode.get("output_allowed") == "0",
                        "R4 READY or elbow RAM zero unavailable")
                sample = elbow_sample(link)
                ready_angle = physical_angle(sample, zero_tick)
                require(TARGET_DEG <= ready_angle <= STOPPED_MAX_DEG,
                        f"READY pose is outside 20-30 degrees: {ready_angle:.2f}")
                link.record(direction="marker", name="READY_POSE",
                            angle_deg=round(ready_angle, 3), feedback=sample)
                break
            require(clock() < ready_deadline, "R4 READY timed out")
            sleep(POLL_S)

        start_attempts = 1  # A lost serial reply never leads to another start.
        start_outcome = "unknown"
        try:
            reply = link.ask("followstart", timeout_s=0.15)
        except TrialError as exc:
            if str(exc) == "followstart: ERR blocked":
                start_outcome = "refused"
            raise
        match = re.fullmatch(
            r"OK S5 started axes=2 expires_in_ms=([0-9]+); support arm before stop",
            reply)
        require(match is not None, "R4 followstart reply unavailable")
        start_outcome = "accepted"
        started_at = clock()
        session_deadline = started_at + int(match.group(1)) / 1000.0
        target_deadline = started_at + TARGET_TIMEOUT_S
        while True:
            mode = s5_status(link, timeout_s=0.10)
            require(mode.get("phase") == "4" and mode.get("output_allowed") == "1"
                    and mode.get("stop_failed") == "0",
                    "R4 left RUNNING before the force reading")
            target = mode.get("cmd_el_mA", "na")
            require(target.lstrip("-").isdecimal(), "R4 current target unavailable")
            if int(target) != 0:
                active_cue = {"cmd_el_mA": int(target), "status": mode}
                link.record(direction="marker", name="LOW_F_ON_TONE_TARGET_NONZERO",
                            **active_cue)
                print("LOW TONE: FOLLOW target is active; read F_on now.",
                      flush=True)
                beep(850, 250)
                break
            require(clock() < target_deadline, "R4 current target stayed zero")
            sleep(POLL_S)

        # The host stops within R4's five-second session, including its ramp.
        require(clock() + READING_S + 0.5 < session_deadline,
                "insufficient R4 session time for the force reading")
        reading_deadline = clock() + READING_S
        while clock() < reading_deadline:
            mode = s5_status(link, timeout_s=0.10)
            target = mode.get("cmd_el_mA", "na")
            require(mode.get("phase") == "4" and mode.get("output_allowed") == "1"
                    and mode.get("stop_failed") == "0"
                    and target.lstrip("-").isdecimal() and int(target) != 0,
                    "R4 active target ended during the force reading")
            link.record(direction="sample", name="F_ON_WINDOW", status=mode)
            sleep(POLL_S)
    except (Exception, KeyboardInterrupt) as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if prepare_attempted:
            try:
                stop_reply = link.ask("followstop", timeout_s=0.15)
                require(stop_reply in ("OK S5 ramping_down; support arm",
                                       "OK S5 session_closed; support arm"),
                        "R4 followstop refused")
            except Exception as exc:
                error = (error + "; " if error else "") + \
                    f"stop: {type(exc).__name__}: {exc}"
            try:
                deadline = clock() + STOP_TIMEOUT_S
                while True:
                    final = s5_status(link)
                    if final.get("phase") in ("7", "8"):
                        break
                    require(clock() < deadline, "R4 stop observation timed out")
                    sleep(POLL_S)
                require(final.get("output_allowed") == "0",
                        "R4 output remains allowed after stop")
                require(final.get("stop_failed") == "0",
                        "R4 stop-frame delivery failed")
                if start_outcome not in ("not_attempted", "refused"):
                    require(final.get("stop") == "1",
                            "R4 zero-frame acceptance unconfirmed")
                require(final.get("phase") == "7" and final.get("gate") == "0",
                        f"R4 ended in phase={final.get('phase')} gate={final.get('gate')}; "
                        "normal STOPPED unavailable")
            except Exception as exc:
                error = (error + "; " if error else "") + \
                    f"post-stop: {type(exc).__name__}: {exc}"
        link.record(direction="result", zero_tick=zero_tick,
                    high_stop_cue=stop_cue, stopped_angle_deg=settle_angle,
                    low_active_cue=active_cue, prepare_attempted=prepare_attempted,
                    start_attempts=start_attempts, start_outcome=start_outcome,
                    stop_reply=stop_reply,
                    final_phase=final.get("phase"), error=error)
    return {"high_stop_cue": stop_cue, "stopped_angle_deg": settle_angle,
            "low_active_cue": active_cue, "prepare_attempted": prepare_attempted,
            "start_attempts": start_attempts, "start_outcome": start_outcome,
            "stop_reply": stop_reply,
            "final_phase": final.get("phase"), "error": error}


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeLink:
    def __init__(self, failure=None):
        self.failure = failure
        self.commands = []
        self.phase = "idle"
        self.stop_reads = 0
        self.feedback_reads = 0
        self.running_reads = 0
        self.journal = io.StringIO()

    def record(self, **event):
        self.journal.write(json.dumps(event) + "\n")

    def ask(self, command, timeout_s=None):
        self.commands.append(command)
        if command == "ping":
            return "OK pong"
        if command == "status":
            return "OK stage=S2_CAN_RX output_allowed=0"
        if command == "canstatus":
            return "OK can2_started=1 can2_baud=1000000 can2_start_error=0"
        if command == "elbowfeedback":
            self.feedback_reads += 1
            count = self.feedback_reads
            delta = (0 if count <= 2 else 150 if count == 3
                     else 300 if count == 4 else 456 if count == 5 else 520)
            if self.failure == "missed_cue" and count >= 5:
                delta = 750
            if self.failure == "overshoot" and count >= 6:
                delta = 750
            if self.failure == "late_stop_28" and count >= 6:
                delta = 637
            age = 200 if self.failure == "stale" and count == 4 else 10
            return ("OK bus=elbow motor_id=1 seen=1 valid=1 online=1 "
                    f"angle_ticks={(7900 + delta) % TICKS_PER_TURN} rpm=0 "
                    f"current_raw=0 received_ms={count * 10} age_ms={age} "
                    f"output_allowed={int(self.phase == 'running')}")
        if command == "followprepare":
            if self.failure == "prepare":
                return "ERR blocked"
            self.phase = "ready"
            return "OK S5 preparing output_allowed=0"
        if command == "followstart":
            if self.failure == "start":
                raise TrialError("followstart: ERR blocked")
            self.phase = "running"
            return "OK S5 started axes=2 expires_in_ms=5000; support arm before stop"
        if command == "followstop":
            prior = self.phase
            self.phase = "stopping"
            return ("OK S5 ramping_down; support arm" if prior == "running"
                    else "OK S5 session_closed; support arm")
        if command == "followstatus":
            if self.phase == "stopping":
                self.stop_reads += 1
                if self.stop_reads >= 2:
                    self.phase = "stopped"
            if self.phase == "running":
                self.running_reads += 1
            phase, gate, allowed, stop, zero = {
                "idle": (1, 0, 0, 0, 0), "ready": (3, 5, 0, 0, 1),
                "running": (4, 2, 1, 0, 1), "stopping": (5, 6, 0, 0, 1),
                "stopped": (7, 0, 0, int(self.failure != "start"), 0)
            }[self.phase]
            target = (0 if self.failure == "zero_target" or self.running_reads == 1
                      else EXPECTED_CURRENT_TARGET_MA)
            return (f"OK s5=1 axes=2 phase={phase} gate={gate} "
                    f"q_el_mrad=0 cmd_el_mA={target} zero_el={zero} "
                    f"output_allowed={allowed} stop={stop} stop_failed=0")
        raise AssertionError(command)


def offline_check():
    require(shortest_ticks(50, 8100) == 142, "encoder positive wrap")
    require(shortest_ticks(8100, 50) == -142, "encoder negative wrap")
    for failure in (None, "late_stop_28", "stale", "missed_cue", "overshoot",
                    "prepare", "start", "zero_target"):
        link = FakeLink(failure)
        clock = FakeClock()
        tones = []
        result = run_once(link, lambda hz, ms: tones.append((hz, ms)),
                          clock.clock, clock.sleep)
        require(link.commands.count("followprepare") <= 1
                and link.commands.count("followstart") <= 1
                and link.commands.count("followstop") <= 1,
                f"{failure}: repeated motion command")
        require(bool(result["error"]) ==
                (failure not in (None, "late_stop_28")),
                f"{failure}: expected result")
        if failure == "start":
            require(result["start_outcome"] == "refused",
                    "explicit start rejection must not be logged as unknown")
        require(sum(hz == 2200 for hz, _ in tones) ==
                (0 if failure in ("stale", "missed_cue") else 1),
                f"{failure}: high stop tone count")
        require(sum(hz == 850 for hz, _ in tones) ==
                (1 if failure in (None, "late_stop_28") else 0),
                f"{failure}: low active tone count")
        if failure in ("missed_cue", "overshoot"):
            require(750 * 360.0 / TICKS_PER_TURN > STOPPED_MAX_DEG
                    and link.commands.count("followprepare") == 0
                    and link.commands.count("followstart") == 0,
                    f"{failure}: over-30-degree pose must not start FOLLOW")
        if failure in (None, "late_stop_28"):
            require(20.0 <= result["stopped_angle_deg"] <= STOPPED_MAX_DEG
                    and result["final_phase"] == "7",
                    "successful stopped pose and stop not logged")
            require(result["low_active_cue"]["cmd_el_mA"] == EXPECTED_CURRENT_TARGET_MA,
                    "4 A candidate simulated current target not logged")
        if failure == "late_stop_28":
            require(27.9 <= result["stopped_angle_deg"] <= 28.1
                    and link.commands.count("followprepare") == 1
                    and link.commands.count("followstart") == 1
                    and link.commands.count("followstop") == 1,
                    "28-degree stop must complete exactly one FOLLOW session")
        print(f"offline {failure or 'success'}: high={sum(hz == 2200 for hz, _ in tones)} "
              f"active={sum(hz == 850 for hz, _ in tones)} "
              f"start={link.commands.count('followstart')} "
              f"stop={link.commands.count('followstop')} "
              f"result={'aborted' if result['error'] else 'stopped'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true", help="fake VCP only")
    parser.add_argument("--run", action="store_true", help="request one R4 FOLLOW")
    parser.add_argument("--port", help="explicit ST-LINK VCP port")
    parser.add_argument("--output-dir", type=Path, help="H: raw JSONL directory")
    args = parser.parse_args()
    if args.offline_check:
        offline_check()
        return 0
    require(args.run and args.port and args.output_dir,
            "live use needs --run, --port and --output-dir")
    output = args.output_dir.resolve()
    require(output.drive.upper() == "H:", "output-dir must resolve to H: drive")
    try:
        import winsound
    except ImportError as exc:
        raise TrialError("Windows winsound is required for the tones") from exc
    output.mkdir(parents=True, exist_ok=True)
    path = output / ("s5-elbow-lift-follow-4a-" +
                     datetime.now().strftime("%Y%m%d-%H%M%S") + ".jsonl")
    with path.open("x", encoding="utf-8", newline="\n") as journal:
        port = open_vcp(args.port)
        try:
            link = Link(port, journal)
            link.record(direction="metadata", candidate_bin_sha256=CANDIDATE_BIN_SHA256,
                        hash_source="offline_candidate_bin_not_board_checked_by_script",
                        expected_current_target_mA=EXPECTED_CURRENT_TARGET_MA,
                        target_deg=TARGET_DEG, stopped_window_deg=[20, 30],
                        operator_pose="physical_elbow_extended_zero_shoulder_fixed",
                        reading="F_on_only", prior_F_off_approx_N=0.4,
                        prior_F_off_pose_uncertain=True)
            result = run_once(link, winsound.Beep)
        finally:
            port.close()
    print(json.dumps({"transcript": str(path), **result}, ensure_ascii=False))
    return 1 if result["error"] else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (TrialError, OSError) as exc:
        print(f"ABORT: {exc}", file=sys.stderr)
        sys.exit(1)
