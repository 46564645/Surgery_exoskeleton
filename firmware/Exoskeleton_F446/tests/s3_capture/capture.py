"""Bounded S3 VCP recording. Default/help are offline; no serial import on import.

Qualification is read-only and owns SDO for this boot. Trial requires a separately
approved frozen plan and a different boot. No default physical command/threshold.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import re
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
OLD = HERE.parent / 'encoder_capture' / 'capture.py'
if not OLD.is_file():
    raise RuntimeError('Required relative encoder_capture/capture.py dependency is missing: ' + str(OLD))
_spec = importlib.util.spec_from_file_location('_s2_capture_reuse', OLD)
_reuse = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_reuse)
Journal, parse, require, uint, forward = (_reuse.Journal, _reuse.parse, _reuse.require,
                                       _reuse.uint, _reuse.forward)
CaptureError, PARENT = _reuse.CaptureError, _reuse.PARENT
SAFE_OFF, ARMED, ACTIVE, FAULT, PREPARING, READY = range(6)
READER_DISABLED, READER_IDLE, READER_ACTIVE, READER_STOPPED = range(4)
READER_FAULT, READER_STOPPING, READER_OBSERVING, READER_COMPLETE = range(4, 8)
FIELDS = ((372, 'float'), (378, 'float'), (195, 'bool'), (366, 'bool'),
          (272, 'float'), (287, 'uint8'), (288, 'uint8'))
GROUPS = {
    's3limits': {'axes', 'policy', 'coords', 'direction', 'ratio', 'zero', 'lo', 'hi', 'speed', 'limit', 'slew'},
    's3timing': {'feedback', 'service', 'max_request', 'prepare', 'reply', 'period', 'window'},
    's3stopcfg': {'span', 'speed', 'dwell', 'fresh', 'window'},
}
READS = frozenset(('ping', 'status', 'canstatus', 'shoulder', 'shoulderfeedback',
                  'outputstatus', 'stopstatus', 's3feedback', 's3properties', 's3reader',
                  's3limits', 's3timing', 's3stopcfg', 's3anchor', 'qualstatus', 'qualerror'))


def tagged(reply, name, keys=None):
    require(reply.startswith('OK ' + name + ' '), 'Wrong reply tag: ' + name)
    require(all('=' in token for token in reply.split()[2:]), 'Unexpected unkeyed reply token')
    f = parse(reply)
    if keys is not None:
        require(set(f) == set(keys), 'Unexpected/missing fields: ' + name)
    return f


def bit(f, key):
    value = uint(f, key)
    require(value in (0, 1), 'Invalid boolean: ' + key)
    return value


def hex_value(f, key, digits=8):
    value = f.get(key, '')
    require(bool(re.fullmatch('[0-9A-F]{' + str(digits) + '}', value)), 'Invalid hex: ' + key)
    return int(value, 16)


def real(f, key):
    value = struct.unpack('<f', struct.pack('<I', hex_value(f, key)))[0]
    require(math.isfinite(value), 'Nonfinite float: ' + key)
    return value


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def elapsed(new, old):
    delta = (new - old) & 0xffffffff
    require(delta < 0x80000000, 'Reversed/future timestamp')
    return delta


def fixed(f, expected):
    for key, value in expected.items():
        require(f.get(key) == str(value), 'Unexpected ' + key)


class Link(_reuse.Link):
    # Constructor is the existing no-reset/DTR-low/RTS-low serial procedure.
    def __init__(self, port, journal, mode, serial_factory, deadline, max_queries, clock=time.monotonic):
        self.deadline, self.max_queries, self.clock = deadline, max_queries, clock
        self.count, self.once, self.pending = 0, set(), False
        try:
            super().__init__(port, journal, mode, serial_factory)
        except Exception:
            if hasattr(self, 'serial'):
                self.serial.close()
            raise

    def query(self, command, deadline=None):
        base = command.split(' ', 1)[0]
        readable = command in READS or bool(re.fullmatch(r'qualfield [0-6]', command))
        pulse = bool(re.fullmatch(r'shoulderpulse [1-9][0-9]* -?[0-9]+ [1-9][0-9]*', command))
        require(readable or base in {'qualify', 'qualcancel', 'prepare', 'arm', 'cancel'} or pulse,
                'Command outside fixed whitelist')
        require(command == base or pulse or bool(re.fullmatch(r'qualfield [0-6]', command)), 'Extra arguments')
        require(self.mode == 'trial' or readable or command in {'qualify', 'qualcancel'}, 'Qualification cannot output')
        require(self.mode != 'trial' or command not in {'qualify', 'qualcancel'}, 'Trial cannot qualify')
        cleanup = command in {'cancel', 'qualcancel'}
        require(self.usable or cleanup, 'Unusable serial link')
        require(not self.pending, 'A serial query is already pending')
        if not readable:
            require(base not in self.once, 'Command may only be attempted once: ' + base)
            self.once.add(base)  # Before any write; uncertain delivery cannot retry.
        limit = min(self.deadline, deadline) if deadline is not None else self.deadline
        if cleanup:
            limit = self.clock() + .7  # One bounded safety cleanup after a failed normal budget.
        else:
            require(self.count < self.max_queries and self.clock() < limit, 'Query/time budget exhausted')
        self.count += 1
        self.pending = True
        self.journal.emit('COMMAND', command=command, number=self.count)
        raw = b''
        try:
            remaining = limit - self.clock()
            require(remaining > 0, 'Expired serial deadline')
            write_timeout = min(.35, remaining)
            if self.serial.write_timeout != write_timeout:
                self.serial.write_timeout = write_timeout
            wire = (command + '\n').encode('ascii')
            require(self.serial.write(wire) == len(wire), 'Incomplete serial write')
            remaining = limit - self.clock()
            require(remaining > 0, 'Write exhausted serial deadline')
            read_timeout = min(.35, remaining)
            if self.serial.timeout != read_timeout:
                self.serial.timeout = read_timeout
            raw = self.serial.read_until(b'\n', 257)
            require(self.clock() < limit, 'Reply after serial deadline')
            require(raw.endswith(b'\n') and len(raw) <= 256, 'Missing/oversize reply')
            reply = raw.decode('ascii').rstrip('\r\n')
        except Exception:
            self.usable = False
            self.journal.emit('SERIAL_EXCEPTION', command=command, partial_hex=raw.hex())
            raise
        finally:
            self.pending = False
        self.journal.emit('REPLY', command=command, raw_hex=raw.hex(), raw_reply=reply)
        try:
            parse(reply)
        except CaptureError:
            self.usable = False  # Keep raw evidence; only bounded cleanup remains.
            raise
        return reply


def query(link, command, deadline=None):
    return link.query(command, deadline=deadline)


def gate(link, qualification):
    require(query(link, 'ping') == 'OK pong', 'Wrong ping')
    status = parse(query(link, 'status'))
    fixed(status, {'stage': 'S2_CAN_RX', 'output_allowed': 0, 'uart_errors': 0,
                   'rx_overflows': 0, 'tx_timeouts': 0, 'scheduler_overruns': 0})
    uint(status, 'uptime_ms')
    fixed(parse(query(link, 'canstatus')), {'can1_started': 1, 'can1_baud': 500000,
          'can1_start_error': 0, 'can2_started': 1, 'can2_baud': 1000000,
          'can2_start_error': 0, 'output_allowed': 0})
    shoulder = parse(query(link, 'shoulder'))
    fixed(shoulder, {'bus': 'shoulder', 'started': 1, 'start_error': 0,
                    'drop': 0, 'invalid': 0, 'fov': 0, 'err': 0, 'bus_off': 0})
    out = parse(query(link, 'outputstatus'))
    fixed(out, {'stage': 'S2_CAN_RX', 's3_state': SAFE_OFF, 'configured': int(not qualification),
                'transport_bound': int(not qualification), 'output_allowed': 0, 'request_valid': 0,
                'arm_token': 0, 'prep_state': 0, 'feedback_pending': 0})
    hb = parse(query(link, 'shoulderfeedback'))
    fixed(hb, {'hb': 1, 'state': 1, 'error': '00000000', 'fault': 0, 'output_allowed': 0})
    latest = parse(query(link, 'status'))
    fixed(latest, {'output_allowed': 0, 'uart_errors': 0, 'rx_overflows': 0,
                   'tx_timeouts': 0, 'scheduler_overruns': 0})
    require(elapsed(uint(latest, 'uptime_ms'), uint(hb, 'hb_ms')) <= 250, 'Stale IDLE heartbeat')
    unused = qual_status(query(link, 'qualstatus'))
    fixed(unused, {'state': 0, 'reason': 0, 'attempted': 0, 'completed': 0,
                   'mask': 0, 'claimed': 0, 'pending': 0, 'cancelled': 0, 'out': 0})
    return {'status': latest, 'output': out, 'heartbeat': hb, 'can': shoulder, 'qualification_before': unused}


def qual_status(reply):
    f = tagged(reply, 'qualstatus', 'state reason attempted completed mask claimed pending first last begin end hfirst hlast cancelled out'.split())
    for key in f:
        uint(f, key)
    for key in ('claimed', 'pending', 'first', 'last', 'cancelled', 'out'):
        bit(f, key)
    require(uint(f, 'state') <= 4 and uint(f, 'reason') <= 9, 'Invalid qualification state/reason')
    require(uint(f, 'attempted') <= 7 and uint(f, 'completed') <= uint(f, 'attempted')
            and uint(f, 'mask') <= 127 and uint(f, 'mask').bit_count() == uint(f, 'completed')
            and bit(f, 'out') == 0, 'Invalid qualification counts/gate')
    return f


def qual_field(reply, index):
    f = tagged(reply, 'qualfield', 'field ep requested replied decoded q r bits req raw id dlc'.split())
    endpoint, kind = FIELDS[index]
    fixed(f, {'field': index, 'ep': endpoint, 'requested': 1, 'replied': 1, 'decoded': 1, 'dlc': 8})
    bits = hex_value(f, 'bits')
    hex_value(f, 'req', 16); hex_value(f, 'raw', 16)
    request, raw = bytes.fromhex(f['req']), bytes.fromhex(f['raw'])
    require(len(request) == 8 and len(raw) == 8 and hex_value(f, 'id', 3) == 0x025, 'Invalid CAN raw frame')
    expected = b'\x00' + struct.pack('<H', endpoint) + b'\x00'
    require(request == expected + b'\x00' * 4 and raw[:4] == expected
            and int.from_bytes(raw[4:], 'little') == bits, 'Raw SDO envelope/value mismatch')
    require(elapsed(uint(f, 'r'), uint(f, 'q')) <= 250, 'Qualification reply latency')
    if kind == 'float':
        value = real(f, 'bits')
    else:
        require(bits <= (1 if kind == 'bool' else 255), 'Dirty scalar high bytes or invalid bool')
        value = bool(bits) if kind == 'bool' else bits
    return {'field': index, 'endpoint': endpoint, 'type': kind, 'value': value,
            'request_ms': uint(f, 'q'), 'reply_ms': uint(f, 'r'), 'wire': f}


def qualify(link, summary, journal, clock=time.monotonic):
    summary['preflight'] = gate(link, True)
    begun = clock()
    deadline = begun + 2.0
    require(query(link, 'qualify', deadline) == 'OK qualification_started output_allowed=0', 'Qualification blocked')
    previous = None
    while clock() < deadline:
        f = qual_status(query(link, 'qualstatus', deadline))
        if previous:
            for key in ('attempted', 'completed'):
                require(uint(f, key) >= uint(previous, key), 'Qualification counter regressed')
        summary['qualification_status'] = previous = f
        journal.emit('QUALIFICATION_STATUS', fields=f)
        state = uint(f, 'state')
        if state == 2:
            fixed(f, {'reason': 0, 'attempted': 7, 'completed': 7, 'mask': 127,
                       'claimed': 1, 'pending': 0, 'first': 1, 'last': 1, 'cancelled': 1})
            require(clock() < deadline, 'Qualification completed after 2 seconds')
            break
        require(state == 1 and uint(f, 'reason') == 0, 'Qualification failed/cancelled/idle')
    else:
        raise CaptureError('Qualification exceeded 2 seconds')
    fields = [qual_field(query(link, 'qualfield ' + str(i)), i) for i in range(7)]
    for a, b in zip(fields, fields[1:]):
        elapsed(b['request_ms'], a['reply_ms'])
    elapsed(fields[0]['request_ms'], uint(f, 'begin'))
    require(forward(uint(f, 'hlast'), fields[-1]['reply_ms']) <= 2000, 'No post-last-field IDLE heartbeat')
    require(elapsed(uint(f, 'end'), uint(f, 'begin')) <= 2000, 'Firmware qualification window exceeded')
    summary['qualification_fields'] = fields
    summary['cycle_ms'] = forward(fields[-1]['reply_ms'], fields[0]['request_ms'])
    summary['success'] = True
    summary['interpretation'] = 'Seven real CAN reads with first/final IDLE; not motion readiness or fresh evidence for another boot.'


def feedback(reply):
    f = tagged(reply, 's3feedback', 'seq now fresh pos vel pq pr vq vr out'.split())
    for key in ('seq', 'now', 'pq', 'pr', 'vq', 'vr'):
        uint(f, key)
    bit(f, 'fresh'); bit(f, 'out')
    f['position_rev'], f['velocity_rev_s'] = real(f, 'pos'), real(f, 'vel')
    for a, b in (('pq', 'pr'), ('pr', 'vq'), ('vq', 'vr')):
        elapsed(uint(f, b), uint(f, a))
    return f


class Tracker:
    def __init__(self, freshness_ms):
        self.freshness_ms, self.samples, self.last = freshness_ms, [], None

    def observe(self, f):
        if bit(f, 'fresh') == 0:
            return False  # Historical cache remains a record, never new motion evidence.
        require(uint(f, 'seq') != 0, 'Fresh snapshot has no sequence')
        for key in ('pq', 'pr', 'vq', 'vr'):
            require(elapsed(uint(f, 'now'), uint(f, key)) <= self.freshness_ms, 'Stale feedback timestamp')
        if self.last:
            if f['seq'] == self.last['seq']:
                require(all(f[k] == self.last[k] for k in ('pos', 'vel', 'pq', 'pr', 'vq', 'vr')),
                        'Cached pair changed under one sequence')
                return False
            forward(uint(f, 'seq'), uint(self.last, 'seq'))
            for key in ('pq', 'pr', 'vq', 'vr'):
                forward(uint(f, key), uint(self.last, key))
        self.samples.append(dict(f)); self.last = f
        return True


def reader(reply):
    f = tagged(reply, 's3reader', 'state reason requests stop observed obs hb hms error fault win out now hvalid'.split())
    for key in ('state', 'reason', 'requests', 'stop', 'obs', 'hb', 'hms', 'fault', 'win', 'now'):
        uint(f, key)
    bit(f, 'observed'); bit(f, 'out'); bit(f, 'hvalid')
    require(uint(f, 'state') <= 7 and uint(f, 'reason') <= 4, 'Invalid reader state/reason')
    require(hex_value(f, 'error') == 0 and uint(f, 'fault') == 0, 'Driver error/fault')
    require(uint(f, 'state') != READER_FAULT and uint(f, 'reason') == 0, 'Reader fault')
    return f


def properties(reply, freshness_ms):
    f = tagged(reply, 's3properties', 'seq now fresh tq cal ready cm im zq mq iq first last'.split())
    for key in ('seq', 'now', 'cm', 'im', 'zq', 'mq', 'iq', 'first', 'last'):
        uint(f, key)
    fixed(f, {'fresh': 1, 'cal': 1, 'ready': 1, 'cm': 1, 'im': 1})
    require(uint(f, 'seq') > 0 and real(f, 'tq') == 0.0, 'Unverified ready target/sequence')
    for key in ('first', 'zq', 'mq', 'iq', 'last'):
        require(elapsed(uint(f, 'now'), uint(f, key)) <= freshness_ms, 'Stale property proof')
    for a, b in (('first', 'zq'), ('zq', 'mq'), ('mq', 'iq'), ('iq', 'last')):
        elapsed(uint(f, b), uint(f, a))
    return f


def anchor(reply):
    f = tagged(reply, 's3anchor', 'coords phase pos seq q r begin now out'.split())
    for key in ('coords', 'phase', 'seq', 'q', 'r', 'begin', 'now'):
        uint(f, key)
    bit(f, 'out')
    require(uint(f, 'coords') == 3 and uint(f, 'phase') <= 3, 'Invalid relative anchor scope/phase')
    f['origin_rev'] = real(f, 'pos')
    return f


def ready_anchor(current, before, plan, props, baseline):
    fixed(current, {'coords': 3, 'phase': 2, 'out': 0})
    require(uint(current, 'seq') != 0, 'Locked anchor has no complete sequence')
    elapsed(uint(current, 'begin'), uint(before, 'now'))
    require(elapsed(uint(current, 'now'), uint(current, 'begin')) < uint(plan['s3timing'], 'prepare'),
            'Anchor outside this prepare window')
    forward(uint(current, 'q'), uint(current, 'begin'))
    forward(uint(current, 'r'), uint(current, 'begin'))
    require(elapsed(uint(current, 'r'), uint(current, 'q')) <= uint(plan['s3timing'], 'reply'),
            'Anchor reply latency exceeded')
    elapsed(uint(current, 'now'), uint(current, 'r'))
    # Anchor is intentionally frozen history. Current READY/baseline snapshots
    # prove freshness separately; never require the old origin to stay fresh.
    for proof in (props, baseline):
        elapsed(uint(proof, 'seq'), uint(current, 'seq'))
        elapsed(uint(current, 'now'), uint(proof, 'now'))


def validate_plan(plan):
    require(isinstance(plan, dict) and plan.get('version') == 1 and plan.get('mode') == 'trial'
            and plan.get('approved') is True and plan.get('enabled') is True, 'Trial plan is disabled/unapproved')
    for command, keys in GROUPS.items():
        group = plan.get(command)
        require(isinstance(group, dict) and set(group) == keys
                and all(isinstance(value, str) for value in group.values()), 'Missing frozen group: ' + command)
    limits, timing, stop = plan['s3limits'], plan['s3timing'], plan['s3stopcfg']
    fixed(limits, {'axes': 1, 'policy': 2})
    require(limits['direction'] in ('1', '-1') and limits['coords'] in ('1', '2', '3'), 'Unknown mapping/scope')
    if limits['coords'] == '3':
        require(limits['direction'] == '1' and real(limits, 'ratio') == 1.0
                and hex_value(limits, 'zero') == 0, 'Relative native rotor requires +1 direction, ratio1, zero0')
    for key in ('ratio', 'speed', 'limit', 'slew'):
        require(real(limits, key) > 0, 'Invalid positive limit: ' + key)
    for key in ('zero', 'lo', 'hi'):
        real(limits, key)
    require(real(limits, 'lo') < real(limits, 'hi'), 'Invalid activity limits')
    for group in (timing, {k: stop[k] for k in ('dwell', 'fresh', 'window')}):
        for key in group:
            require(0 < uint(group, key) < 0x80000000, 'Invalid timing: ' + key)
    require(real(stop, 'span') > 0 and real(stop, 'speed') > 0, 'Invalid stop thresholds')
    pulse = plan.get('pulse', {})
    effort, ttl = pulse.get('signed_mNm'), pulse.get('ttl_ms')
    require(type(effort) is int and -0x7fffffff <= effort <= 0x7fffffff and effort != 0,
            'Missing/nonzero signed mNm action')
    require(type(ttl) is int and 0 < ttl <= uint(timing, 'max_request') < 0x80000000, 'Invalid explicit TTL')
    require(abs(f32(f32(effort) / 1000.0)) <= real(limits, 'limit'), 'Action exceeds frozen output limit')
    bounds = plan.get('bounds', {})
    require(set(bounds) == {'total_seconds', 'prepare_seconds', 'stop_seconds', 'max_queries'}, 'Missing explicit PC bounds')
    for key in ('total_seconds', 'prepare_seconds', 'stop_seconds'):
        require(type(bounds[key]) in (int, float) and math.isfinite(bounds[key]) and 0 < bounds[key] <= 30,
                'Invalid PC duration: ' + key)
    require(bounds['prepare_seconds'] * 1000 <= uint(timing, 'prepare')
            and bounds['stop_seconds'] * 1000 >= uint(stop, 'window'), 'PC/MCU phase budget mismatch')
    require(type(bounds['max_queries']) is int and 1 <= bounds['max_queries'] <= 1000, 'Invalid query bound')
    return plan


def timing_budget(plan, cycle_ms):
    timing, stop = plan['s3timing'], plan['s3stopcfg']
    require(type(cycle_ms) is int and 0 < cycle_ms < 0x80000000, 'Missing measured seven-read cycle')
    c, p, service = cycle_ms, uint(timing, 'period'), uint(timing, 'service')
    freshness = max(p, c) + c + service
    require(freshness <= min(uint(timing, 'feedback'), uint(stop, 'fresh')), 'Cycle cannot meet freshness budget')
    required_window = c + uint(timing, 'prepare') + uint(timing, 'max_request') + plan['pulse']['ttl_ms'] + service
    require(required_window <= uint(timing, 'window'), 'Reader window cannot cover trial phases')
    stop_window = uint(timing, 'feedback') + uint(stop, 'dwell') + max(p, c) + c + service
    require(stop_window <= uint(stop, 'window'), 'Stop observation window cannot cover new IDLE heartbeat and dwell')
    return {'cycle_ms': c, 'freshness_required_ms': freshness,
            'reader_required_ms': required_window, 'stop_required_ms': stop_window}


def frozen_plan(path, expected_sha):
    raw = pathlib.Path(path).read_bytes()
    require(bool(re.fullmatch('[0-9a-f]{64}', expected_sha or '')) and hashlib.sha256(raw).hexdigest() == expected_sha,
            'Plan SHA256 is not the explicitly confirmed frozen plan')
    plan = validate_plan(json.loads(raw.decode('utf-8-sig')))
    reference = plan.get('qualification', {})
    require(set(reference) == {'summary', 'sha256'}, 'Missing qualification record binding')
    saved = pathlib.Path(reference['summary']).read_bytes()
    require(hashlib.sha256(saved).hexdigest() == reference['sha256'], 'Qualification record changed')
    evidence = json.loads(saved.decode('utf-8-sig'))
    require(evidence.get('source') == 'live-vcp' and evidence.get('mode') == 'qualify'
            and evidence.get('success') is True and evidence.get('binding', {}).get('parent') == PARENT,
            'Qualification is not a successful real bound-device record')
    status = qual_status('OK qualstatus ' + ' '.join(k + '=' + v for k, v in evidence['qualification_status'].items()))
    fixed(status, {'state': 2, 'reason': 0, 'attempted': 7, 'completed': 7, 'mask': 127,
                   'claimed': 1, 'pending': 0, 'first': 1, 'last': 1, 'cancelled': 1, 'out': 0})
    records = evidence.get('qualification_fields', [])
    require(len(records) == 7, 'Incomplete qualification evidence')
    decoded = [qual_field('OK qualfield ' + ' '.join(k + '=' + v for k, v in row['wire'].items()), i)
               for i, row in enumerate(records)]
    for a, b in zip(decoded, decoded[1:]):
        elapsed(b['request_ms'], a['reply_ms'])
    forward(uint(status, 'hlast'), decoded[-1]['reply_ms'])
    cycle = forward(decoded[-1]['reply_ms'], decoded[0]['request_ms'])
    require(cycle == evidence.get('cycle_ms'), 'Qualification cycle mismatch')
    timing_budget(plan, cycle)
    plan['_cycle_ms'] = cycle
    plan['_sha256'] = expected_sha
    return plan


def trial(link, plan, summary, journal, clock=time.monotonic):
    validate_plan(plan)
    summary['preflight'] = gate(link, False)  # Qual unused in this DIFFERENT cold boot.
    summary['profile'] = {}
    for command, keys in GROUPS.items():
        actual = tagged(query(link, command), command, keys)
        require(actual == plan[command], 'Frozen firmware profile mismatch: ' + command)
        summary['profile'][command] = actual
    summary['timing_budget'] = timing_budget(plan, plan['_cycle_ms'])
    relative = plan['s3limits']['coords'] == '3'
    if relative:
        before_anchor = anchor(query(link, 's3anchor'))
        summary['anchor_before_prepare'] = before_anchor
        fixed(before_anchor, {'phase': 0, 'seq': 0, 'q': 0, 'r': 0, 'begin': 0, 'out': 0})
        require(hex_value(before_anchor, 'pos') == 0, 'Unrequested anchor contains an origin')
        elapsed(uint(before_anchor, 'now'), uint(summary['preflight']['status'], 'uptime_ms'))
    begun = clock()
    deadline = begun + plan['bounds']['prepare_seconds']
    require(query(link, 'prepare', deadline) == 'OK preparing output_allowed=0', 'Prepare blocked')
    while clock() < deadline:
        out = parse(query(link, 'outputstatus', deadline))
        summary['last_output'] = out
        require(uint(out, 's3_state') in (SAFE_OFF, PREPARING, READY), 'Prepare entered fault/output state')
        fixed(out, {'configured': 1, 'transport_bound': 1, 'output_allowed': 0, 'arm_token': 0})
        if uint(out, 's3_state') == READY:
            fixed(out, {'prep_state': 4, 'request_valid': 0})
            break
    else:
        raise CaptureError('Prepare deadline exceeded')
    # Each telemetry reply is one complete sequence; never splice different seqs.
    summary['ready_properties'] = properties(query(link, 's3properties'), uint(plan['s3timing'], 'feedback'))
    ready_cycle = elapsed(uint(summary['ready_properties'], 'last'), uint(summary['ready_properties'], 'first'))
    summary['timing_budget_ready'] = timing_budget(plan, max(plan['_cycle_ms'], ready_cycle))
    tracker = Tracker(uint(plan['s3timing'], 'feedback'))
    summary['samples'] = tracker.samples
    summary['feedback_records'] = []

    def record_feedback(reply):
        f = feedback(reply)
        summary['feedback_records'].append(dict(f))
        summary['last_feedback'] = f
        if tracker.observe(f):
            journal.emit('SAMPLE', sample=f)
        return f

    baseline = record_feedback(query(link, 's3feedback'))
    fixed(baseline, {'fresh': 1, 'out': 0})
    summary['baseline'] = dict(baseline)
    if relative:
        locked_anchor = anchor(query(link, 's3anchor'))
        summary['anchor_ready'] = locked_anchor
        ready_anchor(locked_anchor, before_anchor, plan, summary['ready_properties'], baseline)
    before = reader(query(link, 's3reader'))
    fixed(before, {'state': READER_ACTIVE, 'out': 0, 'observed': 0, 'hb': 8, 'hvalid': 1})
    require(elapsed(uint(before, 'now'), uint(before, 'hms')) <= uint(plan['s3timing'], 'feedback'),
            'Stale pre-arm closed-loop heartbeat')
    needed = uint(plan['s3timing'], 'max_request') + plan['pulse']['ttl_ms'] + uint(plan['s3timing'], 'service')
    require(uint(before, 'win') >= needed, 'Remaining reader window is insufficient')
    summary['reader_before_arm'] = before
    if relative:
        elapsed(uint(before, 'now'), uint(locked_anchor, 'now'))
    armed_reply = query(link, 'arm')
    require(armed_reply.startswith('OK armed '), 'Wrong arm acknowledgement')
    armed = parse(armed_reply)
    require(set(armed) == {'token'}, 'Wrong arm fields')
    token = uint(armed, 'token')
    require(token != 0, 'Arm did not provide a token')
    # No read, sleep, user wait, second Arm or second pulse between these calls.
    command = 'shoulderpulse %d %d %d' % (token, plan['pulse']['signed_mNm'], plan['pulse']['ttl_ms'])
    summary['pulse_attempted'] = True
    require(query(link, command) == 'OK request', 'Pulse blocked/uncertain')
    summary['pulse_acknowledged'] = True
    stop_deadline = clock() + plan['pulse']['ttl_ms'] / 1000 + plan['bounds']['stop_seconds']
    while clock() < stop_deadline:
        f = record_feedback(query(link, 's3feedback', stop_deadline))
        if bit(f, 'out') == 0:
            break
        require(bit(f, 'fresh') == 1, 'Output allowed with stale feedback')
    else:
        raise CaptureError('Trial/stop deadline exceeded')
    # Observed is a historical post-stop fact; COMPLETE may legitimately fresh=0.
    while clock() < stop_deadline:
        r = reader(query(link, 's3reader', stop_deadline))
        summary['reader_final'] = r
        require(bit(r, 'out') == 0, 'Output permission remained enabled')
        if uint(r, 'state') == READER_COMPLETE:
            require(bit(r, 'observed') == 1 and uint(r, 'hb') == 1, 'No completed IDLE stop observation')
            forward(uint(r, 'obs'), uint(r, 'stop'))
            forward(uint(r, 'stop'), uint(before, 'now'))
            forward(uint(r, 'hms'), uint(r, 'stop'))
            elapsed(uint(r, 'now'), uint(r, 'obs'))
        else:
            require(uint(r, 'state') in (READER_STOPPING, READER_OBSERVING), 'Stop reader did not observe completion')
        # Continue after output stops: a short TTL may finish before the first
        # host poll. These are independent full snapshots, never seq-spliced.
        post = record_feedback(query(link, 's3feedback', stop_deadline))
        require(bit(post, 'out') == 0, 'Output permission returned during stop observation')
        if uint(r, 'state') == READER_COMPLETE:
            break
    else:
        raise CaptureError('Stop observation deadline exceeded')
    stop = parse(query(link, 'stopstatus'))
    fixed(stop, {'policy': 2, 'source': 'shoulder_sdo', 'observed': 1, 'reader': READER_COMPLETE,
                 'stop_accepted': 1, 'purge_ok': 1, 'stop_delivery_failed': 0})
    for key in ('seen', 'fresh', 'stationary'):
        bit(stop, key)
    uint(stop, 'dwell_ms'); uint(stop, 'age_ms')
    final = parse(query(link, 'outputstatus'))
    fixed(final, {'s3_state': FAULT, 'reason': 4, 'output_allowed': 0, 'request_valid': 0,
                  'arm_token': 0, 'stop_attempted': 1})
    summary['stop_status'], summary['last_output'] = stop, final
    if relative:
        retired_anchor = anchor(query(link, 's3anchor'))
        summary['anchor_final'] = retired_anchor
        fixed(retired_anchor, {'phase': 3, 'out': 0})
        require(all(retired_anchor[key] == locked_anchor[key]
                    for key in ('coords', 'pos', 'seq', 'q', 'r', 'begin')), 'Frozen anchor identity changed/reset')
        elapsed(uint(retired_anchor, 'now'), uint(summary['reader_final'], 'now'))
        forward(uint(retired_anchor, 'now'), uint(locked_anchor, 'now'))
    summary['samples'] = tracker.samples
    positions = [s['position_rev'] for s in tracker.samples]
    summary['position_min_rev'], summary['position_max_rev'] = min(positions), max(positions)
    summary['position_range_rev'] = max(positions) - min(positions)
    summary['numerical_variation_observed'] = summary['position_range_rev'] > 0
    summary['motion_evidence'] = {'distinct_pairs': len(tracker.samples),
        'sampling_sufficient': len(tracker.samples) >= 2,
        'numerical_variation_observed': summary['numerical_variation_observed'],
        'position_range_rev': summary['position_range_rev'],
        'terminal_historical_pair': summary['last_feedback'],
        'physical_variation_acceptance': 'review_required'}
    summary['action_stop_success'] = True
    summary['success'] = True
    summary['expected_terminal'] = 'FAULT/EXPIRED: bounded TTL revoked output and remains latched; no ackfault.'
    summary['interpretation'] = 'One bounded pulse with historical post-stop observation; numerical variation is not automatic physical/S2 acceptance.'


def session(link, mode, plan, journal, clock=time.monotonic):
    summary = {'mode': mode, 'source': 'live-vcp', 'success': False, 'pulse_attempted': False,
               'pulse_acknowledged': False, 'cancel_attempted': False}
    try:
        (qualify(link, summary, journal, clock) if mode == 'qualify' else trial(link, plan, summary, journal, clock))
    except (Exception, KeyboardInterrupt) as error:
        summary['error'] = type(error).__name__ + ': ' + str(error)
        journal.emit('ABORT', error=summary['error'])
        cleanup = 'qualcancel' if mode == 'qualify' else 'cancel'
        summary['cancel_attempted'] = True
        try:
            summary['cancel_reply'] = query(link, cleanup)
        except Exception as cleanup_error:
            summary['cancel_error'] = str(cleanup_error)
        if mode == 'qualify' and getattr(link, 'usable', True):
            try:
                summary['qualification_error_raw'] = tagged(query(link, 'qualerror'), 'qualerror')
            except Exception as evidence_error:
                summary['evidence_error'] = str(evidence_error)
    return summary


def binding_probe():
    # Bound the live-only native identity read; avoid the timed-out WMI provider.
    helper = HERE / 'native_vcp_binding.py'
    require(helper.is_file(), 'Native VCP identity helper is missing')
    result = subprocess.run([sys.executable, '-B', str(helper), '--probe'],
                            capture_output=True, text=True, timeout=5,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    require(result.returncode == 0, 'Read-only native binding probe failed: ' + result.stderr)
    binding = json.loads(result.stdout)
    require(binding.get('serial_verified') is True and binding.get('parent') == PARENT
            and len(binding.get('ports', [])) == 1, 'Wrong binding')
    return binding

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--mode', choices=('qualify', 'trial'))
    parser.add_argument('--powered-window-confirmed', action='store_true')
    parser.add_argument('--plan')
    parser.add_argument('--confirm-plan-sha256')
    parser.add_argument('--evidence')
    parser.add_argument('--serial-module-directory')
    args = parser.parse_args()
    if not args.live:
        require(not any((args.mode, args.powered_window_confirmed, args.plan, args.confirm_plan_sha256,
                         args.evidence, args.serial_module_directory)), 'Live-only argument without --live')
        import test_offline
        return test_offline.run_tests(sys.modules[__name__])
    require(args.mode is not None and args.powered_window_confirmed and args.evidence,
            'Live requires explicit mode, powered confirmation and a new evidence directory')
    if args.mode == 'trial':
        require(args.plan and args.confirm_plan_sha256, 'Trial requires frozen approved plan confirmation')
        plan = frozen_plan(args.plan, args.confirm_plan_sha256)
    else:
        require(not args.plan and not args.confirm_plan_sha256, 'Qualification must not use a trial plan')
        plan = None
    directory = pathlib.Path(args.evidence).resolve()
    require(not directory.exists(), 'Evidence already exists; never retry/resume')
    directory.mkdir(parents=True)
    journal = Journal(directory)
    link = None
    summary = {'mode': args.mode, 'source': 'live-vcp', 'success': False}
    try:
        binding = binding_probe()
        (directory / 'binding.json').write_text(json.dumps(binding, indent=2) + '\n', encoding='utf-8')
        module_directory = args.serial_module_directory or _reuse.SERIAL_ROOT
        require((pathlib.Path(module_directory) / 'serial' / '__init__.py').is_file(), 'Existing pyserial unavailable')
        sys.path.insert(0, module_directory)
        import serial  # Explicit live branch only, after all approval/plan validation.
        duration = 12.0 if args.mode == 'qualify' else plan['bounds']['total_seconds']
        maximum = 256 if args.mode == 'qualify' else plan['bounds']['max_queries']
        link = Link(binding['ports'][0]['DeviceID'], journal, args.mode, serial.Serial,
                    time.monotonic() + duration, maximum)
        summary = session(link, args.mode, plan, journal)
        summary['binding'] = binding
        summary['plan_sha256'] = plan['_sha256'] if plan else None
    except (Exception, KeyboardInterrupt) as error:
        summary['error'] = type(error).__name__ + ': ' + str(error)
        journal.emit('ABORT', error=summary['error'])
    finally:
        if link is not None:
            try:
                link.serial.close()
                summary['serial_closed'] = True
            except Exception as error:
                summary['success'] = False; summary['close_error'] = str(error)
            summary['commands_attempted'] = link.count
            summary['one_shots_attempted'] = sorted(link.once)
        summary['finished_utc'] = _reuse.utc()
        (directory / 'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n', encoding='utf-8')
        journal.emit('COMPLETE', success=summary['success'], summary=str(directory / 'summary.json'))
        journal.file.close()
    return 0 if summary['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
