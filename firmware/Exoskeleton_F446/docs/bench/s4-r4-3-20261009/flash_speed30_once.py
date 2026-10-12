"""Flash one exact R4-3 gain-0.36 speed-30 isolated image with 24 V off, backup and readback.

This script has no CAN or serial motor command. It does not retry a failed flash.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess


HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parents[2]
BUILD = FIRMWARE / "build"
CANDIDATE = BUILD / "s4-r4-3-gain36-speed30-20261011/build/Debug"
ELF = CANDIDATE / "Exoskeleton_F446.elf"
BIN = CANDIDATE / "Exoskeleton_F446.bin"
OLD = BUILD / "s4-r4-3-gain36-speed25-20261011/build/Debug/Exoskeleton_F446.bin"
OUT = BUILD / "s4-r4-3-gain36-speed30-board-20261011"
CLI = Path(r"C:\Users\asus\AppData\Local\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe")
SN = "066EFF555748874987182240"
NEW_ELF_SHA = "7527e6be4dfa237769457e08a0052c90139487b8b0bbc9fd8d6f12e2a80f7d03"
NEW_BIN_SHA = "0e41af6e5d07483bbc8cadf167c65de6810164012f14c62e98a8ecf7326126b5"
OLD_BIN_SHA = "97898853fd5a630808d33aaec631229d48d1ee4912497091fe964856f9008677"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def save(name: str, value: dict) -> None:
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")


def run(name: str, args: list[str]) -> str:
    command = [str(CLI), *args]
    record = {"command": command,
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    save(name + ".json", record)
    try:
        result = subprocess.run(command, capture_output=True, timeout=60)
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


def main() -> None:
    require(FIRMWARE.drive.upper() == "H:" and not OUT.exists(),
            "Evidence must be a new H: board directory")
    require(CLI.is_file() and ELF.is_file() and BIN.is_file() and OLD.is_file(),
            "Tool or firmware image missing")
    require(digest(ELF) == NEW_ELF_SHA and digest(BIN) == NEW_BIN_SHA,
            "R4-3 gain36 speed30 candidate image differs from offline build")
    require(digest(OLD) == OLD_BIN_SHA and BIN.stat().st_size == 119036
            and OLD.stat().st_size == 119036,
            "Old or new BIN identity/size mismatch")
    OUT.mkdir()
    temp = OUT / "tmp"
    temp.mkdir()
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(temp)
    record = {"status": "STARTED_DO_NOT_REPEAT", "passed": False,
              "power_basis": "Operator confirmed 24V off and arm supported before this run",
              "motor_commands": 0, "automatic_retry": False, "flash_attempts": 0,
              "new_bin_sha256": NEW_BIN_SHA, "old_bin_sha256": OLD_BIN_SHA,
              "limits": "gain 0.36, 20-30 deg, 40 mNm rotor cap, 30 deg/s trial gate, 10 s session, 1 s ramp"}
    save("attempt.json", record)
    hotplug = ["-c", "port=SWD", "mode=HOTPLUG", "freq=4000", "sn=" + SN]
    backup = OUT / "preflash-512k.bin"
    message = run("backup", hotplug + ["-u", "0x08000000", "0x80000", str(backup)])
    require(SN in message and "STM32F446xx" in message and "0x421" in message
            and "512 KBytes" in message, "Unexpected board identity")
    require(backup.stat().st_size == 524288, "Incomplete Flash backup")
    require(backup.read_bytes()[:OLD.stat().st_size] == OLD.read_bytes(),
            "Board does not contain the known speed25 image")
    record.update({"backup_sha256": digest(backup), "old_prefix_matches": True,
                   "flash_attempts": 1})
    save("attempt.json", record)
    message = run("flash", ["-c", "port=SWD", "mode=NORMAL", "freq=4000", "sn=" + SN,
                            "-w", str(ELF), "-v", "-rst"])
    require("Download verified successfully" in message
            and "Software reset is performed" in message,
            "Official verify or reset absent")
    readback = OUT / "postflash-119036.bin"
    run("readback", hotplug + ["-u", "0x08000000", hex(BIN.stat().st_size),
                                str(readback)])
    require(readback.read_bytes() == BIN.read_bytes(), "Independent readback mismatch")
    record.update({"readback_sha256": digest(readback),
                   "status": "FLASH_READBACK_MATCHED_USB_ONLY", "passed": True})
    save("attempt.json", record)
    print("R4-3 gain36 speed30 image flashed and independently read back; no motor command sent", flush=True)


if __name__ == "__main__":
    main()
