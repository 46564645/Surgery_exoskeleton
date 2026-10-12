"""Materialize one H: isolated R4-3 gain-0.36 candidate, offline only.

Start from the gain-0.18 snapshot materializer, then double its assist gain,
rotor command cap and normal withdrawal slew in the isolated S4 site. The
20-30 degree envelope, 15 degree/s speed gate, 20 mNm/s rise, 10 s session
and 1 s ramp remain unchanged. This script never builds or accesses a device.
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
CHANGES = (
    (b".assist_gain = 0.18f,", b".assist_gain = 0.36f,", "assist gain"),
    (b".s3.safety.joint[0].max_motor_command = 0.020f,",
     b".s3.safety.joint[0].max_motor_command = 0.040f,", "rotor cap"),
    (b".release_slew_nm_s = 0.040f,",
     b".release_slew_nm_s = 0.080f,", "normal withdrawal slew"),
    (b"Runtime range and 20 mNm gates remain.",
     b"Runtime range and 40 mNm gates remain.", "peak gate comment"),
)
UNCHANGED = (
    b".s3.safety.joint[0].lower_rad = 0.34906585f,",
    b".s3.safety.joint[0].upper_rad = 0.52359878f,",
    b".s3.safety.joint[0].max_speed_rad_s = 0.26179939f,",
    b".s3.safety.joint[0].motor_slew_per_s = 0.020f,",
    b".session_ms = 10000U,",
    b".ramp_max_ms = 1000U,",
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def replace_once(data: bytes, old: bytes, new: bytes, label: str) -> bytes:
    if data.count(old) != 1 or new in data:
        raise RuntimeError(f"Unexpected {label} source layout")
    return data.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if FIRMWARE.drive.upper() != "H:" or output == BUILD_ROOT or BUILD_ROOT not in output.parents:
        parser.error("Candidate output must be below firmware/build/ on H:")
    if output.exists():
        parser.error("Output already exists; choose a new snapshot path")

    temp = BUILD_ROOT / "temp-s4-r4-3-gain36"
    temp.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"TEMP": str(temp), "TMP": str(temp), "TMPDIR": str(temp),
                "PYTHONDONTWRITEBYTECODE": "1"})
    subprocess.run([sys.executable, "-B", str(HERE / "prepare_gain18_candidate.py"),
                    "--output", str(output)], check=True, env=env)

    site_path = output / SITE
    original = site_path.read_bytes()
    for marker in UNCHANGED:
        if original.count(marker) != 1:
            raise RuntimeError("Base gain18 limits do not match the expected profile")
    candidate = original
    for old, new, label in CHANGES:
        candidate = replace_once(candidate, old, new, label)

    manifest_path = output / "source-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = [item for item in manifest["inputs"] if item["path"] == SITE]
    base_overlay = manifest.get("r4_3_gain18_overlay")
    if (len(entries) != 1 or entries[0]["materialized_sha256"] != digest(original)
            or not isinstance(base_overlay, dict)
            or base_overlay.get("site_after_sha256") != digest(original)
            or base_overlay.get("assist_gain_to") != 0.18):
        raise RuntimeError("Base gain18 manifest does not match S4 site")

    site_path.write_bytes(candidate)
    entries[0]["materialized_sha256"] = digest(candidate)
    manifest["purpose"] = "R4-3 gain36 isolated candidate; offline only; no hardware action"
    manifest["r4_3_gain36_overlay"] = {
        "script_sha256": digest(Path(__file__).read_bytes()),
        "source_materializer": "prepare_gain18_candidate.py",
        "site_before_sha256": digest(original),
        "site_after_sha256": digest(candidate),
        "parameter_changes": {
            "assist_gain": {"from": 0.18, "to": 0.36},
            "max_motor_command_nm": {"from": 0.020, "to": 0.040},
            "release_slew_nm_s": {"from": 0.040, "to": 0.080},
        },
        "comment_change": "runtime range and 40 mNm gates",
        "unchanged_limits": "20-30 deg; 15 deg/s; 20 mNm/s rotor rise; 10 s session; 1 s ramp",
        "operation": "isolated snapshot only; no build, flash or device access",
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps({"output": str(output), "site_sha256": digest(candidate),
                      "manifest_sha256": digest(manifest_path.read_bytes()),
                      "assist_gain": 0.36, "rotor_cap_nm": 0.040,
                      "release_slew_nm_s": 0.080}, ensure_ascii=False))


if __name__ == "__main__":
    main()
