#!/usr/bin/env python3
"""Read one assisted elbow force value at an operator-held 20-degree pose.

The operator sets the physical elbow angle before launch. This script records
only F_on: one R4 prepare/start, one high beep after nonzero target readback,
about 2.2 seconds for the gauge, then one stop. F_off is not remeasured.
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
from follow_once import preflight
from measure_elbow_force_once import elbow_sample, shortest_ticks, s5_status


PREPARE_TIMEOUT_S = 2.3  # R4 firmware timeout 2.5 s.
TARGET_TIMEOUT_S = 0.8
READING_S = 2.2
STOP_TIMEOUT_S = 2.0
POLL_S = 0.08
MAX_POSE_DRIFT_TICKS = 46  # About 2 degrees at the reported 1:1 elbow assembly.


def pose_held(link, reference_tick):
    sample = elbow_sample(link)
    require(abs(shortest_ticks(sample["tick"], reference_tick)) <= MAX_POSE_DRIFT_TICKS
            and abs(sample["rpm"]) <= 2,
            "elbow moved from the operator-held force-reading pose")
    return sample


def run_once(link, beep, clock=time.monotonic, sleep=time.sleep):
    error = None
    prepare_attempted = False
    start_attempts = 0
    started = False
    beep_record = None
    reference = None
    stop_reply = None
    final = {}
    try:
        preflight(link)
        reference = pose_held(link, elbow_sample(link)["tick"])
        require(reference["output_allowed"] == 0,
                "elbow output is not off before prepare")
        link.record(direction="marker", name="OPERATOR_HELD_ELBOW_20_DEG",
                    reference=reference, source="operator_physical_pose_not_encoder_calibration",
                    existing_F_off_approx_N=0.4,
                    existing_F_off_pose_uncertain=True)
        prepare_attempted = True
        require(link.ask("followprepare") == "OK S5 preparing output_allowed=0",
                "R4 followprepare refused")
        ready_deadline = clock() + PREPARE_TIMEOUT_S
        while True:
            mode = s5_status(link)
            require(mode.get("phase") != "8", "R4 fault while preparing")
            pose_held(link, reference["tick"])
            if mode.get("phase") == "3":
                require(mode.get("gate") == "5" and mode.get("zero_el") == "1"
                        and mode.get("output_allowed") == "0"
                        and mode.get("q_el_mrad", "na").lstrip("-").isdecimal()
                        and abs(int(mode["q_el_mrad"])) <= 35,
                        "R4 READY lacks local elbow zero or disabled output")
                break
            require(clock() < ready_deadline, "R4 READY timed out")
            sleep(POLL_S)

        start_attempts = 1  # Never retry a lost reply.
        reply = link.ask("followstart", timeout_s=0.15)
        match = re.fullmatch(
            r"OK S5 started axes=2 expires_in_ms=([0-9]+); support arm before stop",
            reply)
        require(match is not None and int(match.group(1)) >= 4000,
                "R4 start reply or session budget invalid")
        started = True
        started_at = clock()
        target_deadline = started_at + TARGET_TIMEOUT_S
        while True:
            mode = s5_status(link, timeout_s=0.10)
            sample = pose_held(link, reference["tick"])
            require(mode.get("phase") == "4" and mode.get("output_allowed") == "1"
                    and mode.get("zero_el") == "1" and mode.get("stop_failed") == "0",
                    "R4 left RUNNING before force-reading cue")
            target = mode.get("cmd_el_mA", "na")
            require(target.lstrip("-").isdecimal(), "R4 elbow target unavailable")
            if int(target) != 0:
                beep_record = {"monotonic_s": clock(), "status": mode,
                               "feedback": sample, "cmd_el_mA": int(target)}
                link.record(direction="marker", name="F_ON_BEEP_TARGET_NONZERO",
                            **beep_record)
                print("BEEP / F_on: read assisted force now; keep elbow and gauge fixed",
                      flush=True)
                beep(2200, 350)
                break
            require(clock() < target_deadline, "R4 current target stayed zero")
            sleep(POLL_S)

        require(clock() + READING_S + 0.7 < started_at + 5.0,
                "reading would overrun R4 five-second session")
        reading_deadline = clock() + READING_S
        while clock() < reading_deadline:
            mode = s5_status(link, timeout_s=0.10)
            sample = pose_held(link, reference["tick"])
            target = mode.get("cmd_el_mA", "na")
            require(mode.get("phase") == "4" and mode.get("output_allowed") == "1"
                    and mode.get("stop_failed") == "0"
                    and target.lstrip("-").isdecimal() and int(target) != 0,
                    "assisted force-reading window lost active target")
            link.record(direction="sample", name="F_ON_WINDOW",
                        status=mode, feedback=sample)
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
                error = (error + "; " if error else "") + f"stop: {type(exc).__name__}: {exc}"
            try:
                deadline = clock() + STOP_TIMEOUT_S
                while True:
                    final = s5_status(link)
                    if final.get("phase") in ("7", "8"):
                        break
                    require(clock() < deadline, "R4 stop observation timed out")
                    sleep(POLL_S)
                require(final.get("phase") == "7" and final.get("gate") == "0"
                        and final.get("output_allowed") == "0"
                        and final.get("stop_failed") == "0"
                        and (start_attempts == 0 or final.get("stop") == "1"),
                        "R4 STOPPED or stop-frame acceptance unconfirmed")
            except Exception as exc:
                error = (error + "; " if error else "") + f"post-stop: {type(exc).__name__}: {exc}"
        for command in ("elbowfeedback", "canstatus"):
            try:
                link.ask(command)
            except Exception as exc:
                error = (error + "; " if error else "") + f"{command}: {type(exc).__name__}: {exc}"
        link.record(direction="result", prepare_attempted=prepare_attempted,
                    start_attempts=start_attempts, started=started,
                    F_on_beep=beep_record, stop_reply=stop_reply,
                    final_phase=final.get("phase"), error=error)
    return {"prepare_attempted": prepare_attempted, "start_attempts": start_attempts,
            "started": started, "F_on_beep": beep_record,
            "stop_reply": stop_reply, "final_phase": final.get("phase"),
            "error": error}


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
            age = 200 if self.failure == "stale" and self.feedback_reads > 2 else 10
            tick = 1000 + (60 if self.failure == "drift" and self.phase == "running"
                           else 0)
            return ("OK bus=elbow motor_id=1 seen=1 valid=1 online=1 "
                    f"angle_ticks={tick} rpm=0 current_raw=120 "
                    f"received_ms={self.feedback_reads * 10} age_ms={age} "
                    f"output_allowed={int(self.phase == 'running')}")
        if command == "followprepare":
            if self.failure == "prepare":
                return "ERR blocked"
            self.phase = "ready"
            return "OK S5 preparing output_allowed=0"
        if command == "followstart":
            if self.failure == "start":
                return "ERR blocked"
            self.phase = "running"
            return "OK S5 started axes=2 expires_in_ms=5000; support arm before stop"
        if command == "followstop":
            before = self.phase
            self.phase = "stopping"
            return ("OK S5 ramping_down; support arm" if before == "running"
                    else "OK S5 session_closed; support arm")
        if command == "followstatus":
            if self.phase == "stopping":
                self.stop_reads += 1
                if self.stop_reads >= 2:
                    self.phase = "stopped"
            if self.phase == "running" and self.failure == "fault":
                self.phase = "fault"
            phase, gate, allowed, stop, zero = {
                "idle": (1, 0, 0, 0, 0), "ready": (3, 5, 0, 0, 1),
                "running": (4, 2, 1, 0, 1), "stopping": (5, 6, 0, 0, 1),
                "stopped": (7, 0, 0, int(self.failure not in ("prepare", "start")), 0),
                "fault": (8, 3, 0, 0, 0)
            }[self.phase]
            target = 0 if self.failure == "zero_target" else 1000
            return (f"OK s5=1 axes=2 phase={phase} gate={gate} "
                    f"q_el_mrad=0 cmd_el_mA={target} zero_el={zero} "
                    f"output_allowed={allowed} stop={stop} stop_failed=0")
        raise AssertionError(command)


def offline_check():
    for failure in (None, "prepare", "start", "zero_target", "drift", "stale", "fault"):
        link = FakeLink(failure)
        clock = FakeClock()
        cues = []
        result = run_once(link, lambda hz, ms: cues.append((hz, ms)),
                          clock.clock, clock.sleep)
        require(len(cues) == (1 if failure is None else 0),
                f"{failure}: beep must occur only for stable nonzero active target")
        require(link.commands.count("followstart") <= 1
                and link.commands.count("followstop") <= 1,
                f"{failure}: repeated motion command")
        require(bool(result["error"]) == (failure is not None),
                f"{failure}: expected result")
        if failure is None:
            require(result["final_phase"] == "7"
                    and "F_ON_BEEP_TARGET_NONZERO" in link.journal.getvalue(),
                    "success: active cue or stop not logged")
        print(f"offline {failure or 'success'}: beep={len(cues)} "
              f"start={link.commands.count('followstart')} "
              f"stop={link.commands.count('followstop')} "
              f"result={'aborted' if result['error'] else 'stopped'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true", help="fake VCP only")
    parser.add_argument("--run", action="store_true", help="request one R4 FOLLOW")
    parser.add_argument("--port", help="explicit ST-LINK VCP port")
    parser.add_argument("--output-dir", type=Path, help="H: raw JSONL directory")
    parser.add_argument("--board-bin-sha256", help="independent board readback hash")
    args = parser.parse_args()
    if args.offline_check:
        offline_check()
        return 0
    require(args.run and args.port and args.output_dir and args.board_bin_sha256,
            "live use requires --run, --port, --output-dir and --board-bin-sha256")
    require(re.fullmatch(r"[0-9a-fA-F]{64}", args.board_bin_sha256) is not None,
            "board-bin-sha256 must be 64 hex digits")
    output = args.output_dir.resolve()
    require(output.drive.upper() == "H:", "output-dir must resolve to H: drive")
    try:
        import winsound
    except ImportError as exc:
        raise TrialError("Windows winsound is required for the assisted cue") from exc
    output.mkdir(parents=True, exist_ok=True)
    path = output / ("s5-elbow-F-on-" + datetime.now().strftime("%Y%m%d-%H%M%S")
                     + ".jsonl")
    with path.open("x", encoding="utf-8", newline="\n") as journal:
        port = open_vcp(args.port)
        try:
            link = Link(port, journal)
            link.record(direction="metadata", board_bin_sha256=args.board_bin_sha256.lower(),
                        hash_source="operator_supplied_independent_readback",
                        physical_elbow_angle_deg="operator_held_about_20",
                        existing_F_off_approx_N=0.4,
                        existing_F_off_pose_uncertain=True, reading="F_on_only")
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
