"""Build a materialized S4 HSE candidate under H: and hash its ELF/BIN.

Only CMake/Ninja/objcopy are invoked. There is no flash or device operation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


FIRMWARE = Path(__file__).resolve().parent.parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(argv: list[str], env: dict[str, str], log: Path) -> str:
    result = subprocess.run(argv, env=env, text=True, errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        raise SystemExit(f"Failed ({result.returncode}): {argv[0]}\n"
                         f"Log: {log}\n{result.stdout[-3000:]}")
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--ninja", required=True, type=Path)
    parser.add_argument("--gcc-bin", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    build_root = (FIRMWARE / "build").resolve()
    if FIRMWARE.drive.upper() != "H:" or build_root not in source.parents:
        parser.error("Source must be a materialized H: firmware/build/ snapshot")
    manifest_path = source / "source-manifest.json"
    if not manifest_path.is_file():
        parser.error("Missing source manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for item in manifest["inputs"]:
        path = source / item["path"]
        if not path.is_file() or digest(path) != item["materialized_sha256"]:
            raise SystemExit(f"Materialized input changed: {path}")
    cmake = args.cmake.resolve()
    ninja = args.ninja.resolve()
    gcc_bin = args.gcc_bin.resolve()
    objcopy = gcc_bin / "arm-none-eabi-objcopy.exe"
    if not cmake.is_file() or not ninja.is_file() or not objcopy.is_file():
        parser.error("CMake, Ninja or ARM objcopy path is invalid")

    build = source / "build/Debug"
    temporary = source / "build/tmp"
    build.mkdir(parents=True, exist_ok=True)
    temporary.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"TEMP": str(temporary), "TMP": str(temporary),
                "TMPDIR": str(temporary)})
    env["PATH"] = os.pathsep.join((str(gcc_bin), str(ninja.parent), env.get("PATH", "")))
    toolchain = source / "cmake/gcc-arm-none-eabi.cmake"
    configure_log = build / "configure.log"
    build_log = build / "build.log"
    configure_args = [
        str(cmake), "-S", str(source), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        "-DCMAKE_BUILD_TYPE=Debug", "-DEXPERIMENT_CAN_BLACKBOX=OFF"
    ]
    # CMake reports a spurious "unused CMAKE_TOOLCHAIN_FILE" advisory when an
    # already configured build tree receives that flag again.
    if not (build / "CMakeCache.txt").is_file():
        configure_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
    configure_output = run(configure_args, env, configure_log)
    build_output = run([str(cmake), "--build", str(build)], env, build_log)
    elf = build / "Exoskeleton_F446.elf"
    binary = build / "Exoskeleton_F446.bin"
    if not elf.is_file():
        raise SystemExit("Build reported success without an ELF")
    run([str(objcopy), "-O", "binary", str(elf), str(binary)],
        env, build / "objcopy.log")
    compiler_warnings = len(re.findall(r"\bwarning:", build_output, re.IGNORECASE))
    cmake_advisories = len(re.findall(r"CMake Warning:", configure_output))
    result = {
        "purpose": "Offline HSE S4 first-window candidate; not flashed or physically tested",
        "source_manifest_sha256": digest(manifest_path),
        "source": str(source),
        "build_type": "Debug",
        "clock": "HSE_BYPASS, nominal 8 MHz, PLLM=8",
        "compiler_warnings_in_this_build": compiler_warnings,
        "cmake_advisories_in_this_configure": cmake_advisories,
        "elf": {"path": str(elf), "bytes": elf.stat().st_size,
                "sha256": digest(elf)},
        "bin": {"path": str(binary), "bytes": binary.stat().st_size,
                "sha256": digest(binary)},
        "logs": {"configure": str(configure_log), "build": str(build_log)},
    }
    result_path = build / "build-result.json"
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(json.dumps({"result": str(result_path),
                      "compiler_warnings": compiler_warnings,
                      "cmake_advisories": cmake_advisories,
                      "elf_sha256": result["elf"]["sha256"],
                      "bin_sha256": result["bin"]["sha256"],
                      "bin_bytes": result["bin"]["bytes"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
