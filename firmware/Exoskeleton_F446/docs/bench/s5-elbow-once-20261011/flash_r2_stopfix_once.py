"""Back up, flash, and independently read back one exact S5 R2 stop-fix elbow image.

Run only after the operator confirms 24 V is off and the arm is supported.
No serial or motor commands are sent. A failed operation is never retried.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess


FIRMWARE = Path(__file__).resolve().parents[3]
BUILD = FIRMWARE / "build"
ELF = BUILD / "s5-r2-stopfix-20261011/Exoskeleton_F446.elf"
BIN = BUILD / "s5-r2-stopfix-20261011/Exoskeleton_F446.bin"
OLD = BUILD / "s5-r2-elbow-trial/Exoskeleton_F446.bin"
OUT = BUILD / "s5-r2-stopfix-board-20261011"
CLI = Path(r"C:\Users\asus\AppData\Local\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe")
SN = "066EFF555748874987182240"
ELF_SHA = "010a8307291ab0ea4eca4e595f0c8ff16f5b175be19e483d30c74c9e24f63f2f"
BIN_SHA = "297b35ce7293ae9d068c4a9179f0f69529ca1e07a0ae54c31e6de17263e030eb"
OLD_SHA = "3adf9f9945d51c061b3669caa92fff9a9c16e95ac338208931660ce386a366a9"


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


def main() -> None:
    require(FIRMWARE.drive.upper() == "H:" and not OUT.exists(),
            "Evidence must be a new H: board directory")
    require(CLI.is_file() and ELF.is_file() and BIN.is_file() and OLD.is_file(),
            "Tool or firmware image missing")
    require(digest(ELF) == ELF_SHA and digest(BIN) == BIN_SHA
            and BIN.stat().st_size == 137436, "S5 R2 stop-fix candidate changed")
    require(digest(OLD) == OLD_SHA, "Known old R2 board image changed")
    OUT.mkdir()
    temp = OUT / "tmp"
    temp.mkdir()
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(temp)
    record = {"status": "STARTED_DO_NOT_REPEAT", "passed": False,
              "power_basis": "Operator confirmed 24 V off and arm supported before this run",
              "motor_commands": 0, "automatic_retry": False, "flash_attempts": 0,
              "new_bin_sha256": BIN_SHA, "old_bin_sha256": OLD_SHA}
    save("attempt.json", record)
    hotplug = ["-c", "port=SWD", "mode=HOTPLUG", "freq=4000", "sn=" + SN]
    backup = OUT / "preflash-512k.bin"
    message = run("backup", hotplug + ["-u", "0x08000000", "0x80000", str(backup)])
    require(SN in message and "STM32F446xx" in message and "0x421" in message
            and "512 KBytes" in message, "Unexpected board identity")
    require(backup.stat().st_size == 524288, "Incomplete Flash backup")
    require(backup.read_bytes()[:OLD.stat().st_size] == OLD.read_bytes(),
            "Board does not contain the known old R2 image")
    record.update({"backup_sha256": digest(backup), "old_prefix_matches": True,
                   "flash_attempts": 1})
    save("attempt.json", record)
    message = run("flash", ["-c", "port=SWD", "mode=NORMAL", "freq=4000", "sn=" + SN,
                            "-w", str(ELF), "-v", "-rst"])
    require("Download verified successfully" in message
            and "Software reset is performed" in message,
            "Official verify or reset absent")
    readback = OUT / "postflash-137436.bin"
    run("readback", hotplug + ["-u", "0x08000000", hex(BIN.stat().st_size),
                                str(readback)])
    require(readback.read_bytes() == BIN.read_bytes(), "Independent readback mismatch")
    record.update({"readback_sha256": digest(readback),
                   "status": "FLASH_READBACK_MATCHED_USB_ONLY", "passed": True})
    save("attempt.json", record)
    print("S5 R2 image flashed and independently read back; no motor command sent", flush=True)


if __name__ == "__main__":
    main()
