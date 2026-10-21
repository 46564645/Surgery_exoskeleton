#!/usr/bin/env python3
"""One R5 dual-joint FOLLOW window; no flash, reset, or automatic retry."""

import argparse
import io
import json
import os
import re
import sys
import time
from datetime import datetime
from pathlib import Path

sys.dont_write_bytecode = True
from elbow_once import Link, TrialError, fields, open_vcp, require


def dual_status(link, timeout_s=None):
    reply = link.ask("followstatus", timeout_s=timeout_s)
    status = fields(reply)
    # The shared field parser accepts lowercase keys; these two firmware keys
    # contain uppercase unit letters and need exact extraction here.
    for key in ("cmd_sh_mNm", "cmd_el_mA"):
        match = re.search(r"(?:^| )" + key + r"=([^ ]+)", reply)
        if match is not None:
            status[key] = match.group(1)
    require(status.get("s5") == "1" and status.get("axes") == "3",
            "R5 dual-joint image is not selected")
    return status


def preflight(link):
    require(link.ask("ping") == "OK pong", "unexpected ping reply")
    require(fields(link.ask("status")).get("output_allowed") == "0",
            "output was already enabled")
    can = fields(link.ask("canstatus"))
    for bus, baud in (("can1", "500000"), ("can2", "1000000")):
        require(can.get(bus + "_started") == "1"
                and can.get(bus + "_baud") == baud
                and can.get(bus + "_start_error") == "0",
                bus + " is not started at its configured bitrate")
    shoulder_bus = fields(link.ask("shoulder"))
    # Keep historical CAN errors in the transcript; live feedback checks recovery.
    require(shoulder_bus.get("bus_off") == "0", "CAN1 is bus-off")
    shoulder = fields(link.ask("shoulderfeedback"))
    require(shoulder.get("hb") == "1" and shoulder.get("state") == "1"
            and shoulder.get("error") == "00000000" and shoulder.get("fault") == "0",
            "shoulder heartbeat is not idle and fault-free")
    elbow = fields(link.ask("elbowfeedback"))
    age = elbow.get("age_ms", "")
    require(elbow.get("seen") == "1" and elbow.get("valid") == "1"
            and elbow.get("online") == "1" and age.isdecimal()
            and int(age) <= 150, "C620 feedback is unavailable or stale")
    mode = dual_status(link)
    require(mode.get("phase") == "1" and mode.get("output_allowed") == "0"
            and mode.get("stop_failed") == "0", "R5 session is not idle")


def run_once(link, observe_ms=4500, manual_cue_ms=1000,
             prepare_timeout_ms=5000, stop_timeout_ms=5000, poll_ms=100,
             clock=time.monotonic, sleep=time.sleep, announce=print,
             signal=lambda _name: None):
    """Prepare at the two physical origins, then start/stop at most once."""
    error = None
    prepared = False
    start_attempted = False
    stop_attempted = False
    stop_reply = None
    final = {}
    active_samples = 0
    manual_cued = False
    target_seen = {"shoulder": False, "elbow": False}
    elbow_missing_reads = 0

    def record_targets(status):
        for axis, field, unit in (("shoulder", "cmd_sh_mNm", "mNm"),
                                  ("elbow", "cmd_el_mA", "mA")):
            value = status.get(field, "na")
            if not target_seen[axis] and value.lstrip("-").isdecimal() and int(value) != 0:
                target_seen[axis] = True
                link.record(direction="marker", name="DUAL_TARGET_NONZERO",
                            axis=axis, target=value, unit=unit,
                            meaning="firmware_command_target_not_measured_assistance")

    try:
        preflight(link)
        prepared = True  # A lost prepare reply may still have changed board state.
        require(link.ask("followprepare") == "OK S5 preparing output_allowed=0",
                "R5 followprepare refused")
        deadline = clock() + prepare_timeout_ms / 1000.0
        while True:
            mode = dual_status(link)
            require(mode.get("phase") in ("2", "3"),
                    "R5 left preparation before both RAM zeroes")
            require(mode.get("output_allowed") == "0",
                    "output enabled during preparation")
            if mode.get("zero_sh") == "1" and mode.get("zero_el") == "1":
                break
            require(clock() < deadline, "R5 dual zero timeout")
            sleep(poll_ms / 1000.0)
        link.record(direction="marker", name="DUAL_ZERO_READY")
        announce("两轴零位已取得：请继续承托肩、肘并保持静止，等待自动启动。")
        signal("zero")
        while True:
            mode = dual_status(link)
            require(mode.get("phase") in ("2", "3"),
                    "R5 left preparation before READY")
            require(mode.get("output_allowed") == "0",
                    "output enabled before followstart")
            shoulder = mode.get("q_sh_mrad", "na")
            elbow = mode.get("q_el_mrad", "na")
            elbow_missing_reads = elbow_missing_reads + 1 if elbow == "na" else 0
            if elbow_missing_reads >= 3:
                raw_elbow = None
                raw_error = None
                try:
                    raw_elbow = link.ask("elbowfeedback", timeout_s=0.15)
                except (Exception, KeyboardInterrupt) as exc:
                    raw_error = f"{type(exc).__name__}: {exc}"
                link.record(direction="marker", name="DUAL_ELBOW_ANGLE_INVALID",
                            status=mode, elbowfeedback=raw_elbow, elbowfeedback_error=raw_error)
                raise TrialError("R5 elbow session angle remained invalid after both RAM zeroes")
            if mode.get("phase") == "3" and elbow != "na":
                require(shoulder.lstrip("-").isdecimal()
                        and elbow.lstrip("-").isdecimal()
                        and mode.get("gate") == "5"
                        and mode.get("zero_sh") == "1"
                        and mode.get("zero_el") == "1",
                        "READY lacks dual-joint angle or zero")
                break
            require(clock() < deadline, "R5 READY timeout")
            sleep(poll_ms / 1000.0)
        link.record(direction="marker", name="DUAL_READY", status=mode)
        announce("双轴已静止并进入 READY；继续承托，稍后将自动启动。")
        signal("hold")
        start_attempted = True  # Never send start again after an uncertain reply.
        try:
            started = link.ask("followstart", timeout_s=0.15)
        except TrialError as exc:
            if "ERR blocked" in str(exc):
                try:
                    rejected = dual_status(link)
                    link.record(direction="marker", name="DUAL_START_REFUSED",
                                status=rejected)
                except (Exception, KeyboardInterrupt) as capture_exc:
                    link.record(direction="marker", name="DUAL_START_REFUSED_CAPTURE_FAILED",
                                error=str(capture_exc))
            raise
        match = re.fullmatch(
            r"OK S5 started axes=3 expires_in_ms=([0-9]+); support arm before stop",
            started)
        require(match is not None, "R5 followstart reply mismatch")
        remaining_ms = int(match.group(1))
        require(observe_ms + 1500 < remaining_ms,
                "not enough session time for observation and stop")
        first_running = dual_status(link, timeout_s=0.15)
        require(first_running.get("phase") == "4"
                and first_running.get("output_allowed") == "1",
                "R5 did not enter dual RUNNING after start")
        record_targets(first_running)
        link.ask("elbowfeedback", timeout_s=0.15)
        active_samples += 1
        signal("started")
        announce("双关节 FOLLOW 已启动；继续承托肩、肘，稍后缓慢前抬肩一点。")
        started_at = clock()
        reported_s = 0
        while (clock() - started_at) * 1000.0 < observe_ms:
            mode = dual_status(link, timeout_s=0.15)
            require(mode.get("phase") == "4"
                    and mode.get("output_allowed") == "1"
                    and mode.get("stop_failed") == "0"
                    and mode.get("q_sh_mrad") != "na"
                    and mode.get("q_el_mrad") != "na",
                    "R5 left dual RUNNING or lost an angle")
            active_samples += 1
            if active_samples % 10 == 1:
                try:
                    link.ask("hx711", timeout_s=0.15)
                except (Exception, KeyboardInterrupt):
                    pass  # Auxiliary force telemetry never aborts the window.
            record_targets(mode)
            elapsed_ms = (clock() - started_at) * 1000.0
            if not manual_cued and elapsed_ms >= manual_cue_ms:
                manual_cued = True
                link.record(direction="marker", name="PLANNED_SMALL_POSE_CHANGE",
                            elapsed_ms=round(elapsed_ms))
                signal("move")
                announce("听到此音，现在请在承托下缓慢前抬肩一点，无需对准角度；肘部保持承托。")
            if int(elapsed_ms // 5000) > reported_s // 5:
                reported_s = int(elapsed_ms // 5000) * 5
                announce(f"已运行 {reported_s} s / {observe_ms // 1000} s")
            remaining_s = observe_ms / 1000.0 - (clock() - started_at)
            if remaining_s <= 0.001:
                break
            sleep(min(poll_ms / 1000.0, remaining_s))
    except (Exception, KeyboardInterrupt) as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if prepared:
            stop_attempted = True
            try:
                stop_reply = link.ask("followstop", timeout_s=0.15)
                require(stop_reply in ("OK S5 ramping_down; support arm",
                                       "OK S5 session_closed; support arm"),
                        "R5 followstop was not accepted")
            except (Exception, KeyboardInterrupt) as exc:
                error = (error + "; " if error else "") + \
                    f"followstop: {type(exc).__name__}: {exc}"
                announce("停止命令未确认送达：立即切断 24 V，并保持机构承托。")
            try:
                stop_deadline = clock() + stop_timeout_ms / 1000.0
                while True:
                    final = dual_status(link)
                    if final.get("phase") in ("7", "8"):
                        break
                    require(clock() < stop_deadline, "R5 stop observation timeout")
                    sleep(poll_ms / 1000.0)
                require(final.get("phase") == "7" and final.get("output_allowed") == "0"
                        and final.get("stop_failed") == "0"
                        and (not start_attempted or final.get("stop") == "1"),
                        "R5 normal STOPPED not confirmed")
            except (Exception, KeyboardInterrupt) as exc:
                error = (error + "; " if error else "") + \
                    f"post-stop: {type(exc).__name__}: {exc}"
            try:
                link.ask("elbowfeedback")
            except Exception:
                pass  # Stop remains the priority if a diagnostic query fails.
    if error:
        if prepared and not start_attempted and final.get("phase") == "7" \
                and final.get("output_allowed") == "0":
            announce("本次准备未获许可，联动输出未启动；请关闭 24 V，保持机构承托。")
        elif prepared:
            announce("本次未确认正常停止：保持机构承托，立即切断 24 V。")
        else:
            announce("启动前检查未通过；本脚本未发送联动准备或启动命令。")
    return {"start_attempted": start_attempted, "stop_attempted": stop_attempted,
            "stop_reply": stop_reply, "active_samples": active_samples,
            "shoulder_target_nonzero_seen": target_seen["shoulder"],
            "elbow_target_nonzero_seen": target_seen["elbow"],
            "target_evidence": "firmware command target, not measured assistance",
            "manual_cue_sent": manual_cued, "final_phase": final.get("phase"),
            "error": error}


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakePort:
    """In-memory serial board, used only by --offline-check."""
    def __init__(self, failure=None, shoulder_err="0", shoulder_history="0"):
        self.failure = failure
        self.shoulder_err = shoulder_err
        self.shoulder_history = shoulder_history
        self.commands = []
        self.pending = ""
        self.mode = "idle"
        self.prepare_reads = 0
        self.running_reads = 0
        self.stop_reads = 0
        self.shoulder_moved = False
        self.timeout = 1.0
        self.write_timeout = 1.0

    def write(self, data):
        self.pending = data.decode("ascii").strip()
        self.commands.append(self.pending)
        if self.pending == "followprepare":
            self.mode = "idle" if self.failure == "blocked_prepare" else "preparing"
        elif self.pending == "followstart":
            if self.failure != "blocked_start":
                self.mode = "running"
        elif self.pending == "followstop" and self.mode != "fault":
            self.mode = "stopping"
        return len(data)

    def read_until(self, _terminator, _limit):
        command = self.pending
        if command == "ping":
            reply = "OK pong"
        elif command == "status":
            reply = "OK stage=S2_CAN_RX output_allowed=0"
        elif command == "canstatus":
            reply = ("OK can1_started=1 can1_baud=500000 can1_start_error=0 "
                     "can2_started=1 can2_baud=1000000 can2_start_error=0")
        elif command == "shoulder":
            reply = (f"OK bus=shoulder drop={self.shoulder_history} "
                     f"invalid={self.shoulder_history} fov={self.shoulder_history} "
                     f"err={self.shoulder_err} bus_off=0 queued={self.shoulder_history}")
        elif command == "shoulderfeedback":
            reply = "OK shoulder hb=1 state=1 error=00000000 fault=0"
        elif command == "elbowfeedback":
            reply = ("OK bus=elbow motor_id=1 seen=1 valid=1 online=1 "
                     "angle_ticks=3000 rpm=0 received_ms=1234 age_ms=1")
        elif command == "followprepare":
            reply = "ERR blocked" if self.failure == "blocked_prepare" else \
                "OK S5 preparing output_allowed=0"
        elif command == "followstart":
            if self.failure == "blocked_start":
                reply = "ERR blocked"
            elif self.failure == "lost_start_reply":
                return b""
            else:
                reply = "OK S5 started axes=3 expires_in_ms=10000; support arm before stop"
        elif command == "followstop":
            reply = ("ERR fault" if self.mode == "fault" else
                     "OK S5 ramping_down; support arm")
        elif command == "followstatus":
            phase, gate, allowed, zero_sh, zero_el, q_sh, q_el = \
                "1", "0", "0", "0", "0", "na", "na"
            cmd_sh, cmd_el = "na", "na"
            if self.mode == "preparing":
                self.prepare_reads += 1
                phase = "2"
                if self.prepare_reads >= 2:
                    zero_sh = zero_el = "1"
                    q_sh = q_el = "0"
                if self.failure == "invalid_elbow" and self.prepare_reads >= 4:
                    q_el = "na"
                if self.failure != "invalid_elbow" and self.prepare_reads >= 5:
                    phase, gate = "3", "5"
                    if self.failure == "invalid_elbow_ready":
                        q_el = "na"
            elif self.mode == "running":
                self.running_reads += 1
                phase, gate, allowed = "4", "6", "1"
                zero_sh = zero_el = "1"
                q_sh, q_el = ("175" if self.shoulder_moved else "0"), "0"
                cmd_sh, cmd_el = ("20" if self.shoulder_moved else "0"), "-2000"
                if self.failure == "fault_running" and self.running_reads >= 2:
                    self.mode = "fault"
            if self.mode == "stopping":
                self.stop_reads += 1
                phase, gate, allowed = ("5", "7", "0") if self.stop_reads == 1 \
                    else ("7", "0", "0")
                zero_sh = zero_el = "0"
                q_sh = q_el = "0"
            if self.mode == "fault":
                phase, gate, allowed = "8", "3", "0"
                zero_sh = zero_el = "0"
                q_sh = q_el = "na"
                cmd_sh = cmd_el = "na"
            stopped = "1" if phase == "7" else "0"
            seq = self.running_reads if self.mode == "running" else 0
            reason = "2" if self.failure == "blocked_start" and self.mode == "preparing" \
                and "followstart" in self.commands else "0"
            reply = (f"OK s5=1 axes=3 phase={phase} gate={gate} reason={reason} safety=0 "
                     f"q_sh_mrad={q_sh} q_el_mrad={q_el} "
                     f"cmd_sh_mNm={cmd_sh} cmd_el_mA={cmd_el} seq_sh={seq} seq_el={seq} "
                     f"zero_sh={zero_sh} zero_el={zero_el} remaining_ms=9000 "
                     f"output_allowed={allowed} stop={stopped} stop_failed=0")
        else:
            reply = "ERR unknown"
        return (reply + "\r\n").encode("ascii")


def offline_check():
    for failure in (None, "no_shoulder_move", "fault_running", "lost_start_reply",
                    "blocked_prepare", "blocked_start", "invalid_elbow",
                    "invalid_elbow_ready"):
        port = FakePort(failure)
        journal = io.StringIO()
        link = Link(port, journal)
        clock = FakeClock()
        cues = []
        signals = []
        def note(message):
            cues.append(message)
            if "现在请在承托下缓慢前抬肩" in message \
                    and failure != "no_shoulder_move":
                port.shoulder_moved = True
        result = run_once(link, observe_ms=500, manual_cue_ms=100,
                          clock=clock.clock, sleep=clock.sleep,
                          announce=note, signal=signals.append)
        entries = [json.loads(line) for line in journal.getvalue().splitlines()]
        require(port.commands.count("followprepare") == 1
                and port.commands.count("followstart") ==
                    (0 if failure in ("blocked_prepare", "invalid_elbow",
                                      "invalid_elbow_ready") else 1)
                and port.commands.count("followstop") == 1,
                f"{failure}: expected exactly one prepare/start/stop")
        require(("DUAL_ZERO_READY" in journal.getvalue()) == (failure != "blocked_prepare")
                and ("DUAL_READY" in journal.getvalue()) ==
                    (failure not in ("blocked_prepare", "invalid_elbow",
                                     "invalid_elbow_ready"))
                and (bool(result["error"]) == (failure not in (None, "no_shoulder_move"))),
                f"{failure}: READY/stop result mismatch")
        if failure not in ("blocked_prepare", "invalid_elbow", "invalid_elbow_ready"):
            ready = next(i for i, event in enumerate(entries)
                         if event.get("name") == "DUAL_READY")
            started = next(i for i, event in enumerate(entries)
                           if event.get("direction") == "tx"
                           and event.get("command") == "followstart")
            require(ready < started and entries[ready]["status"]["q_sh_mrad"] == "0"
                    and entries[ready]["status"]["q_el_mrad"] == "0"
                    and cues.count("双轴已静止并进入 READY；继续承托，稍后将自动启动。") == 1,
                    "zero-degree READY did not precede the one start")
        if failure == "blocked_prepare":
            require(not result["start_attempted"] and result["final_phase"] == "7",
                    "blocked preparation was misreported as an active session")
        if failure == "blocked_start":
            refused = next(i for i, event in enumerate(entries)
                           if event.get("name") == "DUAL_START_REFUSED")
            stopped = next(i for i, event in enumerate(entries)
                           if event.get("direction") == "tx"
                           and event.get("command") == "followstop")
            require(refused < stopped and entries[refused]["status"].get("reason") == "2",
                    "start refusal reason was not captured before stop")
        if failure in ("invalid_elbow", "invalid_elbow_ready"):
            invalid = next(i for i, event in enumerate(entries)
                           if event.get("name") == "DUAL_ELBOW_ANGLE_INVALID")
            stopped = next(i for i, event in enumerate(entries)
                           if event.get("direction") == "tx"
                           and event.get("command") == "followstop")
            require(not result["start_attempted"] and invalid < stopped
                    and entries[invalid]["elbowfeedback"].startswith("OK bus=elbow")
                    and port.commands.count("elbowfeedback") >= 2,
                    "persistent invalid elbow angle was not captured before stop")
        if failure is None:
            require(result["final_phase"] == "7" and result["manual_cue_sent"],
                    "healthy dual trial did not reach STOPPED")
        if failure in (None, "no_shoulder_move"):
            require(signals == ["zero", "hold", "started", "move"],
                    "the operator cues are missing or out of order")
            require(result["elbow_target_nonzero_seen"]
                    and result["shoulder_target_nonzero_seen"] == (failure is None),
                    "motor command target evidence is wrong")
            targets = [event for event in entries
                       if event.get("name") == "DUAL_TARGET_NONZERO"]
            require({event["axis"] for event in targets} ==
                    ({"shoulder", "elbow"} if failure is None else {"elbow"})
                    and all(event["meaning"] ==
                            "firmware_command_target_not_measured_assistance"
                            for event in targets),
                    "command target markers were not recorded distinctly")
        require(signals.count("move") == int(result["manual_cue_sent"]),
                "movement cue sound did not match its printed instruction")
        print(f"offline {failure or 'success'}: prepare=1 "
              f"start={port.commands.count('followstart')} stop=1 "
              f"result={'stopped' if not result['error'] else 'aborted'}")
    port = FakePort(shoulder_err="1", shoulder_history="1")
    journal = io.StringIO()
    link = Link(port, journal)
    clock = FakeClock()
    result = run_once(link, observe_ms=500, manual_cue_ms=100,
                      clock=clock.clock, sleep=clock.sleep, announce=lambda _message: None)
    require(result["error"] is None and result["final_phase"] == "7"
            and port.commands.count("followstart") == 1
            and port.commands.count("followstop") == 1
            and "err=1" in journal.getvalue()
            and "drop=1" in journal.getvalue(),
            "historical CAN1 error blocked a healthy dual session")
    print("offline historical_can1_error: prepare=1 start=1 stop=1 result=stopped")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-check", action="store_true", help="in-memory serial only")
    parser.add_argument("--run", action="store_true", help="one live R5 session")
    parser.add_argument("--port", help="explicit ST-LINK VCP")
    parser.add_argument("--output-dir", type=Path, help="H: JSONL evidence directory")
    parser.add_argument("--board-bin-sha256", help="hash from independent board readback")
    parser.add_argument("--observe-ms", type=int, default=4500,
                        help="RUNNING observation before followstop, 500..42000 ms")
    args = parser.parse_args()
    require(500 <= args.observe_ms <= 42000, "observe-ms must be 500..42000")
    if args.offline_check:
        require(not args.run, "offline check cannot run a live session")
        offline_check()
        return 0
    require(args.run and args.port and args.output_dir and args.board_bin_sha256,
            "live mode needs --run, --port, --output-dir and --board-bin-sha256")
    require(re.fullmatch(r"[0-9a-fA-F]{64}", args.board_bin_sha256) is not None,
            "board-bin-sha256 must be 64 hexadecimal digits")
    output = args.output_dir.resolve()
    require(output.drive.upper() == "H:", "output-dir must resolve to H: drive")
    output.mkdir(parents=True, exist_ok=True)
    temp = output / "tmp"
    temp.mkdir(exist_ok=True)
    for key in ("TEMP", "TMP", "TMPDIR"):
        os.environ[key] = str(temp)
    path = output / ("s5-r5-dual-follow-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".jsonl")
    with path.open("x", encoding="utf-8", newline="\n") as journal:
        port = open_vcp(args.port)
        try:
            link = Link(port, journal)
            link.record(direction="metadata", board_bin_sha256=args.board_bin_sha256.lower(),
                        hash_source="independent_readback_supplied_to_script",
                        profile="S5_R5_DUAL_FOLLOW", force_reading="not_requested",
                        observe_ms=args.observe_ms)
            def live_signal(name):
                try:
                    import winsound
                    frequency, duration = {"zero": (900, 180),
                                           "hold": (1200, 250),
                                           "started": (1800, 80),
                                           "move": (2200, 180)}[name]
                    winsound.Beep(frequency, duration)
                except (ImportError, OSError, RuntimeError) as exc:
                    print(f"{name} 提示音不可用，以终端文字为准：{exc}", flush=True)

            result = run_once(link, observe_ms=args.observe_ms,
                              announce=lambda message: print(message, flush=True),
                              signal=live_signal)
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
