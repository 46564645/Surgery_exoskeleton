"""Visible S2 observation window. Default WAIT never enumerates or opens hardware."""
import argparse
import datetime
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
import uuid

from visible_state import DisplayState

HERE = Path(__file__).resolve().parent
TICK_MS = 25


def child_command(evidence, python):
    # No ExecutionPolicy bypass, no direct serial handling, no extra commands.
    shell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    return [str(shell), '-NoLogo', '-NoProfile', '-NonInteractive', '-File',
            str(HERE / 'capture.ps1'), '-Mode', 'capture', '-PoweredWindowConfirmed',
            '-EvidenceDirectory', str(evidence), '-PythonExecutable', python]


class Window:
    def __init__(self, root, args):
        import tkinter as tk
        from tkinter import ttk
        self.root, self.args = root, args
        self.state, self.child = None, None
        self.started = False
        self.close_pending = False
        self.events = queue.Queue(maxsize=1024)
        self.overflow = threading.Event()
        self.history = []
        root.title('S2 编码器观察 — ' + ('演示，不做动作' if args.demo else '未开始'))
        root.geometry('880x570')
        root.minsize(800, 500)
        root.configure(bg='#172434')
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.banner = tk.Label(root, text='演示，不做动作' if args.demo else 'S2 肩侧编码器 · 只读观察',
                               font=('Microsoft YaHei UI', 20, 'bold'), bg='#172434', fg='white')
        self.banner.pack(pady=(20, 12))
        self.status = tk.Label(root, text='未开始 / 不做动作', font=('Microsoft YaHei UI', 36, 'bold'),
                               bg='#314256', fg='white', wraplength=840, height=2)
        self.status.pack(fill='x', padx=20)
        self.detail = tk.Label(root, text='', font=('Microsoft YaHei UI', 14), bg='#172434',
                               fg='white', wraplength=825, justify='left')
        self.detail.pack(fill='x', padx=25, pady=12)
        self.values = tk.Label(root, text='位置：— rev    速度：— rev/s\n配对数：—    固件样本年龄：— ms',
                               font=('Microsoft YaHei UI', 16), bg='#172434', fg='#d5e1ea', justify='left')
        self.values.pack(fill='x', padx=25, pady=6)
        buttons = ttk.Frame(root)
        buttons.pack(pady=12)
        self.start_button = ttk.Button(buttons, text='开始一次读取' if args.live else '开始演示' if args.demo else '等待测试准备',
                                       command=self.start)
        self.start_button.grid(row=0, column=0, padx=8)
        self.stop_button = ttk.Button(buttons, text='停止动作提示，等待读取结束', command=self.stop,
                                      state='disabled')
        self.stop_button.grid(row=0, column=1, padx=8)
        if not (args.live or args.demo):
            self.start_button.configure(state='disabled')
        self.note = tk.Label(root, text='', font=('Microsoft YaHei UI', 11), bg='#172434',
                             fg='#c8d6e2', wraplength=825, justify='left')
        self.note.pack(fill='x', padx=25, pady=5)
        self.event_label = tk.Label(root, text='', font=('Microsoft YaHei UI', 10), bg='#172434',
                                    fg='#a9bccd', wraplength=825, justify='left')
        self.event_label.pack(fill='x', padx=25)
        self.render()
        root.after(TICK_MS, self.poll)

    def start(self):
        if self.started or not (self.args.demo or (self.args.live and self.args.powered_window_confirmed)):
            return
        self.started = True
        self.start_button.configure(state='disabled')
        self.stop_button.configure(state='normal')
        if self.args.demo:
            self.state = DisplayState('DEMO')
            self.state.phase, self.state.detail = 'READY', '演示 READY / 不做动作'
            self.root.after(1500, lambda: self.state.revoke('演示样本过期 / 不做动作'))
        else:
            session = 'evidence-visible-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '-' + uuid.uuid4().hex[:8]
            evidence = HERE / session
            self.state = DisplayState(session)
            self.evidence = evidence
            try:
                env = dict(os.environ, PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8')
                self.child = subprocess.Popen(child_command(evidence, self.args.python_executable),
                                              cwd=str(HERE), stdin=subprocess.DEVNULL,
                                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                              text=True, encoding='utf-8', errors='replace',
                                              bufsize=1, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
                threading.Thread(target=self.read_output, daemon=True).start()
            except Exception as error:
                self.state.revoke('启动失败：' + str(error) + ' / 不做动作')
        self.render()

    def read_output(self):
        for line in self.child.stdout:
            line = line.strip()
            if not line:
                continue
            receipt = time.monotonic()
            try:
                record = json.loads(line)
            except (ValueError, TypeError):
                record = {'unexpected_output': line[:300]}
            try:
                self.events.put_nowait((record, receipt))
            except queue.Full:
                self.overflow.set()
                # Keep draining stdout so capture cleanup cannot block on its pipe.
        self.child.stdout.close()

    def stop(self):
        if self.state:
            self.state.revoke('已停止动作提示 / 不做动作；等待本次读取清理结束')
            self.stop_button.configure(state='disabled')
            self.render()

    def close(self):
        if self.child is not None and self.child.poll() is None:
            self.close_pending = True
            self.stop()
            return  # Preserve owned child: its existing finally performs encstop.
        self.root.destroy()

    def poll(self):
        if self.state:
            if self.overflow.is_set():
                self.state.revoke('事件队列已满 / 不做动作')
            code = self.child.poll() if self.child is not None else None
            if code is not None:
                self.state.process_exit(code)
            for _ in range(256):
                try:
                    record, receipt = self.events.get_nowait()
                except queue.Empty:
                    break
                if 'unexpected_output' in record:
                    self.state.revoke('采集错误：' + record['unexpected_output'] + ' / 不做动作')
                else:
                    self.state.accept(record, receipt)
                    if record.get('event') in ('SESSION', 'READY_FOR_MOVEMENT', 'ABORT', 'COMPLETE'):
                        self.history.append(record['event'])
            self.state.tick()
            self.render()
            if self.close_pending and code is not None:
                self.root.destroy()
                return
        self.root.after(TICK_MS, self.poll)

    def render(self):
        if self.state is None:
            title, color = '未开始 / 不做动作', '#314256'
            detail = ('准备好后点击开始一次读取；绿色提示前不要动作。' if self.args.live else
                      '等待本次测试准备完成。灰色时不要动作；看到绿色提示后再轻微往返一次。')
        elif self.args.demo:
            title, color = '演示，不做动作', '#214c75' if not self.state.terminal else '#972f3f'
            detail = self.state.detail + '；没有启动采集进程或访问硬件。'
        elif self.state.ready:
            title, color = '可做一次小幅手动变化', '#167447'
            detail = '本次 READY 与持续新鲜样本已核对。红灯出现立即停止手动变化。'
        else:
            title = '读取结束 / 不做动作' if self.state.phase == 'COMPLETE' else '中止 / 不做动作' if self.state.terminal else '等待 READY / 不做动作'
            color = '#972f3f' if self.state.terminal else '#314256'
            detail = self.state.detail
        self.status.configure(text=title, bg=color)
        self.detail.configure(text=detail)
        self.root.title('S2 编码器观察 — ' + title)
        if self.state and self.state.tracker.samples:
            latest = self.state.tracker.samples[-1]
            suffix = '（历史值，不能据此动作）' if self.state.terminal else ''
            self.values.configure(text=f"位置：{latest['pos_rev']:.6f} rev    速度：{latest['vel_rev_s']:.6f} rev/s\n配对数：{latest['pairs']}    固件样本年龄：{latest['age_ms']} ms {suffix}")
        pending = self.child is not None and self.child.poll() is None
        note = ('此按钮立即撤下动作提示；本次数据读取会按原有最多30秒窗口结束并清理。'
                if pending else '本窗口不控制电机；绿色只提示可手动变化，COMPLETE不等于验收通过。')
        self.note.configure(text=note)
        self.event_label.configure(text='本次状态：' + ' → '.join(self.history[-5:]))


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--powered-window-confirmed', action='store_true')
    parser.add_argument('--demo', action='store_true')
    parser.add_argument('--python-executable', default=sys.executable)
    args = parser.parse_args(argv)
    if args.live and args.demo:
        parser.error('Demo and Live are mutually exclusive')
    if args.live != args.powered_window_confirmed:
        parser.error('Live requires both --live and --powered-window-confirmed')
    return args


def main():
    args = arguments()
    import tkinter as tk
    root = tk.Tk()
    Window(root, args)
    root.mainloop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
