#!/usr/bin/env python3
"""One bounded S3 elbow pulse over an explicitly selected VCP; no port scan."""

import argparse
import io
import json
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


class TrialError(RuntimeError):
    pass


def fields(reply):
    return dict(re.findall(r"(?:^| )([a-z][a-z0-9_]*)=([^ ]+)", reply))


def require(condition, message):
    if not condition:
        raise TrialError(message)


class Link:
    def __init__(self, port, journal):
        self.port = port
        self.journal = journal
        self.sequence = 0

    def record(self, **event):
        event.update(utc=datetime.now(timezone.utc).isoformat(), monotonic_s=time.monotonic())
        self.journal.write(json.dumps(event, ensure_ascii=False) + "\n")
        self.journal.flush()

    def ask(self, command, timeout_s=None):
        self.sequence += 1
        tx = (command + "\n").encode("ascii")
        self.record(seq=self.sequence, direction="tx", command=command, raw_hex=tx.hex())
        previous_timeouts = None
        try:
            if timeout_s is not None and hasattr(self.port, "timeout"):
                previous_timeouts = (self.port.timeout, self.port.write_timeout)
                self.port.timeout = timeout_s
                self.port.write_timeout = timeout_s
            written = self.port.write(tx)
            require(written == len(tx), f"{command}: incomplete serial write")
            raw = self.port.read_until(b"\n", 256)
            self.record(seq=self.sequence, direction="rx", command=command,
                        raw_hex=raw.hex(), raw_ascii=raw.decode("ascii", "backslashreplace"))
            require(raw.endswith(b"\n"), f"{command}: response timeout or truncated line")
            reply = raw.decode("ascii").strip()
            require(reply.startswith("OK "), f"{command}: {reply or 'empty reply'}")
            return reply
        except Exception as exc:
            self.record(seq=self.sequence, direction="error", command=command,
                        error=f"{type(exc).__name__}: {exc}")
            raise
        finally:
            if previous_timeouts is not None:
                self.port.timeout, self.port.write_timeout = previous_timeouts


def read_preflight(link):
    require(link.ask("ping") == "OK pong", "wrong ping response")
    status = fields(link.ask("status"))
    require(status.get("stage") == "S2_CAN_RX" and status.get("output_allowed") == "0",
            "unexpected stage or output already enabled")
    can = fields(link.ask("canstatus"))
    require(can.get("can2_started") == "1" and can.get("can2_baud") == "1000000"
            and can.get("can2_start_error") == "0", "CAN2 startup is not ready")
    elbow = fields(link.ask("elbowfeedback"))
    require(elbow.get("bus") == "elbow" and elbow.get("motor_id") == "1"
            and elbow.get("seen") == "1" and elbow.get("valid") == "1"
            and elbow.get("online") == "1", "elbow feedback is missing or invalid")
    age = elbow.get("age_ms", "")
    require(age.isdecimal() and int(age) <= 100, "elbow feedback exceeds 100 ms freshness limit")
    stop = fields(link.ask("stopstatus"))
    require(stop.get("source") == "c620_feedback"
            and stop.get("stop_pending") in ("0", "1")
            and stop.get("stop_txok") in ("0", "1"),
            "R2 stop-fix image or C620 feedback is missing")
    gate = fields(link.ask("outputstatus"))
    require(gate.get("stage") == "S2_CAN_RX" and gate.get("s3_state") == "0"
            and gate.get("transport_bound") == "1" and gate.get("output_allowed") == "0"
            and gate.get("elbow_zero_pending") == "0", "S3 gate is not safely idle")


def run_once(link, sleep=time.sleep, midpulse_sample=False):
    """The only path that can send elbowpulse; cancel is attempted once in finally."""
    error = None
    pulse_sent = False
    cancelled = False
    post = {}
    observation = {"before": None, "during": None,
                   "during_request_after_ms": None,
                   "during_response_after_ms": None} if midpulse_sample else None
    try:
        read_preflight(link)
        require(link.ask("prepare") == "OK preparing output_allowed=0", "prepare refused")
        deadline = time.monotonic() + 2.0
        while True:
            gate = fields(link.ask("outputstatus"))
            require(gate.get("s3_state") != "3", "S3 entered FAULT during prepare")
            if gate.get("s3_state") == "5":
                require(gate.get("configured") == "1" and gate.get("elbow_zero_valid") == "1"
                        and gate.get("output_allowed") == "0", "READY lacks elbow session zero")
                break
            require(time.monotonic() < deadline, "READY timeout")
            sleep(0.05)
        # Preparing the RAM elbow zero resets the C620 standstill window.
        # READY can therefore precede the 250 ms stillness proof needed by Arm.
        while True:
            stop = fields(link.ask("stopstatus"))
            require(stop.get("source") == "c620_feedback" and stop.get("seen") == "1"
                    and stop.get("fresh") == "1", "C620 standstill feedback is unavailable")
            if stop.get("stationary") == "1":
                break
            require(time.monotonic() < deadline, "post-READY standstill timeout")
            sleep(0.05)
        armed = link.ask("arm")
        match = re.fullmatch(r"OK armed token=([1-9][0-9]*)", armed)
        require(match is not None, "arm returned no token")
        token = match.group(1)
        gate = fields(link.ask("outputstatus"))
        require(gate.get("s3_state") == "1" and gate.get("output_allowed") == "1"
                and gate.get("arm_token") == token, "arm state or token changed")
        if midpulse_sample:
            observation["before"] = link.ask("elbowfeedback", timeout_s=0.05)
            before = fields(observation["before"])
            require(before.get("valid") == "1" and before.get("online") == "1"
                    and before.get("output_allowed") == "1",
                    "pre-pulse C620 feedback is unavailable")
        pulse_sent = True  # Set before the write; never send another pulse if reply is lost.
        pulse_at = time.monotonic()
        require(link.ask(f"elbowpulse {token} 2000 150", timeout_s=0.05) == "OK request",
                "pulse refused")
        if midpulse_sample:
            sleep(max(0.0, pulse_at + 0.04 - time.monotonic()))
            require(time.monotonic() < pulse_at + 0.075,
                    "mid-pulse sampling window missed")
            observation["during_request_after_ms"] = round(
                (time.monotonic() - pulse_at) * 1000.0, 3)
            observation["during"] = link.ask("elbowfeedback", timeout_s=0.03)
            observation["during_response_after_ms"] = round(
                (time.monotonic() - pulse_at) * 1000.0, 3)
            during = fields(observation["during"])
            require(during.get("valid") == "1" and during.get("online") == "1"
                    and during.get("output_allowed") == "1",
                    "mid-pulse C620 feedback or output permission is unavailable")
            before_tick = before.get("received_ms", "")
            during_tick = during.get("received_ms", "")
            age = during.get("age_ms", "")
            require(before_tick.isdecimal() and during_tick.isdecimal()
                    and age.isdecimal() and int(age) <= 20
                    and 0 < ((int(during_tick) - int(before_tick)) & 0xFFFFFFFF) < 0x80000000,
                    "mid-pulse C620 feedback is not a fresh frame")
            require(time.monotonic() < pulse_at + 0.10,
                    "mid-pulse query overran cancel target")
        # Cancel before the 150 ms firmware TTL; the TTL remains a fallback.
        sleep(max(0.0, pulse_at + 0.10 - time.monotonic()))
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            cancelled = link.ask("cancel") == "OK cancelled output_allowed=0"
            require(cancelled, "cancel was not accepted")
        except Exception as exc:
            error = (error + "; " if error else "") + f"cancel: {type(exc).__name__}: {exc}"
        for command in ("stopstatus", "elbowfeedback", "outputstatus"):
            try:
                post[command] = link.ask(command)
            except Exception as exc:
                error = (error + "; " if error else "") + f"{command}: {type(exc).__name__}: {exc}"
        deadline = time.monotonic() + 0.20
        while fields(post.get("stopstatus", "")).get("stop_pending") == "1" \
                and time.monotonic() < deadline:
            sleep(0.01)
            try:
                post["stopstatus"] = link.ask("stopstatus")
            except Exception as exc:
                error = (error + "; " if error else "") + f"stopstatus: {type(exc).__name__}: {exc}"
                break
        gate = fields(post.get("outputstatus", ""))
        if gate.get("s3_state") != "0" or gate.get("output_allowed") != "0":
            error = (error + "; " if error else "") + "post-stop SAFE_OFF was not confirmed"
        stop = fields(post.get("stopstatus", ""))
        if (stop.get("source") != "c620_feedback" or stop.get("stop_attempted") != "1"
                or stop.get("stop_accepted") != "1" or stop.get("purge_ok") != "1"
                or stop.get("stop_pending") != "0" or stop.get("stop_txok") != "1"
                or stop.get("stop_delivery_failed") != "0"):
            error = (error + "; " if error else "") + "C620 zero frame TXOK was not confirmed"
    result = {"pulse_sent": pulse_sent, "cancel_accepted": cancelled,
              "post": post, "error": error}
    if midpulse_sample:
        result["midpulse_observation"] = observation
    return result


class FakePort:
    """In-memory response script; it never imports or opens pyserial."""
    BASE = {
        "ping": "OK pong",
        "status": "OK stage=S2_CAN_RX output_allowed=0",
        "canstatus": "OK can2_started=1 can2_baud=1000000 can2_start_error=0",
        "elbowfeedback": "OK bus=elbow motor_id=1 seen=1 valid=1 online=1 rotor_pos_rad=0.000000 age_ms=10",
        "stopstatus": "OK source=c620_feedback seen=1 fresh=1 stationary=1 dwell_ms=250 stop_attempted=1 stop_accepted=1 purge_ok=1 stop_pending=0 stop_txok=1 stop_delivery_failed=0",
        "cancel": "OK cancelled output_allowed=0",
    }

    def __init__(self, failure=None):
        self.failure = failure
        self.commands = []
        self.pending = None
        self.output_reads = 0
        self.stop_reads = 0
        self.feedback_reads = 0

    def write(self, data):
        command = data.decode("ascii").strip()
        self.commands.append(command)
        self.pending = command
        return len(data)

    def read_until(self, _terminator, _limit):
        command = self.pending
        if command == "outputstatus":
            self.output_reads += 1
            state = ("0" if self.output_reads == 1 else "5" if self.output_reads == 2
                     else "1" if self.output_reads == 3 else "0")
            reply = (f"OK stage=S2_CAN_RX s3_state={state} configured=1 transport_bound=1 "
                     f"output_allowed={int(state == '1')} elbow_zero_pending=0 "
                     f"elbow_zero_valid={int(state != '0' or self.output_reads > 3)} arm_token=7")
        elif command == "prepare":
            reply = "ERR blocked" if self.failure == "prepare" else "OK preparing output_allowed=0"
        elif command == "stopstatus":
            self.stop_reads += 1
            reply = self.BASE[command]
            if self.stop_reads == 2 or (self.failure == "standstill" and self.stop_reads > 1):
                reply = reply.replace("stationary=1 dwell_ms=250", "stationary=0 dwell_ms=4")
            if self.stop_reads == 4:
                reply = reply.replace("stop_pending=0 stop_txok=1", "stop_pending=1 stop_txok=0")
            if self.failure == "stop_tx" and self.stop_reads >= 5:
                reply = reply.replace("stop_txok=1", "stop_txok=0")
        elif command == "arm":
            reply = "OK armed token=7"
        elif command.startswith("elbowpulse "):
            reply = "ERR blocked" if self.failure == "pulse" else "OK request"
        elif command == "elbowfeedback":
            self.feedback_reads += 1
            active = "cancel" not in self.commands and self.output_reads >= 3
            pulsing = active and any(c.startswith("elbowpulse ") for c in self.commands)
            if self.failure == "mid_feedback" and pulsing:
                return b""
            received_ms = (self.feedback_reads - 1 if self.failure == "stale_mid_feedback"
                           and pulsing else self.feedback_reads) * 10
            reply = ("OK bus=elbow motor_id=1 seen=1 valid=1 online=1 "
                     f"rotor_pos_rad=0.000000 angle_ticks={1002 if pulsing else 1000} "
                     f"rpm={2 if pulsing else 0} current_raw={400 if pulsing else 0} "
                     f"received_ms={received_ms} age_ms=1 "
                     f"output_allowed={int(active)}")
        else:
            reply = self.BASE[command]
        return (reply + "\r\n").encode("ascii")


def offline_check():
    for midpulse_sample in (False, True):
        failures = (None, "prepare", "standstill", "pulse", "stop_tx")
        if midpulse_sample:
            failures += ("mid_feedback", "stale_mid_feedback")
        for failure in failures:
            fake = FakePort(failure)
            journal = io.StringIO()
            result = run_once(Link(fake, journal), sleep=lambda _seconds: None,
                              midpulse_sample=midpulse_sample)
            pulses = sum(command.startswith("elbowpulse ") for command in fake.commands)
            cancels = fake.commands.count("cancel")
            require(pulses <= 1 and cancels == 1, f"{failure}: pulse/cancel count")
            require(bool(result["error"]) == (failure is not None), f"{failure}: result")
            require((pulses == 1) == (failure not in ("prepare", "standstill")),
                    f"{failure}: pulse path")
            if failure is None:
                require(fake.stop_reads >= 5, "standstill and pending stop were not rechecked")
                if midpulse_sample:
                    require(result["midpulse_observation"]["during"] is not None,
                            "mid-pulse feedback missing")
                    commands = fake.commands
                    pulse_index = next(i for i, c in enumerate(commands)
                                       if c.startswith("elbowpulse "))
                    require(commands[pulse_index + 1:pulse_index + 3]
                            == ["elbowfeedback", "cancel"],
                            "mid-pulse sample must precede cancel")
            if failure == "mid_feedback":
                require(result["midpulse_observation"]["during"] is None,
                        "failed mid-pulse read was accepted")
            require("raw_hex" in journal.getvalue(), f"{failure}: raw journal")
            print(f"offline midpulse={int(midpulse_sample)} {failure or 'success'}: "
                  f"pulse={pulses}, cancel={cancels}, "
                  f"result={'stopped' if not result['error'] else 'aborted'}")


def open_vcp(name):
    try:
        import serial
    except ImportError as exc:
        raise TrialError("pyserial is required for live use") from exc
    port = serial.Serial(port=None, baudrate=115200, bytesize=8, parity="N", stopbits=1,
                         timeout=0.35, write_timeout=0.35, xonxoff=False, rtscts=False,
                         dsrdtr=False)
    port.dtr = False
    port.rts = False
    port.port = name
    port.open()
    return port


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="explicit VCP, e.g. COM10; never auto-scanned")
    parser.add_argument("--output-dir", type=Path, help="H: directory for raw JSONL transcript")
    parser.add_argument("--midpulse-sample", action="store_true",
                        help="read one fresh C620 feedback sample during the same pulse")
    parser.add_argument("--offline-check", action="store_true", help="fake VCP only; no device")
    args = parser.parse_args()
    if args.offline_check:
        offline_check()
        return 0
    require(args.port and args.output_dir, "live use requires --port and --output-dir")
    output = args.output_dir.resolve()
    require(output.drive.upper() == "H:", "output-dir must resolve to H: drive")
    output.mkdir(parents=True, exist_ok=True)
    path = output / ("elbow-once-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".jsonl")
    with path.open("x", encoding="utf-8", newline="\n") as journal:
        port = open_vcp(args.port)
        try:
            result = run_once(Link(port, journal), midpulse_sample=args.midpulse_sample)
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
