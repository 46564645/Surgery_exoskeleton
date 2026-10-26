"""Build a publication figure from one saved live-monitor session.

Reads samples.csv / force.csv / summary.json, draws the three-panel
angle / assist-torque / force time series with assist and soft-pause windows
shaded, and writes figure.pdf, figure.svg and figure.png.

The live window keeps its own tkinter preview; this script is the printable
version: same data, fixed fonts and sizes, vector output.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from svg_layers import single_curve_layer
from matplotlib import font_manager  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402

ASSIST_COLOR = "#e3f2e9"
PAUSE_COLOR = "#efefef"
BOOST_COLOR = "#dbe9f6"

TEXT = {
    "zh": {
        "angle": "关节角度 (°)",
        "torque": "助力力矩 (N·m)",
        "force": "连接力 (N)",
        "time": "时间 (s)",
        "shoulder": "肩",
        "elbow": "肘",
        "force_label": "HX711",
        "assist": "助力生效",
        "pause": "软限暂停",
        "fault": "安全停机",
        "title": "S5 FOLLOW 关节角、助力力矩与连接力",
        "angle_shoulder": "肩关节角 (°)",
        "angle_elbow": "肘关节角 (°)",
        "angle_forearm": "前臂角（肩+肘） (°)",
        "model": "理论 sin 曲线",
        "boost": "力触发增量",
    },
    "en": {
        "angle": "Joint angle (deg)",
        "torque": "Assist torque (N·m)",
        "force": "Cuff force (N)",
        "time": "Time (s)",
        "shoulder": "Shoulder",
        "elbow": "Elbow",
        "force_label": "HX711",
        "assist": "assist on",
        "pause": "soft pause",
        "fault": "safety stop",
        "title": "S5 FOLLOW: joint angle, assist torque and cuff force",
        "angle_shoulder": "Shoulder joint angle (deg)",
        "angle_elbow": "Elbow joint angle (deg)",
        "angle_forearm": "Forearm angle, shoulder + elbow (deg)",
        "model": "ideal sin model",
        "boost": "force boost",
    },
}


def read_csv(path):
    if not path.is_file():
        return []
    with path.open(encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def number(row, key):
    value = row.get(key, "")
    if value in ("", None, "na"):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def series(rows, key):
    out = []
    for row in rows:
        value = number(row, key)
        if value is not None:
            out.append((float(row["t_s"]), value))
    return out


def runs(rows):
    """Contiguous (start, end, kind) spans for the shading."""
    spans = []
    current = None
    start = None
    for row in rows:
        phase = row.get("phase", "")
        kind = None
        if phase == "4":
            if row.get("force_active") == "1":
                kind = "boost"
            else:
                kind = "pause" if row.get("paused") == "1" else "assist"
        if kind != current:
            if current is not None and start is not None:
                spans.append((start, float(row["t_s"]), current))
            current, start = kind, float(row["t_s"])
    if current is not None and rows:
        spans.append((start, float(rows[-1]["t_s"]), current))
    return spans


def cjk_font():
    for name in ("Microsoft YaHei", "SimHei", "Noto Sans CJK SC", "Source Han Sans SC"):
        try:
            font_manager.findfont(name, fallback_to_default=False)
            return name
        except Exception:
            continue
    return None


def build(directory, outdir, size, dpi, lang, window, title, full=False):
    samples = read_csv(directory / "samples.csv")
    if not samples:
        raise SystemExit("samples.csv 为空或不存在")
    force = read_csv(directory / "force.csv")
    summary = {}
    if (directory / "summary.json").is_file():
        summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))

    fault_at = None
    for row in samples:
        if row.get("phase") == "8":
            fault_at = float(row["t_s"])
            break
    running = [r for r in samples if r.get("phase") == "4"]
    running_end = float(running[-1]["t_s"]) if running else float(samples[-1]["t_s"])
    if window:
        lo, hi = window
    elif full:
        lo, hi = 0.0, float(samples[-1]["t_s"])
    else:
        # Default: only the powered session. After a stop or fault the firmware
        # repeats the last joint state, which would otherwise look like a held pose.
        lo, hi = 0.0, running_end
    samples = [r for r in samples if lo <= float(r["t_s"]) <= hi]
    force = [r for r in force if lo <= float(r["t_s"]) <= hi]
    if not samples:
        raise SystemExit("指定时间窗内没有样本")

    text = TEXT[lang]
    if lang == "zh":
        font = cjk_font()
        if font:
            plt.rcParams["font.sans-serif"] = [font]
        plt.rcParams["axes.unicode_minus"] = False
    plt.rcParams.update({"font.size": 9, "axes.linewidth": 0.8,
                         "xtick.direction": "in", "ytick.direction": "in"})

    fig, axes = plt.subplots(3, 1, figsize=size, sharex=True)
    fig.subplots_adjust(left=0.21, right=0.97, top=0.86 if title else 0.88,
                    bottom=0.09, hspace=0.12)
    ax_angle, ax_torque, ax_force = axes

    for start, end, kind in runs(samples):
        if kind is None or end <= start:
            continue
        color = {"assist": ASSIST_COLOR, "pause": PAUSE_COLOR, "boost": BOOST_COLOR}[kind]
        for axis in axes:
            axis.axvspan(start, end, color=color, linewidth=0, zorder=0)

    sh_angle = series(samples, "q_sh_deg")
    el_angle = series(samples, "q_el_deg")
    sh_torque = series(samples, "sh_joint_Nm")
    el_torque = series(samples, "el_Nm")
    force_series = series(force, "force_N")

    if sh_angle:
        ax_angle.plot(*zip(*sh_angle), color="#1f6fb2", lw=1.2, label=text["shoulder"])
    if el_angle:
        ax_angle.plot(*zip(*el_angle), color="#d9822b", lw=1.2, label=text["elbow"])
    if sh_torque:
        ax_torque.plot(*zip(*sh_torque), color="#2f855a", lw=1.2, label=text["shoulder"])
    if el_torque:
        ax_torque.plot(*zip(*el_torque), color="#b83280", lw=1.2, label=text["elbow"])
    if force_series:
        ax_force.plot(*zip(*force_series), color="#b7791f", lw=1.2, label=text["force_label"])

    for axis, label in ((ax_angle, text["angle"]), (ax_torque, text["torque"]),
                        (ax_force, text["force"])):
        axis.set_ylabel(label)
        axis.grid(True, color="#dddddd", lw=0.5, zorder=1)
        axis.legend(loc="upper right", fontsize=7, frameon=False, ncol=2)
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)
    ax_force.set_xlabel(text["time"])
    ax_force.set_xlim(float(samples[0]["t_s"]), float(samples[-1]["t_s"]))

    if fault_at is not None and lo - 0.05 <= fault_at <= hi + 0.5:
        marker = min(fault_at, float(samples[-1]["t_s"]))
        for axis in axes:
            axis.axvline(marker, color="#c53030", lw=1.0, ls="--", zorder=2)
        ax_angle.annotate(text["fault"], xy=(marker, 1.0),
                          xycoords=("data", "axes fraction"), xytext=(-4, -10),
                          textcoords="offset points", color="#c53030",
                          fontsize=7, ha="right")

    handles = [Patch(color=ASSIST_COLOR, label=text["assist"]),
               Patch(color=PAUSE_COLOR, label=text["pause"]),
               Patch(color=BOOST_COLOR, label=text["boost"])]
    legend_y = 0.955 if title else 0.995
    fig.legend(handles=handles, loc="upper center", ncol=3, frameon=False,
               fontsize=7, bbox_to_anchor=(0.58, legend_y), columnspacing=1.2,
               handlelength=1.4)
    if title:
        fig.suptitle(title, fontsize=9, y=0.995)

    outdir.mkdir(parents=True, exist_ok=True)
    outputs = []
    for suffix in ("pdf", "svg", "png"):
        path = outdir / f"figure.{suffix}"
        fig.savefig(path, dpi=dpi if suffix == "png" else None)
        if suffix == "svg":
            single_curve_layer(path)
        outputs.append(path)
    plt.close(fig)

    meta = {"session": str(directory), "samples": len(samples), "force_samples": len(force),
            "duration_s": float(samples[-1]["t_s"]), "fault_at_s": fault_at,
            "outputs": [str(p) for p in outputs], "summary": summary}
    (outdir / "figure.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n",
                                        encoding="utf-8")
    return outputs, meta


def build_xy(directory, outdir, size, dpi, lang, window, title, amplitude, elbow_amplitude):
    """Assist torque against joint angle: the gravity-compensation curve."""
    samples = read_csv(directory / "samples.csv")
    if not samples:
        raise SystemExit("samples.csv 为空或不存在")
    text = TEXT[lang]
    if lang == "zh":
        font = cjk_font()
        if font:
            plt.rcParams["font.sans-serif"] = [font]
        plt.rcParams["axes.unicode_minus"] = False
    plt.rcParams.update({"font.size": 9, "axes.linewidth": 0.8,
                         "xtick.direction": "in", "ytick.direction": "in"})
    if window:
        lo, hi = window
        samples = [r for r in samples if lo <= float(r["t_s"]) <= hi]
    running = [r for r in samples if r.get("phase") == "4"]
    rows = running or samples

    fig, axes = plt.subplots(2, 1, figsize=size)
    fig.subplots_adjust(left=0.19, right=0.97, top=0.90, bottom=0.09, hspace=0.30)
    # The elbow model is a function of the forearm angle, i.e. shoulder + elbow.
    panels = [
        (axes[0], ("q_sh_deg",), "sh_joint_Nm", "#1f6fb2", text["shoulder"],
         text["angle_shoulder"], amplitude),
        (axes[1], ("q_sh_deg", "q_el_deg"), "el_Nm", "#d9822b", text["elbow"],
         text["angle_forearm"], elbow_amplitude),
    ]
    for axis, angle_keys, torque_key, color, label, xlabel, amp in panels:
        xs, ys = [], []
        for row in rows:
            torque = number(row, torque_key)
            parts = [number(row, key) for key in angle_keys]
            if torque is None or any(part is None for part in parts):
                continue
            xs.append(sum(parts))
            ys.append(torque)
        if xs:
            axis.plot(xs, ys, color=color, lw=1.0, label=label)
        if amp:
            import math as _math
            # Keep the model inside the measured span so it never looks extrapolated.
            lo = int(min(xs)) - 3 if xs else -30
            hi = int(max(xs)) + 3 if xs else 105
            grid = [_math.radians(v) for v in range(lo, hi + 1, 1)]
            axis.plot([_math.degrees(a) for a in grid],
                      [amp * _math.sin(a) for a in grid],
                      color="#888888", lw=1.0, ls="--", label=text["model"])
        axis.axhline(0.0, color="#cccccc", lw=0.6)
        axis.set_xlabel(xlabel)
        axis.set_ylabel(text["torque"])
        axis.grid(True, color="#dddddd", lw=0.5)
        axis.legend(loc="upper left", fontsize=7, frameon=False)
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)
    if title:
        fig.suptitle(title, fontsize=8, y=0.975)

    outdir.mkdir(parents=True, exist_ok=True)
    outputs = []
    for suffix in ("pdf", "svg", "png"):
        path = outdir / f"curve.{suffix}"
        fig.savefig(path, dpi=dpi if suffix == "png" else None)
        if suffix == "svg":
            single_curve_layer(path)
        outputs.append(path)
    plt.close(fig)
    meta = {"session": str(directory), "mode": "xy", "samples": len(rows),
            "amplitude_nm": amplitude, "elbow_amplitude_nm": elbow_amplitude,
            "outputs": [str(p) for p in outputs]}
    (outdir / "curve.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n",
                                       encoding="utf-8")
    return outputs, meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="live-<时间戳> 会话目录")
    parser.add_argument("--out", type=Path, help="输出目录，默认 <session>/figure")
    parser.add_argument("--width", type=float, default=3.5, help="图宽 inch，默认 3.5（单栏）")
    parser.add_argument("--height", type=float, default=6.2, help="图高 inch，默认 6.2")
    parser.add_argument("--dpi", type=int, default=300, help="PNG 分辨率，默认 300")
    parser.add_argument("--lang", choices=("zh", "en"), default="zh", help="标注语言")
    parser.add_argument("--window", nargs=2, type=float, metavar=("T0", "T1"),
                        help="只画这段时间（秒）")
    parser.add_argument("--title", default="", help="图标题；留空则不显示")
    parser.add_argument("--mode", choices=("time", "xy"), default="time",
                        help="time=三联时间序列；xy=力矩-角度曲线")
    parser.add_argument("--amplitude", type=float, default=0.0,
                        help="xy 模式下叠加肩理论幅值（N·m），0 表示不画")
    parser.add_argument("--elbow-amplitude", type=float, default=0.0,
                        help="xy 模式下叠加肘理论幅值（N·m），0 表示不画")
    parser.add_argument("--full", action="store_true",
                        help="画出整个记录（默认只画 RUNNING 会话段）")
    args = parser.parse_args()

    outdir = args.out or (args.directory / "figure")
    if args.mode == "xy":
        outputs, meta = build_xy(args.directory, outdir, (args.width, args.height),
                                 args.dpi, args.lang, args.window, args.title,
                                 args.amplitude, args.elbow_amplitude)
    else:
        outputs, meta = build(args.directory, outdir, (args.width, args.height), args.dpi,
                              args.lang, args.window, args.title, args.full)
    print(json.dumps({"outputs": [str(p) for p in outputs],
                      "mode": args.mode,
                      "duration_s": meta.get("duration_s"),
                      "fault_at_s": meta.get("fault_at_s")}, ensure_ascii=False))


if __name__ == "__main__":
    sys.exit(main())
