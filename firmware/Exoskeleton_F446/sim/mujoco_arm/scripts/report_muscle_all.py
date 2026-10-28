#!/usr/bin/env python3
"""One command for the whole muscle-driven result set.

Runs the static holds, the bursts and the scene stills with the muscle-driven
model and collects everything in one directory with an index:

  static/muscle_static.png    activation and torque per surgical posture
  burst/muscle_burst.png      activation and joint velocity in the first burst
  scenes/scene_*_sh*_el*.png  the model at the three postures
  report.png                  the two result figures stacked
  README.md                   what each file is and which settings produced it

Usage (from firmware/Exoskeleton_F446):
    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\report_muscle_all.py
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

SIM_ROOT = Path(__file__).resolve().parents[1]
FW_ROOT = SIM_ROOT.parents[1]
sys.path.insert(0, str(SIM_ROOT))

from sim.controller import Controller  # noqa: E402

POSTURES = ((20, 100), (30, 110), (60, 70))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    out_dir = Path(args.out) if args.out else FW_ROOT / "build" / "sim-runs" / \
        datetime.now().strftime("report-muscle-%Y%m%d-%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)
    python = sys.executable

    static_dir = out_dir / "static"
    burst_dir = out_dir / "burst"
    scene_dir = out_dir / "scenes"

    print("[1/3] static holds ...")
    subprocess.run([python, str(SIM_ROOT / "scripts" / "muscle_static.py"),
                    "--out", str(static_dir)], check=True)
    print("[2/3] bursts ...")
    subprocess.run([python, str(SIM_ROOT / "scripts" / "muscle_burst.py"),
                    "--out", str(burst_dir)], check=True)
    print("[3/3] scenes ...")
    for shoulder, elbow in POSTURES:
        subprocess.run([python, str(SIM_ROOT / "scripts" / "plot_scene.py"),
                        "--pose", f"{shoulder},{elbow}", "--out", str(scene_dir)],
                       check=True, stdout=subprocess.DEVNULL)

    # Stack the two result figures into one image for quick sharing.
    try:
        from PIL import Image
        static_png = static_dir / "muscle_static.png"
        burst_png = burst_dir / "muscle_burst.png"
        images = [Image.open(p) for p in (static_png, burst_png) if p.exists()]
        if images:
            width = max(i.width for i in images)
            scaled = [i.resize((width, int(i.height * width / i.width))) for i in images]
            combined = Image.new("RGB", (width, sum(i.height for i in scaled)), "white")
            y = 0
            for image in scaled:
                combined.paste(image, (0, y))
                y += image.height
            combined.save(out_dir / "report.png")
            print(f"wrote {out_dir / 'report.png'}")
    except Exception as error:      # a missing figure must not fail the report
        print(f"combined image skipped: {error}")

    site = Controller().site
    (out_dir / "README.md").write_text(
        "\n".join([
            "# Muscle-driven result set",
            "",
            f"Generated {datetime.now():%Y-%m-%d %H:%M}.",
            "",
            "Firmware settings read back from the control library:",
            "",
            f"- send interval {site['send_interval_ms']} ms",
            f"- shoulder gain {site['shoulder_gain']:.2f}, rotor cap "
            f"{site['shoulder_cap_rotor_nm'] * 1000:.0f} mNm",
            f"- elbow gravity amplitude {site['elbow_gravity_amplitude_nm']:.3f} N*m",
            f"- shoulder Amplify {site['amplify_increment_rotor_nm'] * 1000:.0f} mNm",
            "",
            "| File | Content |",
            "|---|---|",
            "| `report.png` | static and burst figures stacked |",
            "| `static/muscle_static.png` | activation and human torque per posture (C0/C1/C2) |",
            "| `static/muscle_static.json` | the numbers behind it |",
            "| `burst/muscle_burst.png` | activation and joint velocity in the first burst |",
            "| `burst/muscle_burst.json` | burst metrics including positive work |",
            "| `scenes/` | the model at P1 (20/100), P4 (30/110), P5 (60/70) |",
            "",
            "All numbers are model estimates: the physical parameters and the",
            "human controller are assumptions, see `model/params.json`.",
        ]), encoding="utf-8")
    print(f"\nwrote {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
