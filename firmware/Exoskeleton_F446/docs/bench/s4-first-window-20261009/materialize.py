"""Copy the current firmware into a new H: build directory with two isolated S4 overlays.

This script has no device, flash, reset, serial, or CAN operation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


PROFILE_DIR = Path(__file__).resolve().parent
FIRMWARE = PROFILE_DIR.parents[2]
ARCHIVE = FIRMWARE / "docs/bench/relative-hse-20261004/overlay"
TREES = ("App", "Core", "Drivers", "cmake", "third_party")
ROOT_FILES = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "Exoskeleton_F446.ioc",
    "startup_stm32f446xx.s",
    "STM32F446xx_FLASH.ld",
    "experiments/can_blackbox/experiment.cmake",
)
SITE_PATTERN = re.compile(rb"static const S4SiteProfile site = \{.*?^\};", re.MULTILINE | re.DOTALL)
ARCHIVE_SHA256 = {
    "Core/Src/main.c": "af17810070557fda31586d51ffd336ec0175e7f84f895e64eeca0ab0fc5d085e",
    "Exoskeleton_F446.ioc": "0985a45495f5c6b0a11c2285189aef5651c76dba1f4e9114f13c7a64ab56009d",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def replace_once(data: bytes, old: bytes, new: bytes, label: str) -> bytes:
    if data.count(old) != 1:
        raise ValueError(f"Expected exactly one {label} in active source")
    return data.replace(old, new, 1)


def hse_main(active: bytes) -> bytes:
    if active.count(b"\r\n") != active.count(b"\n"):
        raise ValueError("Unexpected mixed main.c newlines")
    hsi = (b"  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;\r\n"
           b"  RCC_OscInitStruct.HSIState = RCC_HSI_ON;\r\n"
           b"  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;\r\n")
    hse = (b"  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;\r\n"
           b"  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;\r\n")
    result = replace_once(active, hsi, hse, "HSI oscillator block")
    result = replace_once(result, b"RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;",
                          b"RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;", "PLL source")
    return replace_once(result, b"RCC_OscInitStruct.PLL.PLLM = 16;",
                        b"RCC_OscInitStruct.PLL.PLLM = 8;", "PLL divisor")


def hse_ioc(active: bytes) -> bytes:
    if active.count(b"\r\n") != active.count(b"\n"):
        raise ValueError("Unexpected mixed IOC newlines")
    result = replace_once(active, b",VcooutputI2S\r\n",
                          b",VcooutputI2S,PLLM,PLLSourceVirtual\r\n", "IOC RCC parameters")
    ending = b"boardIOC=true\r\n"
    if not result.endswith(ending):
        raise ValueError("IOC ending changed; review HSE overlay before proceeding")
    return result + (b"PH0-OSC_IN.Mode=HSE-External-Clock-Source\r\n"
                     b"PH1-OSC_OUT.Mode=HSE-External-Clock-Source\r\n"
                     b"RCC.PLLM=8\r\n"
                     b"RCC.PLLSourceVirtual=RCC_PLLSOURCE_HSE\r\n")


def site_overlay(active: bytes, fragment: bytes) -> bytes:
    matches = list(SITE_PATTERN.finditer(active))
    if len(matches) != 1 or len(list(SITE_PATTERN.finditer(fragment))) != 1:
        raise ValueError("S4 site initializer layout changed")
    if (b".s3.safety.enabled_axes = 0U" not in matches[0].group()
            or b".output_enabled = false" not in matches[0].group()
            or b".session_zero_on_prepare = false" not in matches[0].group()):
        raise ValueError("Active S4 site is no longer the output-disabled baseline")
    candidate = SITE_PATTERN.search(fragment).group()
    if (b".output_enabled = true" not in candidate
            or b".session_zero_on_prepare = true" not in candidate
            or b".s3.rotor_reference_confirmed[0] = false" not in candidate):
        raise ValueError("Trial initializer lost its manual-permit or RAM-zero contract")
    return active[:matches[0].start()] + candidate + active[matches[0].end():]


def inputs() -> list[Path]:
    found: list[Path] = []
    for tree in TREES:
        directory = FIRMWARE / tree
        if not directory.is_dir():
            raise FileNotFoundError(directory)
        found.extend(path for path in directory.rglob("*") if path.is_file())
    found.extend(FIRMWARE / item for item in ROOT_FILES)
    for path in found:
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"Missing or linked build input: {path}")
    return sorted(found, key=lambda path: path.relative_to(FIRMWARE).as_posix())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path,
                        help="New directory below this firmware's H: build/ directory")
    args = parser.parse_args()
    build_root = (FIRMWARE / "build").resolve()
    output = args.output.resolve()
    if FIRMWARE.drive.upper() != "H:" or output == build_root or build_root not in output.parents:
        parser.error("This trial materialization must stay in firmware/build/ on H:")
    if output.exists():
        parser.error("Output already exists; this script never overwrites a source snapshot")

    fragment = (PROFILE_DIR / "trial_s4_site_init.inc").read_bytes()
    selected = inputs()
    payload: dict[str, bytes] = {}
    source_hash: dict[str, str] = {}
    for path in selected:
        rel = path.relative_to(FIRMWARE).as_posix()
        data = path.read_bytes()
        payload[rel] = data
        source_hash[rel] = sha256(data)

    payload["App/application/s4_site.c"] = site_overlay(
        payload["App/application/s4_site.c"], fragment)
    for rel, transform in (("Core/Src/main.c", hse_main),
                           ("Exoskeleton_F446.ioc", hse_ioc)):
        archival = (ARCHIVE / rel).read_bytes()
        if sha256(archival) != ARCHIVE_SHA256[rel]:
            raise ValueError(f"Historical HSE reference changed: {rel}")
        payload[rel] = transform(payload[rel])
        if payload[rel] != archival:
            raise ValueError(f"Current {rel} is not the previously verified clock-only HSE overlay")

    output.mkdir(parents=True)
    manifest = {
        "purpose": "Isolated first supervised S4 window candidate; no hardware action",
        "source_root": str(FIRMWARE),
        "source_manifest_scope": "Selected build inputs; no build, tests, captures or private presets",
        "clock_reference": "docs/bench/relative-hse-20261004/overlay, hash-locked",
        "trial_initializer_sha256": sha256(fragment),
        "materializer_sha256": sha256(Path(__file__).read_bytes()),
        "input_count": len(payload),
        "inputs": [
            {"path": rel, "active_sha256": source_hash[rel],
             "materialized_sha256": sha256(data),
             "overlaid": rel in {"App/application/s4_site.c", "Core/Src/main.c",
                                  "Exoskeleton_F446.ioc"}}
            for rel, data in sorted(payload.items())
        ],
    }
    for rel, data in sorted(payload.items()):
        target = output / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (output / "source-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "inputs": len(payload),
                      "manifest": str(output / "source-manifest.json"),
                      "s4_site_sha256": sha256(payload["App/application/s4_site.c"]),
                      "clock": "HSE_BYPASS, 8 MHz nominal, PLLM=8; verified overlay"},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
