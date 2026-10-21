"""S5 FOLLOW live window: one bounded session, three-panel angle/torque/force plot.

The window owns the VCP and runs the same preflight/prepare/READY/start/stop
sequence as dual_follow_once.py. It never flashes, resets or changes parameters,
and it keeps one session per launch because the fault latch needs an MCU reset.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import queue
import re
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parents[1]
BENCH = FIRMWARE / "docs" / "bench" / "s5-elbow-once-20261011"
sys.path.insert(0, str(BENCH))
from elbow_once import Link, TrialError, open_vcp, require  # noqa: E402

try:
    from tkinter import messagebox
except Exception:  # headless --offline-check never imports tkinter
    messagebox = None

SH_ROTOR_PER_JOINT = 8.0
EL_KT_NM_PER_A = 0.020
WINDOW_S = 60.0
POLL_MS = 50
FORCE_EVERY = 5

PHASE_TEXT = {0: "禁用", 1: "待机", 2: "准备中", 3: "READY", 4: "运行中",
              5: "停止中", 7: "已停止", 8: "故障"}

SAMPLE_FIELDS = ["t_s", "phase", "gate", "reason", "safety", "q_sh_deg", "q_el_deg",
                 "sh_rotor_mNm", "sh_joint_Nm", "el_mA", "el_Nm", "paused",
                 "allowed", "remaining_ms"]
FORCE_FIELDS = ["t_s", "raw", "force_N", "valid", "sat", "age_ms"]


def num(field_map, key, scale=1.0):
    value = field_map.get(key)
    if value is None or value in ("na", "range"):
        return None
    try:
        return float(value) * scale
    except ValueError:
        return None


def parse_fields(reply):
    return dict(re.findall(r"(\w+)=([^ ]+)", reply))


def preflight(link):
    require(link.ask("ping") == "OK pong", "ping 回复异常")
    status = parse_fields(link.ask("status"))
    require(status.get("output_allowed") == "0", "输出已经处于许可状态")
    can = parse_fields(link.ask("canstatus"))
    for bus, baud in (("can1", "500000"), ("can2", "1000000")):
        require(can.get(bus + "_started") == "1" and can.get(bus + "_baud") == baud
                and can.get(bus + "_start_error") == "0",
                f"{bus} 未按配置启动")
    shoulder_bus = parse_fields(link.ask("shoulder"))
    require(shoulder_bus.get("bus_off") == "0", "CAN1 处于 bus-off")
    shoulder = parse_fields(link.ask("shoulderfeedback"))
    require(shoulder.get("hb") == "1" and shoulder.get("state") == "1"
            and shoulder.get("error") == "00000000" and shoulder.get("fault") == "0",
            "肩心跳不是 IDLE 或无故障")
    elbow = parse_fields(link.ask("elbowfeedback"))
    age = elbow.get("age_ms", "")
    require(elbow.get("seen") == "1" and elbow.get("valid") == "1"
            and elbow.get("online") == "1" and age.isdecimal() and int(age) <= 150,
            "C620 反馈缺失或过期")
    mode = parse_fields(link.ask("followstatus"))
    require(mode.get("s5") == "1" and mode.get("axes") == "3", "当前不是 R5 双轴镜像")
    require(mode.get("phase") == "1" and mode.get("output_allowed") == "0"
            and mode.get("stop_failed") == "0", "R5 会话不在待机状态")


class Worker(threading.Thread):
    """Owns the serial port for one bounded session; never retries on its own."""

    def __init__(self, args, outdir, events, stop_event):
        super().__init__(daemon=True)
        self.args = args
        self.outdir = outdir
        self.events = events
        self.stop_event = stop_event
        self.samples = 0
        self.peak = [0.0, 0.0]
        self.pauses = 0
        self.result = {}
        self.stage_name = "idle"
        self.started_utc = datetime.now(timezone.utc).isoformat()

    def emit(self, **event):
        event.setdefault("utc", datetime.now(timezone.utc).isoformat())
        try:
            self.events.put_nowait(event)
        except queue.Full:
            try:
                self.events.get_nowait()
            except queue.Empty:
                pass
            try:
                self.events.put_nowait(event)
            except queue.Full:
                pass

    def stage(self, stage, text):
        self.stage_name = stage
        self.emit(kind="stage", stage=stage, text=text)

    def status(self, link, timeout_s=0.3):
        reply = link.ask("followstatus", timeout_s=timeout_s)
        return parse_fields(reply)

    def run(self):
        try:
            port = open_vcp(self.args.port)
        except Exception as exc:
            self.emit(kind="error", text=f"打不开 {self.args.port}：{exc}")
            return
        journal_path = self.outdir / "session.jsonl"
        samples_path = self.outdir / "samples.csv"
        force_path = self.outdir / "force.csv"
        started = None
        error = None
        try:
            with journal_path.open("x", encoding="utf-8", newline="\n") as journal, \
                    samples_path.open("x", encoding="utf-8", newline="") as samples_file, \
                    force_path.open("x", encoding="utf-8", newline="") as force_file:
                link = Link(port, journal)
                samples_csv = csv.DictWriter(samples_file, fieldnames=SAMPLE_FIELDS)
                force_csv = csv.DictWriter(force_file, fieldnames=FORCE_FIELDS)
                samples_csv.writeheader()
                force_csv.writeheader()
                started = time.monotonic()
                try:
                    self.session(link, samples_csv, force_csv)
                except (TrialError, OSError) as exc:
                    error = f"{type(exc).__name__}: {exc}"
                    self.emit(kind="error", text=error)
                    self.best_effort_stop(link)
                self.result.update(samples=self.samples, peak_deg_s=self.peak,
                                   speed_pauses=self.pauses, error=error)
        except Exception as exc:  # file or port failures must surface in the window
            self.emit(kind="error", text=f"{type(exc).__name__}: {exc}")
        finally:
            try:
                port.close()
            except Exception:
                pass
            summary = dict(self.result)
            summary.update(started_utc=self.started_utc, board_bin_sha256=self.args.board_bin_sha256,
                           duration_ms=self.args.duration_ms, port=self.args.port)
            try:
                (self.outdir / "summary.json").write_text(
                    json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            except OSError:
                pass
            self.emit(kind="finished", text="会话结束")

    def session(self, link, samples_csv, force_csv):
        self.stage("preflight", "预检中")
        preflight(link)
        self.stage("preparing", "准备中：等待两轴取零")
        require(link.ask("followprepare") == "OK S5 preparing output_allowed=0", "followprepare 被拒绝")
        deadline = time.monotonic() + 20.0
        while True:
            mode = self.status(link)
            require(mode.get("phase") in ("2", "3"), "准备阶段异常退出")
            if mode.get("zero_sh") == "1" and mode.get("zero_el") == "1":
                break
            require(time.monotonic() < deadline, "两轴取零超时")
            time.sleep(0.05)
        while True:
            mode = self.status(link)
            require(mode.get("phase") in ("2", "3"), "READY 之前离开准备阶段")
            if (mode.get("phase") == "3" and mode.get("gate") == "5"
                    and mode.get("q_sh_mrad") not in (None, "na")
                    and mode.get("q_el_mrad") not in (None, "na")):
                break
            require(time.monotonic() < deadline, "READY 超时")
            time.sleep(0.05)
        # The firmware only keeps READY for prepare_timeout_ms (20 s for R5) and
        # treats a late Arm as EXPIRED, so the confirmed button starts at once.
        self.stage("ready", "READY：立即启动")
        if self.stop_event.is_set():
            self.stage("stopping", "已取消准备")
            self.stop_session(link)
            return
        reply = link.ask("followstart", timeout_s=0.3)
        match = re.fullmatch(r"OK S5 started axes=3 expires_in_ms=(\d+); support arm before stop", reply)
        require(match is not None, "followstart 回复异常")
        session_ms = int(match.group(1))
        budget_s = min(self.args.duration_ms / 1000.0, (session_ms - 1500) / 1000.0)
        require(budget_s > 1.0, "会话剩余时间不足以观察")
        self.emit(kind="marker", name="start")
        self.stage("running", f"运行中：{budget_s:.0f} s 后自动停止")
        t0 = time.monotonic()
        next_t = t0
        index = 0
        while not self.stop_event.is_set() and (time.monotonic() - t0) < budget_s:
            mode = self.status(link, timeout_s=0.15)
            elapsed = time.monotonic() - t0
            self.publish_sample(mode, elapsed, samples_csv)
            if index % FORCE_EVERY == 0:
                self.publish_force(link, elapsed, force_csv)
            index += 1
            next_t += POLL_MS / 1000.0
            delay = next_t - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_t = time.monotonic()
        self.stop_session(link)

    def publish_sample(self, mode, elapsed, samples_csv):
        paused = mode.get("pz", "0/0").split("/")[0] == "1"
        peak = mode.get("pk", "0/0").split("/")
        try:
            self.peak = [max(self.peak[0], float(peak[0])), max(self.peak[1], float(peak[1]))]
            self.pauses = max(self.pauses, int(mode.get("pz", "0/0").split("/")[1]))
        except (ValueError, IndexError):
            pass
        sh_rotor = num(mode, "cmd_sh_mNm")
        el_a = num(mode, "cmd_el_mA")
        row = {
            "t_s": f"{elapsed:.3f}",
            "phase": mode.get("phase", "0"),
            "gate": mode.get("gate", "0"),
            "reason": mode.get("reason", "0"),
            "safety": mode.get("safety", "0"),
            "q_sh_deg": self.fmt(num(mode, "q_sh_mrad", 180.0 / math.pi / 1000.0)),
            "q_el_deg": self.fmt(num(mode, "q_el_mrad", 180.0 / math.pi / 1000.0)),
            "sh_rotor_mNm": self.fmt(sh_rotor),
            "sh_joint_Nm": self.fmt(None if sh_rotor is None else sh_rotor / 1000.0 * SH_ROTOR_PER_JOINT),
            "el_mA": self.fmt(el_a),
            "el_Nm": self.fmt(None if el_a is None else el_a / 1000.0 * EL_KT_NM_PER_A),
            "paused": 1 if paused else 0,
            "allowed": mode.get("output_allowed", "0"),
            "remaining_ms": mode.get("remaining_ms", "0"),
        }
        samples_csv.writerow(row)
        self.samples += 1
        self.emit(kind="sample", row=row)

    def publish_force(self, link, elapsed, force_csv):
        try:
            reply = link.ask("hx711", timeout_s=0.15)
        except Exception:
            return  # auxiliary telemetry never aborts the window
        mode = parse_fields(reply)
        row = {"t_s": f"{elapsed:.3f}", "raw": mode.get("raw", "na"),
               "force_N": self.fmt(num(mode, "force_N")), "valid": mode.get("valid", "0"),
               "sat": mode.get("sat", "0"), "age_ms": mode.get("age_ms", "na")}
        force_csv.writerow(row)
        self.emit(kind="force", row=row)

    @staticmethod
    def fmt(value):
        return "" if value is None else f"{value:.4f}"

    def stop_session(self, link):
        self.stage("stopping", "停止中：等待确认")
        accepted = False
        try:
            reply = link.ask("followstop", timeout_s=0.2)
            accepted = reply in ("OK S5 ramping_down; support arm", "OK S5 session_closed; support arm")
        except Exception as exc:
            self.emit(kind="error", text=f"followstop 未确认送达：{exc}")
        final = {}
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            try:
                final = self.status(link, timeout_s=0.2)
            except Exception:
                break
            if final.get("phase") in ("7", "8"):
                break
            time.sleep(0.05)
        confirmed = (final.get("phase") == "7" and final.get("output_allowed") == "0"
                     and final.get("stop_failed") == "0" and final.get("stop") == "1")
        self.result.update(stop_reply_accepted=accepted, final_phase=final.get("phase"),
                           stop_confirmed=confirmed)
        if confirmed:
            self.stage("stopped", "已正常停止（phase=7，输出许可 0）")
        else:
            self.stage("unconfirmed", "停止未确认：保持承托，立即切断 24 V")

    def best_effort_stop(self, link):
        try:
            link.ask("followstop", timeout_s=0.2)
        except Exception:
            pass
        self.stage("unconfirmed", "异常退出：保持承托，立即切断 24 V")


class Window:
    def __init__(self, root, args, outdir):
        import tkinter as tk
        from tkinter import ttk
        self.tk = tk
        self.root = root
        self.args = args
        self.outdir = outdir
        self.events = queue.Queue(maxsize=8192)
        self.stop_event = threading.Event()
        self.worker = None
        self.stage = "idle"
        self.samples = []
        self.force = []
        self.dirty = True
        self.closing = False
        self.error_text = ""
        self.markers = []

        root.title("S5 FOLLOW 实时监控")
        root.geometry("1120x860")
        root.minsize(900, 700)
        root.configure(bg="#141c28")
        root.protocol("WM_DELETE_WINDOW", self.close)

        self.banner = tk.Label(root, text="待机：先点“检查并准备”",
                               font=("Microsoft YaHei UI", 16, "bold"),
                               bg="#1d2a3a", fg="white", anchor="w", padx=12, pady=6)
        self.banner.pack(fill="x")
        self.detail = tk.Label(root, text="未连接 / 未出力", font=("Microsoft YaHei UI", 11),
                               bg="#141c28", fg="#b9cad9", anchor="w", padx=12)
        self.detail.pack(fill="x")

        controls = ttk.Frame(root)
        controls.pack(fill="x", padx=10, pady=6)
        self.confirm = tk.BooleanVar(value=False)
        self.confirm_box = tk.Checkbutton(controls, text="24V 已开、机构已承托、可立即断电",
                                          variable=self.confirm, command=self.refresh_buttons,
                                          bg="#141c28", fg="#dbe7f1", selectcolor="#1d2a3a",
                                          activebackground="#141c28", activeforeground="white",
                                          font=("Microsoft YaHei UI", 10))
        self.confirm_box.grid(row=0, column=0, padx=(0, 12))
        self.prepare_button = ttk.Button(controls, text="检查并开始", command=self.start_prepare)
        self.prepare_button.grid(row=0, column=1, padx=4)
        self.stop_button = ttk.Button(controls, text="停止", command=self.request_stop, state="disabled")
        self.stop_button.grid(row=0, column=2, padx=4)
        self.export_button = ttk.Button(controls, text="导出实验图", command=self.export_figure,
                                        state="disabled")
        self.export_button.grid(row=0, column=3, padx=4)
        self.duration = tk.StringVar(value=str(args.duration_ms // 1000))
        ttk.Label(controls, text="会话秒数").grid(row=0, column=4, padx=(16, 4))
        ttk.Entry(controls, textvariable=self.duration, width=5).grid(row=0, column=5)

        self.canvas = tk.Canvas(root, bg="#0f1620", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True, padx=10, pady=(0, 8))
        self.footer = tk.Label(root, text="", font=("Microsoft YaHei UI", 9),
                               bg="#141c28", fg="#8fa6b8", anchor="w", padx=12)
        self.footer.pack(fill="x")

        self.refresh_buttons()
        root.after(50, self.poll)
        root.after(120, self.redraw)

    def refresh_buttons(self):
        running = self.worker is not None and self.worker.is_alive()
        self.stop_button.configure(state="normal" if running else "disabled")
        self.prepare_button.configure(
            state="disabled" if running or not self.confirm.get() else "normal")
        self.export_button.configure(
            state="normal" if not running and self.samples else "disabled")

    def start_prepare(self):
        if self.worker is not None and self.worker.is_alive():
            return
        try:
            seconds = int(self.duration.get())
        except ValueError:
            self.error_text = "会话秒数必须是整数"
            return
        if not 2 <= seconds <= 60:
            self.error_text = "会话秒数需在 2–60 s（固件上限 45 s，脚本留 1.5 s 停止余量）"
            return
        self.args.duration_ms = min(seconds * 1000, 43000)
        self.outdir.mkdir(parents=True, exist_ok=True)
        self.stop_event.clear()
        self.samples.clear()
        self.force.clear()
        self.markers.clear()
        self.error_text = ""
        self.stage = "preflight"
        self.worker = Worker(self.args, self.outdir, self.events, self.stop_event)
        self.worker.started_utc = datetime.now(timezone.utc).isoformat()
        self.worker.start()
        self.refresh_buttons()

    def export_figure(self):
        script = HERE / "figure_session.py"
        try:
            result = subprocess.run([sys.executable, str(script), str(self.outdir)],
                                    capture_output=True, text=True, timeout=180)
        except Exception as exc:
            messagebox.showerror("导出失败", str(exc))
            return
        if result.returncode != 0:
            messagebox.showerror("导出失败", (result.stderr or result.stdout)[-800:])
            return
        messagebox.showinfo("已导出", f"{self.outdir / 'figure'}\nfigure.pdf / .svg / .png")

    def request_stop(self):
        self.stop_event.set()

    def close(self):
        if self.worker is not None and self.worker.is_alive():
            self.closing = True
            self.stop_event.set()
            self.stage = "stopping"
            self.banner.configure(text="正在停止，等待确认…")
            return
        self.root.destroy()

    def poll(self):
        for _ in range(512):
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            kind = event.get("kind")
            if kind == "sample":
                self.samples.append(event["row"])
                self.dirty = True
            elif kind == "force":
                self.force.append(event["row"])
                self.dirty = True
            elif kind == "stage":
                self.stage = event.get("stage", self.stage)
                self.banner.configure(text=event.get("text", ""))
                if self.stage in ("stopped", "unconfirmed"):
                    self.banner.configure(bg="#1d5c37" if self.stage == "stopped" else "#7a1f1f")
                    if self.samples:
                        self.markers.append(float(self.samples[-1]["t_s"]))
                elif self.stage == "ready":
                    self.banner.configure(bg="#1d4a6b")
                elif self.stage == "running":
                    self.markers.append(float(self.samples[-1]["t_s"]) if self.samples else 0.0)
                self.dirty = True
                self.refresh_buttons()
            elif kind == "marker":
                if event.get("name") == "start":
                    self.markers.append(float(self.samples[-1]["t_s"]) if self.samples else 0.0)
                    self.dirty = True
            elif kind == "error":
                self.error_text = event.get("text", "")
                self.banner.configure(text="错误：" + self.error_text, bg="#7a1f1f")
                self.dirty = True
            elif kind == "finished":
                self.refresh_buttons()
        if self.closing and (self.worker is None or not self.worker.is_alive()):
            self.root.destroy()
            return
        self.root.after(50, self.poll)

    def redraw(self):
        if self.dirty:
            self.draw()
            self.dirty = False
        self.root.after(120, self.redraw)

    def draw(self):
        c = self.canvas
        c.delete("all")
        width = max(c.winfo_width(), 400)
        height = max(c.winfo_height(), 320)
        left, right, top, bottom, gap = 74, 16, 10, 26, 12
        panel_h = (height - top - bottom - 2 * gap) / 3.0
        plot_w = width - left - right
        t_end = float(self.samples[-1]["t_s"]) if self.samples else 0.0
        t_max = WINDOW_S if t_end <= WINDOW_S else t_end
        t_min = t_max - WINDOW_S

        def x_of(t):
            return left + (t - t_min) / (t_max - t_min) * plot_w

        panels = [
            ("角度 (deg)", [("q_sh_deg", "#4fd1c5", "肩"), ("q_el_deg", "#f6ad55", "肘")], -40.0, 130.0),
            ("助力力矩 (N·m)", [("sh_joint_Nm", "#68d391", "肩"), ("el_Nm", "#f687b3", "肘")], -0.3, 1.5),
            ("HX711 力 (N)", [("force_N", "#f6e05e", "力")], -30.0, 30.0),
        ]
        for index, (title, series, low, high) in enumerate(panels):
            p_top = top + index * (panel_h + gap)
            p_bottom = p_top + panel_h
            self.draw_bands(c, left, plot_w, p_top, p_bottom, t_min, t_max)
            collected = []
            for key, color, label in series:
                points = []
                rows = self.samples if key != "force_N" else self.force
                for row in rows:
                    value = row.get(key)
                    if value in (None, "", "na"):
                        continue
                    t = float(row["t_s"])
                    if t < t_min or t > t_max:
                        continue
                    points.append((t, float(value)))
                collected.append((points, color))
            values = [value for points, _ in collected for _, value in points]
            if values:
                low = min(low, min(values) * 1.1)
                high = max(high, max(values) * 1.1)
            for points, color in collected:
                if len(points) > 2:
                    step = max(1, len(points) // 450)
                    points = points[::step] + [points[-1]]
                coords = []
                for t, value in points:
                    coords.extend((x_of(t), self.y_of(value, low, high, p_top, p_bottom)))
                if len(coords) >= 4:
                    c.create_line(*coords, fill=color, width=2)
                elif len(coords) == 2:
                    c.create_oval(coords[0] - 2, coords[1] - 2, coords[0] + 2, coords[1] + 2,
                                  fill=color, outline="")
            c.create_text(left + 6, p_top + 4, anchor="nw", text=title,
                          fill="#cfe0ee", font=("Microsoft YaHei UI", 10, "bold"))
            legend = "  ".join(f"{label} {color}" for _, color, label in series)
            c.create_text(left + plot_w - 6, p_top + 4, anchor="ne", text=legend,
                          fill="#7f97ab", font=("Consolas", 8))
            c.create_line(left, p_bottom, left + plot_w, p_bottom, fill="#2b3a4b")
            for frac in (0.0, 0.5, 1.0):
                y = p_bottom - frac * (p_bottom - p_top)
                c.create_text(left - 8, y, anchor="e", text=f"{low + (high - low) * frac:6.2f}",
                              fill="#7f97ab", font=("Consolas", 8))
                c.create_line(left, y, left + plot_w, y, fill="#1c2836")

        for marker in self.markers:
            x = x_of(marker)
            if left <= x <= left + plot_w:
                c.create_line(x, top, x, height - bottom, fill="#e2e8f0", dash=(4, 3))

        tick = 10.0
        start = math.ceil(t_min / tick) * tick
        while start <= t_max:
            x = x_of(start)
            c.create_text(x, height - bottom + 12, text=f"{start:.0f}s",
                          fill="#7f97ab", font=("Consolas", 8))
            start += tick

        detail = f"样本 {len(self.samples)}  力样本 {len(self.force)}  阶段 {PHASE_TEXT.get(int(self.samples[-1]['phase']) if self.samples else 1, '—')}"
        if self.samples:
            last = self.samples[-1]
            detail += f"  pz={last['paused']}  allowed={last['allowed']}  remaining={last['remaining_ms']} ms"
        if self.force:
            detail += f"  力={self.force[-1]['force_N'] or 'na'} N"
        if self.error_text:
            detail += "  |  " + self.error_text
        self.detail.configure(text=detail)
        self.footer.configure(text=f"记录目录：{self.outdir}")

    @staticmethod
    def y_of(value, low, high, top, bottom):
        if high <= low:
            return bottom
        ratio = (value - low) / (high - low)
        ratio = min(max(ratio, 0.0), 1.0)
        return bottom - ratio * (bottom - top)

    def draw_bands(self, canvas, left, plot_w, top, bottom, t_min, t_max):
        if not self.samples:
            return
        colors = {"assist": "#16301f", "pause": "#2a2a22", "idle": "#131a24"}
        run_start = None
        run_kind = None
        previous_t = None
        for row in self.samples:
            t = float(row["t_s"])
            if t < t_min:
                continue
            if t > t_max:
                break
            kind = "idle"
            if row["phase"] == "4":
                kind = "pause" if row["paused"] == "1" else "assist"
            if run_kind is None:
                run_kind, run_start = kind, t
            elif kind != run_kind:
                self.band_rect(canvas, left, plot_w, top, bottom, run_start, t, t_min, t_max, colors[run_kind])
                run_kind, run_start = kind, t
            previous_t = t
        if run_kind is not None and previous_t is not None:
            self.band_rect(canvas, left, plot_w, top, bottom, run_start, previous_t, t_min, t_max, colors[run_kind])

    @staticmethod
    def band_rect(canvas, left, plot_w, top, bottom, t0, t1, t_min, t_max, color):
        if t1 <= t0:
            return
        x0 = left + (t0 - t_min) / (t_max - t_min) * plot_w
        x1 = left + (t1 - t_min) / (t_max - t_min) * plot_w
        canvas.create_rectangle(x0, top, max(x1, x0 + 1), bottom, fill=color, outline="")


def load_replay(directory):
    samples, force = [], []
    with (directory / "samples.csv").open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            samples.append(row)
    force_path = directory / "force.csv"
    if force_path.is_file():
        with force_path.open(encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                force.append(row)
    return samples, force


class FakePort:
    """In-memory board for --offline-check; no hardware is touched."""

    def __init__(self):
        self.pending = ""
        self.phase = 1
        self.prepare_reads = 0
        self.running_reads = 0
        self.stop_reads = 0
        self.commands = []
        self.timeout = 0.35
        self.write_timeout = 0.35

    def write(self, data):
        self.pending = data.decode("ascii").strip()
        self.commands.append(self.pending)
        if self.pending == "followprepare":
            self.phase = 2
        elif self.pending == "followstart":
            self.phase = 4
        elif self.pending == "followstop":
            self.phase = 7 if self.phase != 8 else 8
        return len(data)

    def close(self):
        pass

    def read_until(self, _terminator, _limit):
        command = self.pending
        if command == "ping":
            reply = "OK pong"
        elif command == "status":
            reply = "OK stage=S2_CAN_RX output_allowed=0"
        elif command == "canstatus":
            reply = ("OK can1_started=1 can1_baud=500000 can1_start_error=0 "
                     "can2_started=1 can2_baud=1000000 can2_start_error=0")
        elif command == "shoulder":
            reply = "OK bus=shoulder bus_off=0 err=0"
        elif command == "shoulderfeedback":
            reply = "OK shoulder hb=1 state=1 error=00000000 fault=0"
        elif command == "elbowfeedback":
            reply = "OK bus=elbow motor_id=1 seen=1 valid=1 online=1 age_ms=1"
        elif command == "followprepare":
            reply = "OK S5 preparing output_allowed=0"
        elif command == "followstart":
            reply = "OK S5 started axes=3 expires_in_ms=45000; support arm before stop"
        elif command == "followstop":
            reply = "OK S5 ramping_down; support arm"
        elif command == "hx711":
            reply = "OK hx711 seen=1 seq=1 raw=140000 sat=0 age_ms=20 valid=1 force_N=0.500 zero_raw=144811 cal=temporary"
        elif command == "followstatus":
            reply = self.followstatus()
        else:
            reply = "ERR unknown"
        return (reply + "\r\n").encode("ascii")

    def followstatus(self):
        phase, gate, allowed, zero, q, paused, stop = "1", "0", "0", "0", "na", "0", "0"
        if self.phase == 2:
            self.prepare_reads += 1
            phase = "2"
            if self.prepare_reads >= 3:
                zero, q = "1", "0"
            if self.prepare_reads >= 6:
                phase, gate = "3", "5"
        elif self.phase == 4:
            self.running_reads += 1
            phase, gate, allowed, zero = "4", "2", "1", "1"
            q = str(300 + self.running_reads)
            paused = "1" if 20 <= self.running_reads < 26 else "0"
            stop = "0"
        elif self.phase == 7:
            self.stop_reads += 1
            phase, gate, stop = "7", "0", "1"
            q = "300"
        return (f"OK s5=1 axes=3 phase={phase} gate={gate} reason=0 safety=0 "
                f"q_sh_mrad={q} q_el_mrad={q} cmd_sh_mNm=120 cmd_el_mA=800 "
                f"seq_sh=1 seq_el=1 zero_sh={zero} zero_el={zero} remaining_ms=40000 "
                f"output_allowed={allowed} stop={stop} stop_failed=0 "
                f"pz={paused}/{1 if paused == '1' else 0} pk=20/60")


def offline_check(outdir):
    events = queue.Queue(maxsize=8192)
    stop_event = threading.Event()
    args = argparse.Namespace(port="FAKE", duration_ms=1500, board_bin_sha256=None)
    worker = Worker(args, outdir, events, stop_event)
    original = globals()["open_vcp"]
    globals()["open_vcp"] = lambda _name: FakePort()
    try:
        worker.start()
        worker.join(15.0)
    finally:
        globals()["open_vcp"] = original
    require(not worker.is_alive(), "offline worker did not finish")
    rows = list(csv.DictReader((outdir / "samples.csv").open(encoding="utf-8")))
    force = list(csv.DictReader((outdir / "force.csv").open(encoding="utf-8")))
    require(len(rows) >= 10, "offline samples missing")
    require(any(row["paused"] == "1" for row in rows), "offline pause band never appeared")
    require(any(row["q_sh_deg"] not in ("", "na") for row in rows), "offline angle missing")
    require(force and force[0]["force_N"] != "", "offline force missing")
    summary = json.loads((outdir / "summary.json").read_text(encoding="utf-8"))
    require(summary["stop_confirmed"], "offline stop not confirmed")
    print(f"offline ok: samples={len(rows)} force={len(force)} "
          f"pauses={summary['speed_pauses']} stop_confirmed={summary['stop_confirmed']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="显式 VCP，例如 COM10；不自动扫描")
    parser.add_argument("--output-dir", type=Path, help="记录目录，默认 firmware/build/live-<时间>")
    parser.add_argument("--duration-ms", type=int, default=40000, help="会话观察时长，默认 40000")
    parser.add_argument("--board-bin-sha256", help="板上镜像 BIN 的 SHA256（可选，写入 summary）")
    parser.add_argument("--offline-check", action="store_true", help="内置假串口，不接触硬件")
    parser.add_argument("--replay", type=Path, help="回放已保存的会话目录")
    args = parser.parse_args()

    if args.replay:
        samples, force = load_replay(args.replay)
        require(samples, "回放目录没有样本")
        import tkinter as tk
        root = tk.Tk()
        window = Window(root, args, args.replay)
        window.samples, window.force = samples, force
        window.stage = "stopped"
        window.banner.configure(text=f"回放：{args.replay.name}", bg="#1d4a6b")
        window.prepare_button.configure(state="disabled")
        window.stop_button.configure(state="disabled")
        window.dirty = True
        root.mainloop()
        return 0

    if args.offline_check:
        outdir = args.output_dir or (FIRMWARE / "build" / ("live-offline-" + datetime.now().strftime("%Y%m%d-%H%M%S")))
        outdir.mkdir(parents=True, exist_ok=True)
        offline_check(outdir)
        return 0

    require(args.port, "实机运行需要 --port")
    outdir = args.output_dir or (FIRMWARE / "build" / ("live-" + datetime.now().strftime("%Y%m%d-%H%M%S")))
    outdir.mkdir(parents=True, exist_ok=True)
    import tkinter as tk
    root = tk.Tk()
    Window(root, args, outdir)
    root.mainloop()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (TrialError, OSError) as exc:
        print(f"ABORT: {exc}", file=sys.stderr)
        sys.exit(1)
