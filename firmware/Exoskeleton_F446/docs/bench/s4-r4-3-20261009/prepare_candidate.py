"""Materialize an isolated R4-3 candidate; never touches the active firmware.

The first-window materializer supplies the unchanged, hash-checked HSE trial
snapshot. This script changes only the FOLLOW session and property-reader
windows, then updates the snapshot manifest before an ordinary offline build.
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
FIRST_WINDOW = FIRMWARE / "docs/bench/s4-first-window-20261009"
BUILD_ROOT = (FIRMWARE / "build").resolve()
SITE = "App/application/s4_site.c"
CHANGES = (
    (b".s3.shoulder_property_window_ms = 30000U,",
     b".s3.shoulder_property_window_ms = 35000U,"),
    (b".session_ms = 3000U,", b".session_ms = 10000U,"),
)


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

    temp = BUILD_ROOT / "temp-s4-r4-3"
    temp.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"TEMP": str(temp), "TMP": str(temp), "TMPDIR": str(temp),
                "PYTHONDONTWRITEBYTECODE": "1"})
    base_script = FIRST_WINDOW / "materialize.py"
    subprocess.run([sys.executable, "-B", str(base_script), "--output", str(output)],
                   check=True, env=env)

    site_path = output / SITE
    original = site_path.read_bytes()
    candidate = original
    for old, new in CHANGES:
        if candidate.count(old) != 1 or new in candidate:
            raise RuntimeError(f"Unexpected profile source layout: {old!r}")
        candidate = candidate.replace(old, new, 1)
    site_path.write_bytes(candidate)

    manifest_path = output / "source-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = [item for item in manifest["inputs"] if item["path"] == SITE]
    if len(entries) != 1 or entries[0]["materialized_sha256"] != digest(original):
        raise RuntimeError("Base snapshot manifest does not match S4 site")
    entries[0]["materialized_sha256"] = digest(candidate)
    manifest["purpose"] = "R4-3 bounded two-posture and drift candidate; no hardware action"
    manifest["r4_3_overlay"] = {
        "script_sha256": digest(Path(__file__).read_bytes()),
        "site_before_sha256": digest(original),
        "site_after_sha256": digest(candidate),
        "changes": [
            {"from": old.decode("ascii"), "to": new.decode("ascii")}
            for old, new in CHANGES
        ],
        "unchanged_limits": "20-30 deg; 20 mNm rotor cap; 10 deg/s; 1 s ramp",
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps({"output": str(output), "site_sha256": digest(candidate),
                      "manifest_sha256": digest(manifest_path.read_bytes()),
                      "session_ms": 10000, "reader_window_ms": 35000},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
