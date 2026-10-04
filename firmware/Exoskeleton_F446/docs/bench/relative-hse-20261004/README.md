# Archived HSE relative bench profile

This archive reproduces the source configuration used for the accepted2026-10-04 supervised shoulder trial. It does not alter the default firmware: the normal checkout keeps HSI and a disabled site. No output command,flash command ordevice access exists in the materializer.

The only runtime source/configuration overlays are `Core/Src/main.c`, `Exoskeleton_F446.ioc` and `App/application/s3_site.c`. The manifest checks every build input before writing a new separate directory. The historical site startsSAFE_OFF but permits the recorded shoulder preparation/pulse after all gates; building the archive does not authorize another hardware action.

From this directory, with Python3 and a new destination:

```text
python materialize.py --output /path/to/new/relative-hse-source
```

Put ARM GCC13.3.Rel1 and Ninja1.13.2 on PATH, then configure/build with CMake4.3.1:

```text
cmake -S /path/to/new/relative-hse-source -B /path/to/new/relative-hse-source/build/Debug -G Ninja -DCMAKE_TOOLCHAIN_FILE=/path/to/new/relative-hse-source/cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build /path/to/new/relative-hse-source/build/Debug
arm-none-eabi-objcopy -O binary /path/to/new/relative-hse-source/build/Debug/Exoskeleton_F446.elf /path/to/new/relative-hse-source/build/Debug/Exoskeleton_F446.bin
```

ExpectedBIN:91148bytes,SHA256`7f844000b67d9880106a54b2f3fcd3f4487e10666ad2756036fbbdfc0bff6740`. RAM10600bytes. DebugELF includes source/build paths and is not required to retain the historical ELF hash. The ELF actually flashed was`762c0902a4269f3056c32e75b2cb99039d9a45f22678aac41364c111500bcf54`. The source-derived reconstructed BIN is checked independently before publication; see`reproduction-result.json`.

S2 acceptance covers continuous credible shoulder encoder feedback and one real change. Dropout/recovery was canceled and not executed. S3 covers only this single bounded trial and stop; velocity direction,calibrated joint displacement and remainingS3 acceptance were not established. [Trial evidence](../../../tests/s3_capture/evidence-relative-20261004T013713353068Z/acceptance.json) contains the recorded limits. FullFlash backups,binaries,tool installations and private machine presets are excluded.
