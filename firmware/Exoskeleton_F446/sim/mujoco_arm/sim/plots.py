"""Figure for one experiment: human effort over time plus a summary bar chart."""

from __future__ import annotations

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

from sim.runner import Run  # noqa: E402

COLOURS = {"C0": "tab:grey", "C1": "tab:orange", "C2": "tab:blue"}
LABELS = {"C0": "C0 bare arm", "C1": "C1 worn, unpowered", "C2": "C2 worn, firmware assist"}


def figure(runs: list[Run], summaries: list[dict], out: Path, title: str) -> Path:
    fig, axes = plt.subplots(2, 2, figsize=(11, 6.5), constrained_layout=True)
    for run in runs:
        a = run.arrays()
        colour = COLOURS.get(run.condition, None)
        label = LABELS.get(run.condition, run.condition)
        axes[0, 0].plot(a["time"], a["human_sh_nm"], color=colour, label=label, lw=1.2)
        axes[0, 1].plot(a["time"], a["human_el_nm"], color=colour, label=label, lw=1.2)
        if run.condition != "C0":
            axes[1, 0].plot(a["time"], a["strap_upper_n"], color=colour, lw=1.0,
                            label=f"{label} upper band")
            axes[1, 0].plot(a["time"], a["strap_forearm_n"], color=colour, lw=1.0, ls="--",
                            label=f"{label} forearm band")
    axes[0, 0].set(title="human shoulder torque", ylabel="N·m", xlabel="t / s")
    axes[0, 1].set(title="human elbow torque", ylabel="N·m", xlabel="t / s")
    axes[1, 0].set(title="band force", ylabel="N", xlabel="t / s")
    axes[1, 0].legend(fontsize=7, loc="upper right")

    # Summary: RMS effort per condition.
    conds = [s["condition"] for s in summaries]
    x = np.arange(len(conds))
    width = 0.38
    sh = [s["shoulder"]["rms"] for s in summaries]
    el = [s["elbow"]["rms"] for s in summaries]
    axes[1, 1].bar(x - width / 2, sh, width, label="shoulder RMS", color="tab:blue")
    axes[1, 1].bar(x + width / 2, el, width, label="elbow RMS", color="tab:green")
    axes[1, 1].set_xticks(x, conds)
    axes[1, 1].set(title="RMS human torque by condition", ylabel="N·m")
    axes[1, 1].legend(fontsize=8)
    for xi, s in zip(x, summaries):
        axes[1, 1].annotate(f"{s['effort_nm_mean']:.2f}", (xi, max(sh + el) * 0.02),
                            ha="center", fontsize=7)
    for ax in (axes[0, 0], axes[0, 1]):
        ax.legend(fontsize=8)
        ax.grid(alpha=0.3)
    fig.suptitle(title)
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out
