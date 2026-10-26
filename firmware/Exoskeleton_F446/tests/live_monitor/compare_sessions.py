"""Compare two saved sessions: cuff force against shoulder angle, assist on vs off.

Each session is one live-monitor directory. Force samples are matched to the
nearest joint sample, then binned by shoulder angle so the two runs can be
compared at the same pose even if the operator held slightly different angles.

Outputs compare.png / .pdf / .svg and compare.json in --out.
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
from matplotlib import font_manager  # noqa: E402

TEXT = {
    "zh": {"force": "连接力 (N)", "angle": "肩关节角 (°)", "on": "开助力",
           "off": "关助力", "delta": "力差 (N)", "pose": "姿态 (°)",
           "title": "同姿态有/无助力对比"},
    "en": {"force": "Cuff force (N)", "angle": "Shoulder angle (deg)", "on": "assist on",
           "off": "assist off", "delta": "Force difference (N)", "pose": "Pose (deg)",
           "title": "Same pose with and without assist"},
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


def pairs(directory, assist=None):
    """(shoulder angle, cuff force) pairs from one session, RUNNING samples only.

    assist=None keeps every sample; "1"/"0" selects the runtime assist gate,
    which lets one session provide both the with- and without-assist data.
    """
    samples = [r for r in read_csv(directory / "samples.csv") if r.get("phase") == "4"]
    if assist is not None:
        samples = [r for r in samples if (r.get("assist") or "1") == assist]
    force = read_csv(directory / "force.csv")
    if not samples or not force:
        return []
    times = [float(r["t_s"]) for r in samples]
    angles = [number(r, "q_sh_deg") for r in samples]
    out = []
    index = 0
    for row in force:
        t = number(row, "t_s")
        value = number(row, "force_N")
        if t is None or value is None:
            continue
        while index + 1 < len(times) and abs(times[index + 1] - t) <= abs(times[index] - t):
            index += 1
        if abs(times[index] - t) > 0.15 or angles[index] is None:
            continue
        out.append((angles[index], value))
    return out


def binned(data, width):
    bins = {}
    for angle, value in data:
        key = round(angle / width) * width
        bins.setdefault(key, []).append(value)
    return {key: sum(values) / len(values) for key, values in bins.items() if values}


def at_pose(data, pose, tolerance):
    values = [value for angle, value in data if abs(angle - pose) <= tolerance]
    return (sum(values) / len(values)) if values else None


def cjk_font():
    for name in ("Microsoft YaHei", "SimHei", "Noto Sans CJK SC"):
        try:
            font_manager.findfont(name, fallback_to_default=False)
            return name
        except Exception:
            continue
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("on_dir", type=Path)
    parser.add_argument("off_dir", type=Path, nargs="?",
                        help="省略时按 samples.csv 的 assist 列在同一会话内分段")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--poses", nargs="*", type=float, default=[],
                        help="在图上单独标注的姿态角度，例如 45 75")
    parser.add_argument("--tolerance", type=float, default=4.0,
                        help="姿态取值的角度容差，默认 ±4°")
    parser.add_argument("--bin", type=float, default=3.0, help="角度分箱宽度，默认 3°")
    parser.add_argument("--lang", choices=("zh", "en"), default="zh")
    args = parser.parse_args()

    text = TEXT[args.lang]
    if args.lang == "zh":
        font = cjk_font()
        if font:
            plt.rcParams["font.sans-serif"] = [font]
        plt.rcParams["axes.unicode_minus"] = False
    plt.rcParams.update({"font.size": 9, "axes.linewidth": 0.8,
                         "xtick.direction": "in", "ytick.direction": "in"})

    if args.off_dir is None:
        on_pairs = pairs(args.on_dir, "1")
        off_pairs = pairs(args.on_dir, "0")
        source = {"mode": "single_session", "directory": str(args.on_dir)}
    else:
        on_pairs = pairs(args.on_dir)
        off_pairs = pairs(args.off_dir)
        source = {"mode": "two_sessions", "on": str(args.on_dir), "off": str(args.off_dir)}
    if not on_pairs or not off_pairs:
        raise SystemExit("至少一个会话缺少 RUNNING 样本或力数据")

    on_bins = binned(on_pairs, args.bin)
    off_bins = binned(off_pairs, args.bin)

    fig, axes = plt.subplots(1, 2, figsize=(7.0, 3.2), gridspec_kw={"width_ratios": [2, 1]})
    ax, bar = axes
    if on_bins:
        ax.plot(sorted(on_bins), [on_bins[k] for k in sorted(on_bins)],
                color="#2f855a", marker="o", ms=3, lw=1.0, label=text["on"])
    if off_bins:
        ax.plot(sorted(off_bins), [off_bins[k] for k in sorted(off_bins)],
                color="#b7791f", marker="s", ms=3, lw=1.0, label=text["off"])
    ax.set_xlabel(text["angle"])
    ax.set_ylabel(text["force"])
    ax.grid(True, color="#dddddd", lw=0.5)
    ax.legend(loc="best", fontsize=7, frameon=False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    poses = args.poses or sorted(set(on_bins) & set(off_bins))
    deltas, labels = [], []
    for pose in poses:
        on_value = at_pose(on_pairs, pose, args.tolerance)
        off_value = at_pose(off_pairs, pose, args.tolerance)
        if on_value is None or off_value is None:
            continue
        deltas.append(on_value - off_value)
        labels.append(f"{pose:g}")
    if deltas:
        colors = ["#2f855a" if value <= 0 else "#c53030" for value in deltas]
        bar.bar(labels, deltas, color=colors)
        bar.axhline(0.0, color="#999999", lw=0.8)
        bar.set_xlabel(text["pose"])
        bar.set_ylabel(text["delta"])
        bar.set_title(text["title"], fontsize=8)
        bar.spines["top"].set_visible(False)
        bar.spines["right"].set_visible(False)

    outdir = args.out or (args.on_dir / "compare")
    outdir.mkdir(parents=True, exist_ok=True)
    outputs = []
    for suffix in ("pdf", "svg", "png"):
        path = outdir / f"compare.{suffix}"
        fig.savefig(path, dpi=300 if suffix == "png" else None, bbox_inches="tight")
        outputs.append(path)
    plt.close(fig)

    meta = dict(source, on=str(args.on_dir), off=str(args.off_dir or ""),
                on_pairs=len(on_pairs), off_pairs=len(off_pairs),
                poses=[{"pose_deg": pose,
                        "on_N": at_pose(on_pairs, pose, args.tolerance),
                        "off_N": at_pose(off_pairs, pose, args.tolerance)}
                       for pose in poses],
                outputs=[str(p) for p in outputs])
    (outdir / "compare.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n",
                                         encoding="utf-8")
    print(json.dumps(meta["poses"], ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
