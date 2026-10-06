"""Create a checked, disposable HSE build input under build/. No device access.

Reuse the accepted archive's 168 input hashes and three overlays. The only
allowed change to an archived input is the exact optional experiment include.
"""
import argparse
import hashlib
import json
from pathlib import Path

EXTRA = "\n# Temporary diagnostics: deleting this experiment directory is supported.\ninclude(experiments/can_blackbox/experiment.cmake OPTIONAL)\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    experiment = Path(__file__).resolve().parent
    firmware = experiment.parents[1]
    output = args.output.resolve()
    build = (firmware / "build").resolve()
    if build not in output.parents or output.exists():
        parser.error("Choose a NEW directory inside this firmware's build/ directory")
    archive = firmware / "docs/bench/relative-hse-20261004"
    manifest = json.loads((archive / "manifest.json").read_text(encoding="utf-8"))
    files = {}
    for item in manifest["inputs"]:
        source = archive / item["overlay"] if "overlay" in item else firmware / item["path"]
        data = source.read_bytes().replace(b"\r\n", b"\n")
        checked = data
        if item["path"] == "CMakeLists.txt":
            if not data.endswith(EXTRA.encode()):
                raise SystemExit("Unexpected top-level experiment integration; inspect before proceeding")
            checked = data[:-len(EXTRA.encode())]
        if hashlib.sha256(checked).hexdigest() != item["sha256_lf"]:
            raise SystemExit("Archive input changed: " + item["path"])
        files[item["path"]] = data
    for name in ("experiment.cmake", "recorder.c", "recorder.h", "stm32_capture.c"):
        files["experiments/can_blackbox/" + name] = (experiment / name).read_bytes().replace(b"\r\n", b"\n")
    output.mkdir(parents=True)
    for relative, data in files.items():
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (output / "experiment-inputs.json").write_text(json.dumps({
        "archive_inputs_verified": len(manifest["inputs"]),
        "archive_expected_bin_sha256": manifest["original"]["bin_sha256"],
        "sha256_lf": {key: hashlib.sha256(data).hexdigest() for key, data in files.items()},
        "no_device_access": True
    }, indent=2), encoding="utf-8")
    print(f"Verified {len(manifest['inputs'])} archive inputs; wrote {len(files)} files to {output}")


if __name__ == "__main__":
    main()
