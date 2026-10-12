"""Materialize one isolated R4-3 candidate with a 15 deg/s speed gate.

The original 10 deg/s trial and active production firmware remain untouched.
This is an offline file operation; it never connects to the board.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parents[2]
BUILD_ROOT = (FIRMWARE / "build").resolve()
SITE = "App/application/s4_site.c"
OLD = b".s3.safety.joint[0].max_speed_rad_s = 0.17453293f, /* 10 deg/s. */"
NEW = b".s3.safety.joint[0].max_speed_rad_s = 0.26179939f, /* 15 deg/s trial. */"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if FIRMWARE.drive.upper() != "H:" or output == BUILD_ROOT or BUILD_ROOT not in output.parents:
        parser.error("Candidate output must be below firmware/build/ on H:")
    if output.exists():
        parser.error("Output already exists; choose a new snapshot path")

    temp = BUILD_ROOT / "temp-s4-r4-3-speed15"
    temp.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"TEMP": str(temp), "TMP": str(temp), "TMPDIR": str(temp),
                "PYTHONDONTWRITEBYTECODE": "1"})
    subprocess.run([sys.executable, "-B", str(HERE / "prepare_candidate.py"),
                    "--output", str(output)], check=True, env=env)

    site_path = output / SITE
    original = site_path.read_bytes()
    if original.count(OLD) != 1 or NEW in original:
        raise RuntimeError("Unexpected original 10 deg/s trial profile")
    candidate = original.replace(OLD, NEW, 1)
    site_path.write_bytes(candidate)

    manifest_path = output / "source-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = [item for item in manifest["inputs"] if item["path"] == SITE]
    if len(entries) != 1 or entries[0]["materialized_sha256"] != digest(original):
        raise RuntimeError("Base R4-3 snapshot manifest does not match site")
    entries[0]["materialized_sha256"] = digest(candidate)
    manifest["purpose"] = "R4-3 supervised 15 deg/s speed-gate trial; no hardware action"
    manifest["r4_3_overlay"]["unchanged_limits"] = (
        "At first overlay: 20-30 deg; 20 mNm rotor cap; 10 deg/s; 1 s ramp; "
        "speed superseded by r4_3_speed15_overlay"
    )
    manifest["r4_3_speed15_overlay"] = {
        "script_sha256": digest(Path(__file__).read_bytes()),
        "site_before_sha256": digest(original),
        "site_after_sha256": digest(candidate),
        "from": OLD.decode("ascii"),
        "to": NEW.decode("ascii"),
        "reason": "User selected a bounded 15 deg/s exploratory trial after repeated "
                  "12.7 and 13.3 deg/s pre-start speed trips; physical source unknown",
        "other_limits": "20-30 deg; 20 mNm rotor cap; 10 s session; 1 s ramp",
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps({"output": str(output), "site_sha256": digest(candidate),
                      "manifest_sha256": digest(manifest_path.read_bytes()),
                      "speed_limit_rad_s": 0.26179939,
                      "speed_limit_deg_s": 15}, ensure_ascii=False))


if __name__ == "__main__":
    main()
