#!/usr/bin/env python3
"""One 0-to-20-degree elbow lift and same-pose force comparison with S5 R4.

The operator places the elbow fully extended before launch, holds the shoulder
and gauge point fixed, reads F_off at the first beep and F_on at the second.
The program never interprets gauge readings as motor torque.
"""

import argparse
import io
import json
import re
import sys
import time
from datetime import datetime
from pathlib import Path

from elbow_once import Link, TrialError, fields, open_vcp, require
from follow_once import preflight


TICKS_PER_TURN = 8192  # C620 single-turn encoder; elbow assembly reported 1:1.
TARGET_DEG = 20.0
CUE_MIN_DEG = 19.5
CUE_MAX_DEG = 22.0
ABORT_DEG = 22.5
POSE_DRIFT_DEG = 2.0
LIFT_TIMEOUT_S = 120.0
BASELINE_HOLD_S = 4.0
ASSIST_HOLD_S = 2.2
PREPARE_TIMEOUT_S = 2.3  # Firmware R4 prepare timeout is 2.5 s.
NONZERO_TIMEOUT_S = 0.8
STOP_TIMEOUT_S = 2.0
POLL_S = 0.08


def shortest_ticks(now, zero):
    return (now - zero + TICKS_PER_TURN // 2) % TICKS_PER_TURN - TICKS_PER_TURN // 2


def angle_deg(ticks, zero):
    return abs(shortest_ticks(ticks, zero)) * 360.0 / TICKS_PER_TURN


def s5_status(link, timeout_s=None):
    # The shared `fields()` helper accepts lowercase keys only, while the
    # firmware deliberately names this unit-bearing field `cmd_el_mA`.
    reply = link.ask("followstatus", timeout_s=timeout_s)
    result = dict(re.findall(r"(?:^| )([A-Za-z][A-Za-z0-9_]*)=([^ ]+)", reply))
    require(result.get("s5") == "1" and result.get("axes") == "2",
            "S5 elbow-only profile is not selected")
    return result


def elbow_sample(link):
    sample = fields(link.ask("elbowfeedback"))
    tick = sample.get("angle_ticks", "")
    age = sample.get("age_ms", "")
    received = sample.get("received_ms", "")
    require(sample.get("bus") == "elbow" and sample.get("motor_id") == "1"
            and sample.get("seen") == "1" and sample.get("valid") == "1"
            and sample.get("online") == "1" and sample.get("output_allowed") in ("0", "1")
            and tick.isdecimal() and 0 <= int(tick) < TICKS_PER_TURN
            and age.isdecimal() and int(age) <= 100
            and received.isdecimal(), "C620 elbow feedback invalid or stale")
    raw_current = sample.get("current_raw", "")
    return {"tick": int(tick), "rpm": int(sample["rpm"]),
            "received_ms": int(received), "age_ms": int(age),
            "output_allowed": int(sample["output_allowed"]),
            "current_raw": int(raw_current) if raw_current.lstrip("-").isdecimal()
            else None}


def quiet_pose(sample, zero, cue_deg):
    angle = angle_deg(sample["tick"], zero)
    require(abs(angle - cue_deg) <= POSE_DRIFT_DEG and abs(sample["rpm"]) <= 2,
            f"elbow moved from force-reading pose: {angle:.2f} deg, "
            f"{sample['rpm']} rpm")
    return angle


def run_once(link, beep, clock=time.monotonic, sleep=time.sleep):
    """A fixed schedule, so the operator need not answer chat during output."""
    error = None
    started = False
    first_cue = second_cue = None
    zero_tick = None
    start_count = 0
    stopped = {}
    stop_reply = None
    try:
        preflight(link)
        # The physical fully extended pose is supplied by the operator; these
        # raw ticks are a host-only reference and never alter firmware zero.
        zero_samples = []
        for _ in range(3):
            sample = elbow_sample(link)
            require(sample["output_allowed"] == 0 and abs(sample["rpm"]) <= 2,
                    "elbow must be still and output-off at physical zero")
            zero_samples.append(sample)
            sleep(POLL_S)
        zero_tick = zero_samples[0]["tick"]
        require(all(abs(shortest_ticks(s["tick"], zero_tick)) <= 3
                    for s in zero_samples), "physical zero moved during reference capture")
        link.record(direction="marker", name="physical_extended_zero",
                    angle_ticks=zero_tick, samples=zero_samples,
                    source="operator_confirmed_elbow_extended_1_to_1")
        link.record(direction="marker", name="START_LIFT_TWO_LOW_BEEPS",
                    angle_ticks=zero_tick, monotonic_s=clock())
        beep(750, 120)
        sleep(0.12)
        beep(750, 120)
        print("ZERO CAPTURED: slowly flex elbow to 20 degrees; stop at first beep",
              flush=True)

        lift_deadline = clock() + LIFT_TIMEOUT_S
        while True:
            sample = elbow_sample(link)
            require(sample["output_allowed"] == 0,
                    "output became enabled before baseline reading")
            angle = angle_deg(sample["tick"], zero_tick)
            require(angle <= ABORT_DEG,
                    f"target was passed before beep: {angle:.2f} deg")
            if CUE_MIN_DEG <= angle <= CUE_MAX_DEG:
                first_cue = {"angle_deg": round(angle, 3), "sample": sample,
                             "monotonic_s": clock()}
                link.record(direction="marker", name="F_OFF_BEEP_20_DEG", **first_cue)
                print(f"BEEP 1 / F_off: {angle:.2f} deg; hold this pose and read gauge",
                      flush=True)
                beep(1800, 350)
                sleep(0.25)  # Allow the hand to settle after the stop cue.
                break
            require(clock() < lift_deadline, "20-degree lift timed out")
            sleep(POLL_S)

        # This output-off interval belongs to the first force reading. The
        # subsequent firmware session zero is captured only after the hold.
        baseline_deadline = clock() + BASELINE_HOLD_S
        while clock() < baseline_deadline:
            sample = elbow_sample(link)
            require(sample["output_allowed"] == 0,
                    "output became enabled in no-assist reading")
            quiet_pose(sample, zero_tick, first_cue["angle_deg"])
            sleep(POLL_S)
        link.record(direction="marker", name="F_OFF_WINDOW_END",
                    angle_deg=quiet_pose(elbow_sample(link), zero_tick,
                                         first_cue["angle_deg"]))

        require(link.ask("followprepare") == "OK S5 preparing output_allowed=0",
                "R4 followprepare refused")
        ready_deadline = clock() + PREPARE_TIMEOUT_S
        while True:
            mode = s5_status(link)
            require(mode.get("phase") != "8", "R4 fault during preparation")
            sample = elbow_sample(link)
            quiet_pose(sample, zero_tick, first_cue["angle_deg"])
            if mode.get("phase") == "3":
                require(mode.get("gate") == "5" and mode.get("zero_el") == "1"
                        and mode.get("output_allowed") == "0"
                        and mode.get("q_el_mrad", "na").lstrip("-").isdecimal()
                        and abs(int(mode["q_el_mrad"])) <= 35,
                        "R4 READY lacks still elbow session zero")
                break
            require(clock() < ready_deadline, "R4 READY timeout")
            sleep(POLL_S)

        start_count = 1  # A lost reply must never trigger a second start.
        reply = link.ask("followstart", timeout_s=0.15)
        match = re.fullmatch(
            r"OK S5 started axes=2 expires_in_ms=([0-9]+); support arm before stop",
            reply)
        require(match is not None and int(match.group(1)) >= 4000,
                "R4 start reply or five-second session budget invalid")
        started = True
        started_at = clock()
        nonzero_deadline = started_at + NONZERO_TIMEOUT_S
        while True:
            mode = s5_status(link, timeout_s=0.10)
            sample = elbow_sample(link)
            angle = quiet_pose(sample, zero_tick, first_cue["angle_deg"])
            require(mode.get("phase") == "4" and mode.get("output_allowed") == "1"
                    and mode.get("stop_failed") == "0" and mode.get("zero_el") == "1",
                    "R4 left active output before assisted reading")
            target = mode.get("cmd_el_mA", "na")
            require(target.lstrip("-").isdecimal(), "R4 current target unavailable")
            if int(target) != 0:
                second_cue = {"angle_deg": round(angle, 3), "sample": sample,
                              "cmd_el_mA": int(target), "monotonic_s": clock()}
                link.record(direction="marker", name="F_ON_BEEP_TARGET_NONZERO",
                            **second_cue)
                print(f"BEEP 2 / F_on: {angle:.2f} deg, command target "
                      f"{target} mA; read gauge now", flush=True)
                beep(2200, 350)
                break
            require(clock() < nonzero_deadline,
                    "R4 current target did not become nonzero")
            sleep(POLL_S)

        assisted_deadline = clock() + ASSIST_HOLD_S
        require(assisted_deadline + 0.7 < started_at + 5.0,
                "assisted reading would overrun R4 session")
        while clock() < assisted_deadline:
            mode = s5_status(link, timeout_s=0.10)
            sample = elbow_sample(link)
            angle = quiet_pose(sample, zero_tick, first_cue["angle_deg"])
            target = mode.get("cmd_el_mA", "na")
            require(mode.get("phase") == "4" and mode.get("output_allowed") == "1"
                    and mode.get("stop_failed") == "0" and target.lstrip("-").isdecimal()
                    and int(target) != 0, "assisted reading lost R4 active target")
            link.record(direction="sample", name="F_ON_WINDOW",
                        angle_deg=round(angle, 3), cmd_el_mA=int(target),
                        feedback=sample)
            sleep(POLL_S)
    except (Exception, KeyboardInterrupt) as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            stop_reply = link.ask("followstop", timeout_s=0.15)
            require(stop_reply in ("OK S5 ramping_down; support arm",
                                   "OK S5 session_closed; support arm"),
                    "R4 followstop not accepted")
        except Exception as exc:
            error = (error + "; " if error else "") + f"stop: {type(exc).__name__}: {exc}"
        try:
            deadline = clock() + STOP_TIMEOUT_S
            while True:
                stopped = s5_status(link)
                if stopped.get("phase") in ("7", "8"):
                    break
                require(clock() < deadline, "R4 stop observation timeout")
                sleep(POLL_S)
            require(stopped.get("phase") == "7" and stopped.get("gate") == "0"
                    and stopped.get("output_allowed") == "0" and stopped.get("stop") == "1"
                    and stopped.get("stop_failed") == "0",
                    "R4 STOPPED or zero-frame acceptance unconfirmed")
        except Exception as exc:
            error = (error + "; " if error else "") + f"post-stop: {type(exc).__name__}: {exc}"
        try:
            link.ask("elbowfeedback")
            link.ask("canstatus")
        except Exception as exc:
            error = (error + "; " if error else "") + f"final-feedback: {type(exc).__name__}: {exc}"
        link.record(direction="result", started=started, start_count=start_count,
                    first_cue=first_cue, second_cue=second_cue,
                    stop_reply=stop_reply, final_phase=stopped.get("phase"), error=error)
    return {"zero_tick": zero_tick, "first_cue": first_cue,
            "second_cue": second_cue, "started": started,
            "start_count": start_count, "stop_reply": stop_reply,
            "final_phase": stopped.get("phase"), "error": error}


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeLink:
    def __init__(self, clock, failure=None):
        self.clock = clock
        self.failure = failure
        self.journal = io.StringIO()
        self.commands = []
        self.feedback_count = 0
        self.phase = "idle"
        self.stop_reads = 0

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
            self.feedback_count += 1
            count = self.feedback_count
            delta = 0 if count <= 4 else 150 if count == 5 else 300 if count == 6 else 455
            if self.failure == "overshoot" and count >= 7:
                delta = 600
            if self.failure == "drift" and count >= 8:
                delta = 510
            age = 200 if self.failure == "stale" and count == 6 else 10
            return ("OK bus=elbow motor_id=1 seen=1 valid=1 online=1 "
                    f"angle_ticks={(7900 + delta) % TICKS_PER_TURN} rpm=0 "
                    f"current_raw={100 if self.phase == 'running' else 0} "
                    f"received_ms={count * 10} age_ms={age} "
                    f"output_allowed={int(self.phase == 'running')}")
        if command == "followprepare":
            self.phase = "ready"
            return "OK S5 preparing output_allowed=0"
        if command == "followstart":
            if self.failure == "start":
                return "ERR blocked"
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
            phase, gate, allowed, stop, zero = {
                "idle": (1, 0, 0, 0, 0), "ready": (3, 5, 0, 0, 1),
                "running": (4, 2, 1, 0, 1), "stopping": (5, 6, 0, 0, 1),
                "stopped": (7, 0, 0, 1, 0)
            }[self.phase]
            target = 0 if self.failure == "zero_target" else 1000
            return (f"OK s5=1 axes=2 phase={phase} gate={gate} "
                    f"q_el_mrad=0 cmd_el_mA={target} zero_el={zero} "
                    f"output_allowed={allowed} stop={stop} stop_failed=0")
        raise AssertionError(f"unexpected fake command {command}")


def offline_check():
    require(shortest_ticks(50, 8100) == 142, "encoder wrap positive")
    require(shortest_ticks(8100, 50) == -142, "encoder wrap negative")
    for failure in (None, "stale", "overshoot", "drift", "start", "zero_target"):
        clock = FakeClock()
        link = FakeLink(clock, failure)
        cues = []
        result = run_once(link, lambda hz, ms: cues.append((hz, ms)),
                          clock.clock, clock.sleep)
        require(link.commands.count("followstart") <= 1
                and link.commands.count("followstop") == 1,
                f"{failure}: start/stop count")
        require(bool(result["error"]) == (failure is not None),
                f"{failure}: expected failure result")
        require(len(cues) == (4 if failure is None else
                             3 if failure in ("drift", "start", "zero_target") else 2),
                f"{failure}: sound cue count")
        if failure is None:
            require(result["final_phase"] == "7"
                    and "F_OFF_BEEP_20_DEG" in link.journal.getvalue()
                    and "F_ON_BEEP_TARGET_NONZERO" in link.journal.getvalue(),
                    "success: cues and stop not recorded")
        print(f"offline {failure or 'success'}: cues={len(cues)}, "
              f"starts={link.commands.count('followstart')}, "
              f"stops={link.commands.count('followstop')}, "
              f"result={'aborted' if result['error'] else 'stopped'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true", help="fake VCP only")
    parser.add_argument("--run", action="store_true", help="request one live R4 window")
    parser.add_argument("--port", help="explicit ST-LINK VCP port; never scanned")
    parser.add_argument("--output-dir", type=Path, help="H: directory for raw JSONL")
    parser.add_argument("--board-bin-sha256", help="independent board readback SHA256")
    args = parser.parse_args()
    if args.offline_check:
        offline_check()
        return 0
    require(args.run and args.port and args.output_dir and args.board_bin_sha256,
            "live use requires --run, --port, --output-dir and --board-bin-sha256")
    require(re.fullmatch(r"[0-9a-fA-F]{64}", args.board_bin_sha256) is not None,
            "board-bin-sha256 must have 64 hex digits")
    output = args.output_dir.resolve()
    require(output.drive.upper() == "H:", "output-dir must resolve to H: drive")
    output.mkdir(parents=True, exist_ok=True)
    path = output / ("s5-elbow-force-" + datetime.now().strftime("%Y%m%d-%H%M%S")
                     + ".jsonl")
    try:
        import winsound  # Windows speaker cue; failure must occur before opening VCP.
    except ImportError as exc:
        raise TrialError("Windows winsound is required for the 20-degree cue") from exc
    with path.open("x", encoding="utf-8", newline="\n") as journal:
        port = open_vcp(args.port)
        try:
            link = Link(port, journal)
            link.record(direction="metadata", board_bin_sha256=args.board_bin_sha256.lower(),
                        hash_source="operator_supplied_independent_readback",
                        target_deg=TARGET_DEG, force_unit="N",
                        operator_pose="elbow_extended_zero_shoulder_fixed",
                        elbow_gear_ratio="operator_reported_1_to_1",
                        first_reading="F_off", second_reading="F_on")
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
