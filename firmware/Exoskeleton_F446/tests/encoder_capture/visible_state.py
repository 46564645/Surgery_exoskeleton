"""Pure host display gate. Import never opens a port or starts a process."""
import datetime
import math
import re
import time

import capture

HOST_FRESH_S = .200  # 25 ms UI tick leaves margin below the 250 ms display budget.
FUTURE_S = .050


def timestamp(value):
    result = datetime.datetime.fromisoformat(value.replace('Z', '+00:00'))
    capture.require(result.tzinfo is not None, 'UTC timezone missing')
    return result.timestamp()


def finite(value):
    capture.require(type(value) in (int, float) and math.isfinite(value), 'Invalid clock')
    return float(value)


def sample_fields(value):
    capture.require(isinstance(value, dict), 'Sample missing')
    # Reuse the existing strict firmware parser and its 100 ms sample-age gate.
    return capture.feedback('OK ' + ' '.join(str(k) + '=' + str(v) for k, v in value.items()))


class DisplayState:
    def __init__(self, session_id, *, monotonic=time.monotonic, wall=time.time):
        self.session_id = session_id
        self.monotonic, self.wall = monotonic, wall
        self.created_mono, self.created_wall = monotonic(), wall()
        self.last_clock_mono, self.last_clock_wall = self.created_mono, self.created_wall
        self.phase, self.detail = 'WAITING', '未开始 / 不做动作'
        self.terminal = False
        self.session_seen = False
        self.ready_seen = False
        self.last_event_mono = None
        self.last_event_wall = None
        self.last_sample_receive = None
        self.last_sample_source = None
        self.last_sample_wall = None
        self.stream_deadline = None
        self.warmup_deadline = None
        self.tracker = capture.Tracker()

    @property
    def ready(self):
        self.tick()
        return self.phase == 'READY' and not self.terminal

    def revoke(self, detail, phase='ABORT'):
        if not self.terminal:
            self.phase, self.detail, self.terminal = phase, detail, True
        return False

    def tick(self):
        if self.terminal:
            return
        now, wall = self.monotonic(), self.wall()
        # A clock discontinuity cannot make an old event fresh again.
        wall_delta = wall - self.last_clock_wall
        mono_delta = now - self.last_clock_mono
        self.last_clock_mono, self.last_clock_wall = now, wall
        if mono_delta < 0 or abs(wall_delta - mono_delta) > HOST_FRESH_S:
            self.revoke('主机时钟发生跳变 / 不做动作')
        elif self.stream_deadline is not None and now >= self.stream_deadline:
            self.revoke('读取时间已到 / 不做动作', 'COMPLETE')
        elif self.warmup_deadline is not None and not self.tracker.samples and now > self.warmup_deadline:
            self.revoke('初始编码器采集超时 / 不做动作')
        elif self.last_sample_receive is not None and (
                now - self.last_sample_receive >= HOST_FRESH_S or
                now - self.last_sample_source >= HOST_FRESH_S or
                wall - self.last_sample_wall >= HOST_FRESH_S):
            self.revoke('样本已过期 / 不做动作')

    def accept(self, record, received_mono=None):
        self.tick()
        if self.terminal:
            return False
        try:
            capture.require(isinstance(record, dict), 'Invalid event')
            capture.require(record.get('session_id') == self.session_id, 'Wrong current session')
            event = record.get('event')
            # Terminal events always withdraw readiness, even if delayed.
            if event in ('ABORT', 'COMPLETE', 'FIRMWARE_BOUNDED_END'):
                detail = str(record.get('error', event))
                return self.revoke(detail + ' / 不做动作', 'COMPLETE' if event != 'ABORT' else 'ABORT')
            now, wall = self.monotonic(), self.wall()
            receipt = now if received_mono is None else finite(received_mono)
            source = finite(record['host_monotonic_s'])
            emitted = timestamp(record['at_utc'])
            for age in (now - receipt, now - source, wall - emitted):
                capture.require(-FUTURE_S <= age < HOST_FRESH_S, 'Delayed/future event')
            capture.require(source >= self.created_mono - FUTURE_S and
                            emitted >= self.created_wall - FUTURE_S, 'Event precedes owned run')
            if self.last_event_mono is not None:
                capture.require(source > self.last_event_mono and emitted >= self.last_event_wall,
                                'Event order regressed')
            self.last_event_mono, self.last_event_wall = source, emitted
            if event == 'SESSION':
                capture.require(not self.session_seen and record.get('mode') == 'capture', 'Invalid SESSION')
                binding = record['binding']
                capture.require(binding.get('parent') == capture.PARENT and
                                binding.get('serial_verified') is True, 'Fixed binding missing')
                ports = binding['ports']
                capture.require(len(ports) == 1 and ports[0]['DeviceID'] == record['port'] and
                                re.fullmatch(r'COM[0-9]+', record['port']) and
                                ports[0]['Status'] == 'OK' and ports[0]['ConfigManagerErrorCode'] == 0,
                                'Invalid fixed VCP')
                capture.require(record.get('max_duration_s') == 30 and
                                record.get('max_can_requests') == 1200 and
                                record.get('host_feedback_hz') == 10, 'Capture bounds changed')
                self.session_seen = True
                self.phase, self.detail = 'WAITING', '正在检查 / 不做动作'
                # Also bounds startup and catches an event that might start the MCU.
                self.stream_deadline = source + capture.MAX_SECONDS
                return True
            capture.require(self.session_seen, 'SESSION required first')
            if event == 'REPLY' and record.get('command') == 'encstart':
                capture.require(record.get('raw_reply', '').strip() ==
                                'OK encoder_stream_started output_allowed=0', 'Start not acknowledged')
                self.stream_deadline = min(self.stream_deadline, source + capture.MAX_SECONDS)
                self.warmup_deadline = source + .200
            elif event == 'ENCODER_SAMPLE':
                sample = sample_fields(record['sample'])
                capture.require(sample['active'] == 1 and sample['reason'] == 0,
                                'Encoder inactive/reason nonzero')
                if sample['valid'] == 0:
                    capture.require(not self.tracker.samples and self.warmup_deadline is not None and
                                    now <= self.warmup_deadline, 'Encoder invalid after acquisition/warmup')
                    self.phase, self.detail = 'WAITING', '等待初始完整配对 / 不做动作'
                    return True
                self.tracker.observe(sample, receipt)
                self.last_sample_receive, self.last_sample_source = receipt, source
                self.last_sample_wall = emitted
                if not self.ready_seen:
                    self.detail = '收到有效样本，等待本次 READY / 不做动作'
            elif event == 'READY_FOR_MOVEMENT':
                capture.require(not self.ready_seen, 'Duplicate READY')
                capture.require(record.get('output_allowed') == 0, 'Output permission changed')
                capture.require(len(self.tracker.samples) >= 2 and
                                record.get('distinct_pairs') == len(self.tracker.samples), 'Two observed pairs required')
                capture.require(sample_fields(record['first_pair']) == self.tracker.samples[0] and
                                sample_fields(record['latest_pair']) == self.tracker.samples[-1],
                                'READY evidence does not match observed samples')
                capture.require(self.last_sample_receive is not None and
                                now - self.last_sample_receive < HOST_FRESH_S and
                                source - self.last_sample_source <= .100,
                                'READY sample expired during checks')
                self.ready_seen = True
                self.phase, self.detail = 'READY', '可做一次小幅手动变化'
            return True
        except (capture.CaptureError, KeyError, TypeError, ValueError, OverflowError) as error:
            return self.revoke('事件校验失败：' + str(error) + ' / 不做动作')

    def process_exit(self, code):
        return self.revoke('采集进程已结束（退出码 ' + str(code) + '）/ 不做动作', 'COMPLETE')
