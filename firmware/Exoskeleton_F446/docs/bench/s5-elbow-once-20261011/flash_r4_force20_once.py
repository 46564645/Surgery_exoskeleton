"""Back up, flash, and independently read back one exact S5 R4 elbow image.

Use --offline-check to validate files without opening ST-LINK. --flash is a
one-time operation after the operator confirms 24 V is off and the arm is
supported. This script sends no motor or serial commands and never retries.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess


FIRMWARE = Path(__file__).resolve().parents[3]
BUILD = FIRMWARE / "build"
ELF = BUILD / "s5-r4-force20-20261011/Exoskeleton_F446.elf"
BIN = BUILD / "s5-r4-force20-20261011/Exoskeleton_F446.bin"
OLD = BUILD / "s5-r2-can2-capture-20261011/Exoskeleton_F446.bin"
OUT = BUILD / "s5-r4-force20-board-20261011"
CLI = Path(r"C:\Users\asus\AppData\Local\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe")
SN = "066EFF555748874987182240"
ELF_SHA = "8412f0872f5014028f32534d2d7f8c06a5adf95c9907876150f3c43c90daed96"
BIN_SHA = "fa3f93140fe5e19474b69f7ecfa7d0d3f20adf9897fde8c777cf59919df9b309"
OLD_SHA = "4113182d09ed5ef9102916fdce08b87c4bd717ea0a00a57b0e0f98414f32732c"
BIN_SIZE = 137484
OLD_SIZE = 141916
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
            and BIN.stat().st_size == BIN_SIZE, "S5 R4 force20 candidate changed")
    require(digest(OLD) == OLD_SHA and OLD.stat().st_size == OLD_SIZE,
            "Expected CAN2 blackbox board image changed")
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
            "Board does not contain the expected CAN2 blackbox image")
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
    print("S5 R4 elbow image flashed and independently read back; no motor command sent",
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
