#!/usr/bin/env python3
"""One S5 elbow FOLLOW window; run only with a confirmed R4 isolation image."""

import argparse
import io
import json
import re
import sys
import time
from datetime import datetime
from pathlib import Path

from elbow_once import Link, TrialError, fields, open_vcp, require


def s5_status(link, timeout_s=None):
    status = fields(link.ask("followstatus", timeout_s=timeout_s))
    require(status.get("s5") == "1" and status.get("axes") == "2",
            "S5 elbow-only profile is not selected")
    return status


def preflight(link):
    require(link.ask("ping") == "OK pong", "unexpected ping reply")
    status = fields(link.ask("status"))
    require(status.get("stage") == "S2_CAN_RX" and status.get("output_allowed") == "0",
            "system is not initially output-off")
    can = fields(link.ask("canstatus"))
    require(can.get("can2_started") == "1" and can.get("can2_baud") == "1000000"
            and can.get("can2_start_error") == "0", "CAN2 is not started at 1 Mbps")
    elbow = fields(link.ask("elbowfeedback"))
    age = elbow.get("age_ms", "")
    require(elbow.get("bus") == "elbow" and elbow.get("motor_id") == "1"
            and elbow.get("seen") == "1" and elbow.get("valid") == "1"
            and elbow.get("online") == "1" and age.isdecimal() and int(age) <= 100,
            "C620 feedback is missing or older than 100 ms")
    mode = s5_status(link)
    require(mode.get("phase") == "1" and mode.get("gate") == "0"
            and mode.get("output_allowed") == "0" and mode.get("stop_failed") == "0",
            "S5 elbow session is not safely idle")


def run_once(link, observe_ms, manual_start_ms, manual_end_ms, ramp_reserve_ms,
             prepare_timeout_ms=4000, stop_timeout_ms=3000, poll_ms=100,
             clock=time.monotonic, sleep=time.sleep):
    error = None
    started = False
    start_count = 0
    manual_samples = 0
    active_samples = 0
    nonzero_target_samples = 0
    last_status = {}
    stop_reply = None
    try:
        preflight(link)
        require(link.ask("followprepare") == "OK S5 preparing output_allowed=0",
                "S5 prepare was not accepted")
        ready_deadline = clock() + prepare_timeout_ms / 1000.0
        while True:
            mode = s5_status(link)
            require(mode.get("phase") != "8", "S5 fault during preparation")
            if mode.get("phase") == "3":
                require(mode.get("gate") == "5" and mode.get("zero_el") == "1"
                        and mode.get("output_allowed") == "0"
                        and mode.get("q_el_mrad", "na").lstrip("-").isdecimal(),
                        "READY lacks elbow zero, angle, or disabled output")
                break
            require(clock() < ready_deadline, "S5 READY timeout")
            sleep(poll_ms / 1000.0)
        start_count = 1  # Set before sending; a lost reply must never cause a retry.
        started_reply = link.ask("followstart", timeout_s=0.15)
        match = re.fullmatch(
            r"OK S5 started axes=2 expires_in_ms=([0-9]+); support arm before stop",
            started_reply)
        require(match is not None, "S5 start reply has wrong axes or format")
        started = True
        remaining = int(match.group(1))
        require(observe_ms + ramp_reserve_ms + 250 < remaining,
                "session budget cannot fit observation plus ramp reserve")
        start_at = clock()
        begin_marked = end_marked = False
        while True:
            elapsed_ms = int((clock() - start_at) * 1000)
            if not begin_marked and elapsed_ms >= manual_start_ms:
                begin_marked = True
                link.record(direction="marker", name="planned_manual_change_begin",
                            elapsed_ms=elapsed_ms)
                print("Manual small-pose-change window START (stay in confirmed range)", flush=True)
            if not end_marked and elapsed_ms >= manual_end_ms:
                end_marked = True
                link.record(direction="marker", name="planned_manual_change_end",
                            elapsed_ms=elapsed_ms)
                print("Manual small-pose-change window END; continue support", flush=True)
            if elapsed_ms >= observe_ms:
                break
            mode = s5_status(link, timeout_s=0.10)
            require(mode.get("phase") == "4" and mode.get("output_allowed") == "1"
                    and mode.get("stop_failed") == "0", "S5 left RUNNING unexpectedly")
            require(mode.get("q_el_mrad", "na").lstrip("-").isdecimal(),
                    "S5 elbow angle is not available")
            active_samples += 1
            if begin_marked and not end_marked:
                manual_samples += 1
            target = mode.get("cmd_el_mA", "na")
            if target.lstrip("-").isdecimal() and int(target) != 0:
                nonzero_target_samples += 1
            remaining_s = observe_ms / 1000.0 - (clock() - start_at)
            if remaining_s > 0.0:
                sleep(min(poll_ms / 1000.0, remaining_s))
        require(begin_marked and end_marked, "planned manual window was not fully observed")
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            stop_reply = link.ask("followstop", timeout_s=0.15)
            require(stop_reply in ("OK S5 ramping_down; support arm",
                                   "OK S5 session_closed; support arm"),
                    "S5 stop was not accepted")
        except Exception as exc:
            error = (error + "; " if error else "") + f"followstop: {type(exc).__name__}: {exc}"
        try:
            stop_deadline = clock() + stop_timeout_ms / 1000.0
            while True:
                last_status = s5_status(link)
                if last_status.get("phase") in ("7", "8"):
                    break
                require(clock() < stop_deadline, "S5 stop observation timeout")
                sleep(poll_ms / 1000.0)
            require(last_status.get("phase") == "7" and last_status.get("gate") == "0"
                    and last_status.get("output_allowed") == "0"
                    and last_status.get("stop") == "1"
                    and last_status.get("stop_failed") == "0",
                    "S5 STOPPED and stop frame acceptance not confirmed")
        except Exception as exc:
            error = (error + "; " if error else "") + f"post-stop: {type(exc).__name__}: {exc}"
        for command in ("elbowfeedback", "canstatus"):
            try:
                link.ask(command)
            except Exception as exc:
                error = (error + "; " if error else "") + f"{command}: {type(exc).__name__}: {exc}"
    return {"started": started, "start_attempts": start_count, "stop_reply": stop_reply,
            "active_samples": active_samples, "manual_window_samples": manual_samples,
            "nonzero_target_samples": nonzero_target_samples,
            "final_phase": last_status.get("phase"), "error": error}


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakePort:
    """Command-response fake; no pyserial import and no real port."""
    def __init__(self, failure=None):
        self.failure = failure
        self.commands = []
        self.pending = ""
        self.mode = "idle"
        self.stop_reads = 0

    def write(self, raw):
        self.pending = raw.decode("ascii").strip()
        self.commands.append(self.pending)
        return len(raw)

    def read_until(self, _terminator, _limit):
        cmd = self.pending
        if cmd == "ping":
            reply = "OK pong"
        elif cmd == "status":
            reply = "OK stage=S2_CAN_RX output_allowed=0"
        elif cmd == "canstatus":
            reply = "OK can2_started=1 can2_baud=1000000 can2_start_error=0"
        elif cmd == "elbowfeedback":
            reply = "OK bus=elbow motor_id=1 seen=1 valid=1 online=1 age_ms=10"
        elif cmd == "followprepare":
            reply = "ERR blocked" if self.failure == "prepare" else "OK S5 preparing output_allowed=0"
            if self.failure != "prepare":
                self.mode = "ready"
        elif cmd == "followstart":
            reply = ("ERR blocked" if self.failure == "start"
                     else "OK S5 started axes=2 expires_in_ms=5000; support arm before stop")
            if self.failure != "start":
                self.mode = "running"
        elif cmd == "followstop":
            reply = ("ERR fault" if self.mode == "fault"
                     else "OK S5 ramping_down; support arm" if self.mode == "running"
                     else "OK S5 session_closed; support arm")
            if self.mode != "fault":
                self.mode = "stopping"
        elif cmd == "followstatus":
            if self.mode == "running" and self.failure == "running":
                self.mode = "fault"
            if self.mode == "stopping":
                self.stop_reads += 1
                if self.stop_reads >= 2:
                    self.mode = "stopped"
            phase, gate, allowed, stop, zero = {
                "idle": (1, 0, 0, 0, 0), "ready": (3, 5, 0, 0, 1),
                "running": (4, 2, 1, 0, 1), "stopping": (5, 6, 0, 0, 1),
                "stopped": (7, 0, 0, 1, 0), "fault": (8, 3, 0, 1, 0)
            }[self.mode]
            reply = (f"OK s5=1 axes=2 phase={phase} gate={gate} reason=0 safety=0 "
                     f"q_sh_mrad=na q_el_mrad=100 cmd_sh_mNm=na cmd_el_mA=200 "
                     f"seq_sh=0 seq_el=10 zero_sh=0 zero_el={zero} remaining_ms=4000 "
                     f"output_allowed={allowed} stop={stop} stop_failed=0")
        else:
            raise AssertionError(f"unexpected fake command: {cmd}")
        return (reply + "\r\n").encode("ascii")


def offline_check():
    for failure in (None, "prepare", "start", "running"):
        fake = FakePort(failure)
        clock = FakeClock()
        journal = io.StringIO()
        result = run_once(Link(fake, journal), 400, 100, 300, 1000,
                          clock=clock.clock, sleep=clock.sleep)
        starts = fake.commands.count("followstart")
        stops = fake.commands.count("followstop")
        require(starts <= 1 and stops == 1, f"{failure}: start/stop count")
        require(bool(result["error"]) == (failure is not None), f"{failure}: result")
        require((starts == 1) == (failure != "prepare"), f"{failure}: start path")
        if failure is None:
            require("planned_manual_change_begin" in journal.getvalue()
                    and "planned_manual_change_end" in journal.getvalue(),
                    "manual window logging")
        print(f"offline {failure or 'success'}: start={starts}, stop={stops}, "
              f"result={'stopped' if not result['error'] else 'aborted'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true", help="fake port only")
    parser.add_argument("--run", action="store_true", help="explicitly request one live R4 window")
    parser.add_argument("--port", help="explicit VCP; never scanned")
    parser.add_argument("--output-dir", type=Path, help="H: raw JSONL directory")
    parser.add_argument("--board-bin-sha256", help="operator-confirmed independent readback hash")
    parser.add_argument("--observe-ms", type=int, help="time from start to stop request")
    parser.add_argument("--manual-start-ms", type=int, help="planned small posture change begins")
    parser.add_argument("--manual-end-ms", type=int, help="planned small posture change ends")
    parser.add_argument("--ramp-reserve-ms", type=int, help="R4 profile ramp_max_ms")
    parser.add_argument("--prepare-timeout-ms", type=int, default=4000)
    parser.add_argument("--stop-timeout-ms", type=int, default=3000)
    args = parser.parse_args()
    if args.offline_check:
        offline_check()
        return 0
    require(args.run and args.port and args.output_dir and args.board_bin_sha256,
            "live mode requires --run, --port, --output-dir and --board-bin-sha256")
    require(re.fullmatch(r"[0-9a-fA-F]{64}", args.board_bin_sha256) is not None,
            "board-bin-sha256 must be a 64-digit hash")
    timing = (args.observe_ms, args.manual_start_ms, args.manual_end_ms,
              args.ramp_reserve_ms)
    require(all(value is not None for value in timing), "live timing arguments are required")
    require(0 < args.manual_start_ms < args.manual_end_ms < args.observe_ms
            and args.ramp_reserve_ms > 0 and args.prepare_timeout_ms > 0
            and args.stop_timeout_ms > 0, "invalid observation or manual window timing")
    output = args.output_dir.resolve()
    require(output.drive.upper() == "H:", "output-dir must resolve to H: drive")
    output.mkdir(parents=True, exist_ok=True)
    path = output / ("s5-r4-elbow-follow-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".jsonl")
    with path.open("x", encoding="utf-8", newline="\n") as journal:
        link = None
        port = open_vcp(args.port)
        try:
            link = Link(port, journal)
            link.record(direction="metadata", board_bin_sha256=args.board_bin_sha256.lower(),
                        hash_source="operator_supplied_independent_readback",
                        observe_ms=args.observe_ms, manual_start_ms=args.manual_start_ms,
                        manual_end_ms=args.manual_end_ms, ramp_reserve_ms=args.ramp_reserve_ms)
            result = run_once(link, *timing, prepare_timeout_ms=args.prepare_timeout_ms,
                              stop_timeout_ms=args.stop_timeout_ms)
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
