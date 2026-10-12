"""Copy the verified speed30 snapshot to one isolated speed40 source tree.

Only the S4 speed gate changes. This performs no build or device action.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parents[2]
BUILD_ROOT = (FIRMWARE / "build").resolve()
BASE = BUILD_ROOT / "s4-r4-3-gain36-speed30-20261011"
BASE_MANIFEST_SHA256 = "b8c0c28716771b1c25803fa6f5b8273fd03d1c9a32d9812f2c2e339c8521b050"
SITE = "App/application/s4_site.c"
OLD = b".s3.safety.joint[0].max_speed_rad_s = 0.52359878f, /* 30 deg/s bounded bench trial. */"
NEW = b".s3.safety.joint[0].max_speed_rad_s = 0.69813170f, /* 40 deg/s bounded bench trial. */"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if (FIRMWARE.drive.upper() != "H:" or output == BUILD_ROOT
            or BUILD_ROOT not in output.parents or output == BASE):
        parser.error("Output must be a new H: firmware/build/ snapshot")
    if output.exists():
        parser.error("Output already exists; choose a new snapshot path")

    manifest_bytes = (BASE / "source-manifest.json").read_bytes()
    if digest(manifest_bytes) != BASE_MANIFEST_SHA256:
        raise RuntimeError("Speed30 source manifest changed")
    prior = json.loads(manifest_bytes)
    if prior.get("input_count") != 182 or len(prior.get("inputs", [])) != 182:
        raise RuntimeError("Unexpected speed30 input inventory")

    verified: list[tuple[str, bytes]] = []
    for entry in prior["inputs"]:
        rel = entry["path"]
        path = (BASE / rel).resolve()
        if BASE.resolve() not in path.parents:
            raise RuntimeError(f"Input escapes source snapshot: {rel}")
        data = path.read_bytes()
        if digest(data) != entry["candidate_sha256"]:
            raise RuntimeError(f"Speed30 input changed: {rel}")
        verified.append((rel, data))
    site_entries = [data for rel, data in verified if rel == SITE]
    if len(site_entries) != 1 or site_entries[0].count(OLD) != 1 or NEW in site_entries[0]:
        raise RuntimeError("Unexpected speed30 S4 site")

    output.mkdir(parents=True)
    inputs = []
    for rel, data in verified:
        candidate = data.replace(OLD, NEW, 1) if rel == SITE else data
        path = output / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(candidate)
        inputs.append({"path": rel, "baseline_sha256": digest(data),
                       "candidate_sha256": digest(candidate), "changed": candidate != data})
    changed = [entry for entry in inputs if entry["changed"]]
    if len(changed) != 1 or changed[0]["path"] != SITE:
        raise RuntimeError("Candidate differs in more than the S4 site")
    manifest = {
        "purpose": "S4 isolated gain36 speed40 candidate; offline only, no hardware action",
        "baseline_root": str(BASE), "candidate_root": str(output),
        "baseline_manifest_sha256": BASE_MANIFEST_SHA256,
        "script_sha256": digest(Path(__file__).read_bytes()),
        "input_count": len(inputs), "changed_inputs": changed, "inputs": inputs,
    }
    manifest_path = output / "source-manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps({"output": str(output), "site_sha256": changed[0]["candidate_sha256"],
                      "manifest_sha256": digest(manifest_path.read_bytes()),
                      "speed_limit_rad_s": 0.69813170, "speed_limit_deg_s": 40,
                      "changed_input_count": 1}, ensure_ascii=False))


if __name__ == "__main__":
    main()
