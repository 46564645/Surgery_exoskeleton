"""Flash the R5 SDO reply retry x4 candidate (45 s session), backup and readback.

No CAN or serial motor command. Does not retry a failed flash.
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
CANDIDATE = BUILD / "s5-r5-retry4-20261022"
ELF = CANDIDATE / "Exoskeleton_F446.elf"
BIN = CANDIDATE / "Exoskeleton_F446.bin"
OUT = BUILD / "s5-r5-retry4-board-20261022"
CLI = Path(r"C:\Users\asus\AppData\Local\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe")
SN = "066EFF555748874987182240"
NEW_ELF_SHA = "cd95187bf764ce9307fef8e7eafb8c5f6fa16b4172d16ab3abd1dec955f41e10"
NEW_BIN_SHA = "a71524fd0305e1d44bee6566173915c6d5f66dc3f099d4bcae8cc4731e7676f0"
OLD_BIN_SHA = "0b6e509c30a8cf3e26250142cdb242b375e66e9ce1de3fe9f2c73af36365f053"
OLD_SIZE = 139052


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


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
        result = subprocess.run(command, capture_output=True, timeout=120)
        raw = result.stdout + result.stderr
        record.update({"returncode": result.returncode, "timed_out": False})
    except subprocess.TimeoutExpired as exc:
        raw = (exc.stdout or b"") + (exc.stderr or b"")
        record.update({"returncode": -1, "timed_out": True})
    (OUT / (name + ".raw")).write_bytes(raw)
    message = raw.decode("utf-8", errors="replace")
    (OUT / (name + ".log")).write_text(message, encoding="utf-8")
    record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    record["raw_sha256"] = digest(raw)
    save(name + ".json", record)
    print(f"{name}: exit={record['returncode']}", flush=True)
    require(record["returncode"] == 0 and not record["timed_out"],
            name + " failed; no automatic retry")
    return message


def main() -> None:
    require(FIRMWARE.drive.upper() == "H:" and not OUT.exists(),
            "Evidence must be a new H: board directory")
    require(CLI.is_file() and ELF.is_file() and BIN.is_file(),
            "Tool or firmware image missing")
    require(digest(ELF.read_bytes()) == NEW_ELF_SHA and digest(BIN.read_bytes()) == NEW_BIN_SHA,
            "Candidate image differs from the built offline image")
    OUT.mkdir()
    temp = OUT / "tmp"
    temp.mkdir()
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(temp)
    record = {"status": "STARTED_DO_NOT_REPEAT", "passed": False,
              "power_basis": "Operator instructed flash; 24 V state not independently verified",
              "motor_commands": 0, "automatic_retry": False, "flash_attempts": 0,
              "new_elf_sha256": NEW_ELF_SHA, "new_bin_sha256": NEW_BIN_SHA,
              "old_bin_sha256": OLD_BIN_SHA}
    save("attempt.json", record)
    hotplug = ["-c", "port=SWD", "mode=HOTPLUG", "freq=4000", "sn=" + SN]
    backup = OUT / "preflash-512k.bin"
    message = run("backup", hotplug + ["-u", "0x08000000", "0x80000", str(backup)])
    require(SN in message and "STM32F446xx" in message and "0x421" in message
            and "512 KBytes" in message, "Unexpected board identity")
    require(backup.stat().st_size == 524288, "Incomplete Flash backup")
    prefix = backup.read_bytes()[:OLD_SIZE]
    record.update({"backup_sha256": digest(backup.read_bytes()),
                   "old_prefix_sha256": digest(prefix),
                   "previous_image_match": digest(prefix) == OLD_BIN_SHA,
                   "flash_attempts": 1})
    save("attempt.json", record)
    message = run("flash", ["-c", "port=SWD", "mode=NORMAL", "freq=4000", "sn=" + SN,
                            "-w", str(ELF), "-v", "-rst"])
    require("Download verified successfully" in message
            and "Software reset is performed" in message,
            "Official verify or reset absent")
    readback = OUT / "postflash-138868.bin"
    run("readback", hotplug + ["-u", "0x08000000", hex(BIN.stat().st_size), str(readback)])
    require(readback.read_bytes() == BIN.read_bytes(), "Independent readback mismatch")
    record.update({"readback_sha256": digest(readback.read_bytes()),
                   "independent_readback_match": True,
                   "status": "FLASH_READBACK_MATCHED", "passed": True})
    save("attempt.json", record)
    print("Candidate flashed and independently read back; no motor command sent", flush=True)


if __name__ == "__main__":
    main()
