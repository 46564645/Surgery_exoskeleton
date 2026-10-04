S2 encoder capture helper (Windows)

Run only against the reviewed S2 bounded-stream firmware. Existing firmware
without encfeedback fails closed. The script neither flashes nor resets.

1. With USB connected and external power OFF, main thread runs:
   powershell -NoProfile -File .\capture.ps1
   Default mode is prepare. It reads passive status only, verifies the unique
   healthy VCP under ST-LINK SN 066EFF555748874987182240, and preserves the
   separate legacy 029 shoulderfeedback observation. It sends no encstart,
   encstop, one-shot encoder request, movement command, or parameter write.

2. For a future approved live window, use the LOCAL visible window:
   powershell -NoProfile -File .\visible_capture.ps1 -Live -PoweredWindowConfirmed
   Both flags require prior main-thread confirmation. The user then clicks
   Start once. The default invocation without flags only opens a WAIT window;
   it starts no child process, enumerates no device and opens no serial port.

3. The local window shows green only after this owned session's real
   READY_FOR_MOVEMENT matches at least two fresh, advancing valid samples.
   The user makes one small manual variation only while this live green
   prompt is shown. Do not relay a short sampling window through delayed
   agent notifications. Late/wrong-session events, invalid samples, ABORT,
   COMPLETE, process exit, a clock jump or 200 ms sample/message expiry
   revoke green; a 25 ms UI timer checks freshness. Revocation is latched.
   Firmware keeps its separate 100 ms pair and 250 ms heartbeat rules.

4. The stop-prompt button immediately withdraws permission to move manually;
   it does NOT claim to cancel CAN reads instantly. The owned child remains
   until its existing bounded <=30 s session and encstop/finally cleanup end.
   Closing the window waits for cleanup. There is no restart/reset/retry.
   Firmware permits one stream per MCU boot; the Oct 3 failed session has
   consumed that opportunity. A new live window is NOT currently ready.

Low-level capture.ps1 still supports prepare/capture and emits raw JSON for
records; it does not create a user-visible window itself. The visible host
uses it unchanged, including fixed device binding and preflight checks.

Each invocation creates a new evidence-* directory here. Every flushed event
contains session_id (that evidence directory name). The visible window keeps the owned process alive through cleanup.
binding.json contains
fixed-device binding; events.jsonl preserves raw replies (hex and text), host
UTC/monotonic timestamps, samples and the immediate readiness event;
summary.json contains first/last CAN counters, distinct observed encoder pair
timestamps/counters/validity, measured position min/max/range/net change and
signed velocity relationships during observed moving intervals. Host sampling
can skip MCU pairs; counts never imply that those skipped samples were saved.
success/session_completed_without_fault describes protocol/session completion.
physical_variation_acceptance always remains review_required: the main thread
must compare the actual user's movement with measured ranges, distinct pair
timestamps and signed velocity relationships. Nonzero float changes alone do
not pass physical variation acceptance. No amplitude acceptance threshold or
hardware-success conclusion is invented.
Legacy 029 zeros remain separate and never establish encoder movement.

Offline validation (opens no serial port and imports no pyserial):
   H:\app\python.exe -B .\test_offline.py
Fixtures use COM999 and synthetic replies, never a real VCP. H:\app\python.exe
and the existing read-only pyserial 3.5 path are used; no package installation.
For a copied repository version, pyserial is first found relative to this
script's repository ancestors. Optional wrapper parameters -PythonExecutable
and -SerialModuleDirectory override local defaults; Python also supports
--serial-module-directory. No workspace D: path is embedded in either script.

Hardware preparation: 2026-10-02 17:02:02 UTC default prepare passed on the
reviewed stream image (ELF e8466a15caad32216d94a927cb14f336c5ab17b6f7601e211dbd695379ad381e).
It confirmed output_allowed=0 and unused stream active=0/pairs=0/tx=0, without
starting CAN reads. The first powered capture on 2026-10-03 aborted after 4.952 s on a new
shoulder CAN bit-recessive error (0x40). Neither continuous nor manual
variation acceptance passed. No manual movement was instructed. See ../../docs/s2_encoder_stream.md for evidence and acceptance scope.
