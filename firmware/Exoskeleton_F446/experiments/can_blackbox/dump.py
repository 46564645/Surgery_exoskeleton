"""Export a frozen CAN blackbox through existing UART. Sends only bb* commands.

Requires pyserial. No CAN requests, firmware upload, reset or enable command.
Partial raw replies survive errors; port must be selected explicitly.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path


def fields(line):
    return dict(part.split("=", 1) for part in line.split() if "=" in part)


def decode(line):
    result = fields(line)
    esr = int(result["esr"], 16)
    result.update(tec=(esr >> 16) & 255, rec=(esr >> 24) & 255,
                  lec=(esr >> 4) & 7, bus_off=bool(esr & 4))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--out", type=Path, help="New output directory; existing directory is rejected")
    args = parser.parse_args()
    import serial
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder = args.out or Path(__file__).resolve().parent / "captures" / stamp
    folder.mkdir(parents=True, exist_ok=False)
    # Set modem-line preferences before open; no intentional reset pulse.
    # Host/adapter open behavior still requires hardware validation.
    port = serial.Serial(port=None, baudrate=115200, timeout=2, write_timeout=2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with (folder / "raw.jsonl").open("w", encoding="utf-8") as log:
        def ask(command):
            record = {"utc": datetime.now(timezone.utc).isoformat(), "command": command}
            port.write((command + "\n").encode("ascii"))
            raw = port.read_until(b"\n", size=256)
            record["reply_hex"] = raw.hex()
            log.write(json.dumps(record) + "\n")
            log.flush()
            line = raw.decode("ascii", errors="strict")
            if not raw.endswith(b"\r\n") or not line.startswith("OK bb "):
                raise RuntimeError(f"Unexpected/partial reply to {command}; raw evidence saved")
            return line
        try:
            port.open()
            first = fields(ask("bbstatus"))
            if first.get("v") != "1" or first.get("bus") != "1":
                raise RuntimeError("Unsupported recorder format")
            # Explicit freeze is limited to recording RAM; original CAN keeps running.
            ask("bbfreeze")
            ask("bbrewind")
            config = fields(ask("bbconfig"))
            status = fields(ask("bbstatus"))
            count = int(status["count"])
            if status.get("frozen") != "1" or not 0 <= count <= 128:
                raise RuntimeError("Invalid frozen count")
            events = []
            for index in range(count):
                event = decode(ask("bbnext"))
                if int(event["i"]) != index:
                    raise RuntimeError("Recorder cursor mismatch")
                events.append(event)
            if fields(ask("bbnext")).get("end") != "1":
                raise RuntimeError("Missing end marker")
            (folder / "capture.json").write_text(json.dumps({
                "port": args.port, "status": status, "config": config, "events": events,
                "note": "Software observations, millisecond timestamps; not wire frames or proof of causation"
            }, indent=2), encoding="utf-8")
        finally:
            port.close()
    print(f"Saved {count} events to {folder.resolve()}")


if __name__ == "__main__":
    main()
