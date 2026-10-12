"""Materialize one H: isolated R4-3 candidate with doubled assist gain.

The existing 20 mNm rotor cap, 20-30 degree envelope, 15 degree/s speed
limit, session, and stop timing remain unchanged. No device access occurs.
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
OLD_GAIN = b".assist_gain = 0.09f,"
NEW_GAIN = b".assist_gain = 0.18f,"
OLD_GATE = b"""    /* The gateway faults rather than clips an oversized FOLLOW candidate.
     * Reject a model/gain combination whose worst-case rotor command can
     * exceed the independently confirmed motor-side limit. */
    double peak_motor_nm = (double)p->gravity.amplitude_nm * (double)p->assist_gain
        / (double)p->s3.safety.joint[0].mapping.rotor_per_joint;
"""
NEW_GATE = b"""    /* The gateway faults rather than clips an oversized FOLLOW candidate.
     * For a one-sided interval wholly within the monotonic sine quadrant,
     * bound the model at its endpoint. All other ranges retain the more
     * conservative full-wave bound. Runtime range and 20 mNm gates remain. */
    const SafetyJointConfig *joint = &p->s3.safety.joint[0];
    double lower = (double)joint->lower_rad - (double)p->gravity.gravity_zero_rad;
    double upper = (double)joint->upper_rad - (double)p->gravity.gravity_zero_rad;
    double peak_fraction = 1.0;
    const double half_pi = 1.57079632679489661923;
    if (isfinite(lower) && isfinite(upper) && lower >= 0.0 && upper <= half_pi) {
        peak_fraction = sin(upper);
    } else if (isfinite(lower) && isfinite(upper)
               && lower >= -half_pi && upper <= 0.0) {
        peak_fraction = -sin(lower);
    }
    double peak_motor_nm = (double)p->gravity.amplitude_nm * (double)p->assist_gain
        * peak_fraction / (double)joint->mapping.rotor_per_joint;
"""


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

    temp = BUILD_ROOT / "temp-s4-r4-3-gain18"
    temp.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"TEMP": str(temp), "TMP": str(temp), "TMPDIR": str(temp),
                "PYTHONDONTWRITEBYTECODE": "1"})
    subprocess.run([sys.executable, "-B", str(HERE / "prepare_speed15_candidate.py"),
                    "--output", str(output)], check=True, env=env)

    site_path = output / SITE
    original = site_path.read_bytes()
    candidate = replace_once(original, OLD_GAIN, NEW_GAIN, "gain")
    candidate = replace_once(candidate, OLD_GATE, NEW_GATE, "range peak gate")
    site_path.write_bytes(candidate)

    manifest_path = output / "source-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = [item for item in manifest["inputs"] if item["path"] == SITE]
    if len(entries) != 1 or entries[0]["materialized_sha256"] != digest(original):
        raise RuntimeError("Base speed15 manifest does not match S4 site")
    entries[0]["materialized_sha256"] = digest(candidate)
    manifest["purpose"] = "R4-3 doubled-gain isolated bench candidate; no hardware action"
    manifest["r4_3_gain18_overlay"] = {
        "script_sha256": digest(Path(__file__).read_bytes()),
        "site_before_sha256": digest(original),
        "site_after_sha256": digest(candidate),
        "assist_gain_from": 0.09,
        "assist_gain_to": 0.18,
        "bounded_peak_rule": "one-sided monotonic sine quadrant, else full-wave bound",
        "unchanged_limits": "20-30 deg; 20 mNm rotor cap; 15 deg/s; 10 s session; 1 s ramp",
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps({"output": str(output), "site_sha256": digest(candidate),
                      "manifest_sha256": digest(manifest_path.read_bytes()),
                      "assist_gain": 0.18, "rotor_cap_nm": 0.020}, ensure_ascii=False))


if __name__ == "__main__":
    main()
