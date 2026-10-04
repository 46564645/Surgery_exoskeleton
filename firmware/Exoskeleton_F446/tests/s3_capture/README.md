# S3 qualification and one-trial recording

Default execution runs focused offline fake-serial checks. `--help` and imports do
not import pyserial or enumerate ports. The tool reuses the sibling
`../encoder_capture/capture.py` helpers (`Journal`, `parse`, `require`, `uint`,
`forward`) and its serial constructor. Missing sibling source is an error; no
alternate repository version is loaded. The live-only identity helper uses the active Windows device map and Configuration
Manager APIs, with two matching present/healthy reads and the same fixed ST-LINK
serial. This preserves the existing PnP identity contract while avoiding the
Win32_SerialPort WMI provider timeout observed on this computer. The helper runs
in a subprocess bounded to 5 seconds; it never opens the serial port.

```powershell
python -B capture.py
python -B capture.py --help
```

`qualify` is one read-only CAN qualification, followed by cached `qualfield 0..6`
records. Preflight requires production SAFE_OFF, configured/transport/output=0,
healthy CAN/UART and a fresh reported IDLE heartbeat. Qualification has a strict
2-second phase limit, 12-second overall serial budget and at most 256 sequential
UART queries. CAN is still limited by firmware to seven reads; queries do not
extend that window. Success includes the raw request/reply bytes, endpoint,
decoded value and device timestamps, with final IDLE after the last reply.
`cancelled=1` on COMPLETE means the firmware successfully closed its read owner.
Failure permits one `qualcancel` and a cached `qualerror` read, never output.

Qualification permanently owns SDO for that boot. Trial must use a different boot
with `qualstatus` still unused. A previous qualification record is a compatibility
and timing reference; it is never fresh calibration, mode or stop evidence.

Future live invocations require a separately confirmed powered window:

```powershell
python -B capture.py --live --mode qualify --powered-window-confirmed --evidence <new-directory>
python -B capture.py --live --mode trial --powered-window-confirmed --plan <approved-plan.json> --confirm-plan-sha256 <reviewed-file-sha256> --evidence <new-directory>
```

No live invocation has been performed during this preparation. Trial is disabled
without an explicitly approved/enabled frozen plan. All three firmware groups
(`s3limits`, `s3timing`, `s3stopcfg`) must match it exactly, including float32 HEX.
The action comparison uses the firmware int-to-float32/divide-to-float32 path.
Measured qualification cycle C must meet `max(period,C)+C+service` freshness and
the initial/prepare/arm/TTL reader window. Stop window also includes a conservative
new IDLE-heartbeat wait of `feedback` milliseconds, then dwell and cycle margins.
These checks reject an insufficient plan; they do not enlarge parameters.

Prepare is attempted once. READY and current zero/calibration/mode properties are
recorded as separate complete sequences. A fresh pre-arm position/velocity baseline
is recorded independently; the final reader-window/healthy heartbeat check follows
it. Arm then immediately issues the only pulse with its returned token, with no
intervening query, sleep or user wait. No auto-increase, retry, reset, acknowledgement
or recovery is available.

`coords=3` is an explicit relative native-rotor trial. Its frozen plan requires
`direction=1`, `ratio=1` and float32 `zero=00000000`; coords1/2 remain supported.
Only this trial path queries `s3anchor`: phase0 before prepare, phase2 after READY
and before arm, then phase3 after the completed stop. The exact reply fields are
`coords phase pos seq q r begin now out`. `pos` is the origin in native rotor rev,
encoded as float32 HEX, and `seq` identifies the complete seven-property batch.
The position request/reply must be strictly after this prepare's `begin`, causal,
and within the reply budget. Current READY and baseline snapshots prove freshness
separately: the latched origin is historical and is not required to stay fresh.
After stop, origin bits, sequence, request/reply and begin timestamps must remain
identical, with phase3 and out=0. Changed or reset anchors end the flow and permit
only the existing one cancel attempt. Qualification and legacy trial paths send
no `s3anchor`; this adds no device operation to default offline execution.

High-frequency recording queries only `s3feedback`, accepting distinct fresh seqs
and rejecting changed duplicates, stale/future/invalid fields. Position/velocity,
property and reader replies are never spliced into a fictitious common sequence.
After out=0, feedback recording continues between reader polls until stop completion.
Final success requires accepted stop/purge, a post-event observed IDLE stop, out=0
and the expected FAULT3/EXPIRED4 latch from TTL. COMPLETE can legitimately report
historical `observed=1` while current fresh/stationary=0. No ackfault is sent.

`success` for trial means the bounded action/stop flow completed. Sample sufficiency,
numeric range and physical variation acceptance are separate `motion_evidence`
fields. Too few samples do not erase completed stop evidence and never cause a
second action. Historical caches remain recorded with their own freshness flag;
they cannot count as new fresh pairs. Any other first fault ends the flow and
permits at most one cancel attempt. Replies are single sequential ASCII lines of
at most 256 bytes, with 350ms reads/writes clipped to the remaining phase budget.
The serial handle is closed and `events.jsonl` plus `summary.json` are retained.

This template is deliberately disabled and has no numeric physical defaults:

```json
{
  "version": 1,
  "mode": "trial",
  "approved": false,
  "enabled": false,
  "qualification": {"summary": null, "sha256": null},
  "s3limits": {
    "axes": null, "policy": null, "coords": null, "direction": null,
    "ratio": null, "zero": null, "lo": null, "hi": null,
    "speed": null, "limit": null, "slew": null
  },
  "s3timing": {
    "feedback": null, "service": null, "max_request": null,
    "prepare": null, "reply": null, "period": null, "window": null
  },
  "s3stopcfg": {"span": null, "speed": null, "dwell": null, "fresh": null, "window": null},
  "pulse": {"signed_mNm": null, "ttl_ms": null},
  "bounds": {"total_seconds": null, "prepare_seconds": null, "stop_seconds": null, "max_queries": null}
}
```

Every group value in an enabled plan is the exact CLI string. Float fields use
eight uppercase HEX digits. The approved plan and referenced real qualification
summary are SHA256-bound before any serial import/enumeration. Numeric values in
`test_offline.py` are synthetic fixtures only and are not approved device settings.


Focused Link integrity regression (offline, fake serial only):
`python -B tests/s3_capture/test_link_integrity.py -v`

Unchanged read/write timeout values no longer invoke pyserial setters, which
reconfigure the Windows port. Changed values still use the original remaining
deadline calculations, including recalculation after a write. A malformed/Non-OK
reply retains its raw log and latches the link unusable for further normal queries;
the existing one bounded cleanup remains available. No reply repair or retry is
introduced. These contracts do not establish why the live reply lost its first O,
and have not been validated by another live window.
