"""Back up, flash, and read back one exact S5 R2 CAN2 capture image.

The offline check never opens ST-LINK. Flashing requires --flash and an operator
confirmation that 24 V is off and the arm is supported. No motor commands or
automatic retries are issued.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess


FIRMWARE = Path(__file__).resolve().parents[2]
BUILD = FIRMWARE / "build"
ELF = BUILD / "s5-r2-can2-capture-20261011/Exoskeleton_F446.elf"
BIN = BUILD / "s5-r2-can2-capture-20261011/Exoskeleton_F446.bin"
OLD = BUILD / "s5-r2-stopfix-20261011/Exoskeleton_F446.bin"
OUT = BUILD / "s5-r2-can2-capture-board-20261011"
CLI = Path(r"C:\Users\asus\AppData\Local\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe")
SN = "066EFF555748874987182240"
ELF_SHA = "b1a30574e9461906fb65d07b3e1973cbdd7e6046325202ed17eb9b38d05dc37a"
BIN_SHA = "4113182d09ed5ef9102916fdce08b87c4bd717ea0a00a57b0e0f98414f32732c"
OLD_SHA = "297b35ce7293ae9d068c4a9179f0f69529ca1e07a0ae54c31e6de17263e030eb"
BIN_SIZE = 141916
OLD_SIZE = 137436
FLASH_SIZE = 524288


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def check_inputs() -> dict:
    require(FIRMWARE.drive.upper() == "H:" and OUT.drive.upper() == "H:",
            "Firmware and evidence must be on H:")
    require(CLI.is_file() and ELF.is_file() and BIN.is_file() and OLD.is_file(),
            "Programmer or exact firmware image is missing")
    require(digest(ELF) == ELF_SHA and digest(BIN) == BIN_SHA
            and BIN.stat().st_size == BIN_SIZE, "CAN2 capture candidate changed")
    require(digest(OLD) == OLD_SHA and OLD.stat().st_size == OLD_SIZE,
            "Known R2 board image changed")
    require(not OUT.exists(), "Evidence directory already exists; do not repeat")
    return {"candidate_elf_sha256": ELF_SHA, "candidate_bin_sha256": BIN_SHA,
            "candidate_bin_size": BIN_SIZE, "expected_board_bin_sha256": OLD_SHA,
            "expected_board_bin_size": OLD_SIZE, "probe_sn": SN,
            "evidence_dir": str(OUT), "hardware_access": False}


def save(name: str, value: dict) -> None:
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")


def run(name: str, args: list[str]) -> str:
    command = [str(CLI), *args]
    record = {"command": command,
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    save(name + ".json", record)
    try:
        result = subprocess.run(command, capture_output=True, timeout=60, check=False)
        raw = result.stdout + result.stderr
        record.update({"returncode": result.returncode, "timed_out": False})
    except subprocess.TimeoutExpired as exc:
        raw = (exc.stdout or b"") + (exc.stderr or b"")
        record.update({"returncode": -1, "timed_out": True})
    (OUT / (name + ".raw")).write_bytes(raw)
    message = raw.decode("utf-8", errors="replace")
    (OUT / (name + ".log")).write_text(message, encoding="utf-8")
    record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    record["raw_sha256"] = hashlib.sha256(raw).hexdigest()
    save(name + ".json", record)
    print(f"{name}: exit={record['returncode']} log={OUT / (name + '.log')}", flush=True)
    require(record["returncode"] == 0 and not record["timed_out"],
            name + " failed; no automatic retry")
    return message


def flash_once() -> None:
    identity = check_inputs()
    OUT.mkdir()
    temp = OUT / "tmp"
    temp.mkdir()
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(temp)
    record = {"status": "STARTED_DO_NOT_REPEAT", "passed": False,
              "power_basis": "Operator confirmed 24 V off and arm supported before this run",
              "motor_commands": 0, "automatic_retry": False, "flash_attempts": 0,
              **identity, "hardware_access": True}
    save("attempt.json", record)
    hotplug = ["-c", "port=SWD", "mode=HOTPLUG", "freq=4000", "sn=" + SN]
    backup = OUT / "preflash-512k.bin"
    message = run("backup", hotplug + ["-u", "0x08000000", "0x80000", str(backup)])
    require(SN in message and "STM32F446xx" in message and "0x421" in message
            and "512 KBytes" in message, "Unexpected board identity")
    require(backup.stat().st_size == FLASH_SIZE, "Incomplete Flash backup")
    require(backup.read_bytes()[:OLD_SIZE] == OLD.read_bytes(),
            "Board does not contain the known R2 stop-fix image")
    record.update({"backup_sha256": digest(backup), "old_prefix_matches": True,
                   "flash_attempts": 1})
    save("attempt.json", record)
    message = run("flash", ["-c", "port=SWD", "mode=NORMAL", "freq=4000", "sn=" + SN,
                            "-w", str(ELF), "-v", "-rst"])
    require("Download verified successfully" in message
            and "Software reset is performed" in message,
            "Official verify or reset absent")
    readback = OUT / f"postflash-{BIN_SIZE}.bin"
    run("readback", hotplug + ["-u", "0x08000000", hex(BIN_SIZE), str(readback)])
    require(readback.stat().st_size == BIN_SIZE and readback.read_bytes() == BIN.read_bytes(),
            "Independent readback mismatch")
    record.update({"readback_sha256": digest(readback),
                   "status": "FLASH_READBACK_MATCHED_USB_ONLY", "passed": True})
    save("attempt.json", record)
    print("S5 R2 capture image flashed and independently read back; no motor command sent",
          flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--offline-check", action="store_true",
                      help="check exact images and tool path without opening ST-LINK")
    mode.add_argument("--flash", action="store_true",
                      help="perform the one-time board operation after operator confirmation")
    args = parser.parse_args()
    if args.offline_check:
        print(json.dumps(check_inputs(), ensure_ascii=False, indent=2))
    else:
        flash_once()


if __name__ == "__main__":
    main()
