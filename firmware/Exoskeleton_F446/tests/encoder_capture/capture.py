"""Bounded, zero-argument S2 encoder capture. No hardware runs on import."""
import argparse
import datetime
import json
import math
import pathlib
import re
import sys
import time

SERIAL_ROOT = r'H:\Desktop\Surgery_exoskeleton-git\firmware\Exoskeleton_F446\build\s2-tools\python'
PARENT = r'USB\VID_0483&PID_374B\066EFF555748874987182240'
READ_COMMANDS = frozenset(('ping', 'status', 'canstatus', 'outputstatus',
                            'shoulder', 'elbow', 'shoulderfeedback', 'encfeedback'))
COMMANDS = READ_COMMANDS | {'encstart', 'encstop'}
MAX_SECONDS = 30.0


class CaptureError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise CaptureError(message)


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def parse(reply):
    require(reply.startswith('OK '), 'Non-OK response: ' + reply)
    fields = {}
    for token in reply.split()[1:]:
        if '=' not in token:
            continue
        key, value = token.split('=', 1)
        require(key and value and key not in fields, 'Empty or duplicate field')
        fields[key] = value
    return fields


def fields_equal(fields, expected):
    for key, value in expected.items():
        require(fields.get(key) == str(value), 'Unexpected ' + key)


def uint(fields, key):
    text = fields.get(key, '')
    require(bool(re.fullmatch(r'[0-9]+', text)), 'Invalid integer ' + key)
    value = int(text)
    require(value <= 0xffffffff, 'Integer out of range ' + key)
    return value


def forward(new, old):
    delta = (new - old) & 0xffffffff
    require(0 < delta < 0x80000000, 'Timestamp failed to advance')
    return delta


def feedback(reply):
    fields = parse(reply)
    fields_equal(fields, {'src': 'encoder_sdo', 'output_allowed': 0})
    for key in ('active', 'reason', 'valid', 'pairs', 'pos_ms', 'vel_ms',
                'age_ms', 'elapsed_ms', 'tx', 'rejected'):
        fields[key] = uint(fields, key)
    require(fields['active'] in (0, 1) and fields['valid'] in (0, 1), 'Invalid flags')
    require(fields['pairs'] <= 600 and fields['tx'] <= 1200, 'Firmware count exceeds session bound')
    require(fields['rejected'] == 0, 'Rejected CAN response')
    if fields['valid']:
        for key in ('pos_rev', 'vel_rev_s'):
            try:
                fields[key] = float(fields[key])
            except (KeyError, ValueError):
                raise CaptureError('Invalid encoder float ' + key)
            require(math.isfinite(fields[key]), 'Nonfinite encoder float')
        require(fields['active'] == 1 and fields['reason'] == 0, 'Invalid active encoder state')
        require(fields['age_ms'] <= 100, 'Stale encoder age')
        require(((fields['vel_ms'] - fields['pos_ms']) & 0xffffffff) <= 100,
                'Position/velocity pair order or latency')
        require(fields['pairs'] > 0 and 2 * fields['pairs'] <= fields['tx'] <= 2 * fields['pairs'] + 2,
                'Pair/TX counters disagree')
    return fields


class Tracker:
    def __init__(self):
        self.samples = []
        self.last_new = None
        self.last_seen = None

    def observe(self, sample, now):
        if self.last_seen:
            for key in ('pairs', 'tx', 'elapsed_ms'):
                require(sample[key] >= self.last_seen[key], 'Counter regressed: ' + key)
        self.last_seen = sample
        if self.samples:
            previous = self.samples[-1]
            if sample['pairs'] == previous['pairs']:
                require((sample['pos_ms'], sample['vel_ms'], sample['pos_rev'], sample['vel_rev_s']) ==
                        (previous['pos_ms'], previous['vel_ms'], previous['pos_rev'], previous['vel_rev_s']),
                        'Cached pair changed without a new pair counter')
                require(now - self.last_new <= .100, 'Pair timestamps stopped advancing')
                return False
            forward(sample['pos_ms'], previous['pos_ms'])
            forward(sample['vel_ms'], previous['vel_ms'])
        self.samples.append(dict(sample))
        self.last_new = now
        return True


class Journal:
    def __init__(self, directory):
        self.directory = pathlib.Path(directory)
        self.session_id = self.directory.resolve().name
        self.file = (self.directory / 'events.jsonl').open('x', encoding='utf-8')

    def emit(self, event, **values):
        record = {'event': event, 'session_id': self.session_id, 'at_utc': utc(),
                  'host_monotonic_s': time.monotonic(), **values}
        line = json.dumps(record, ensure_ascii=True, allow_nan=False)
        self.file.write(line + '\n')
        self.file.flush()
        print(line, flush=True)


class Link:
    def __init__(self, port, journal, mode, serial_factory):
        self.journal, self.mode = journal, mode
        self.start_attempted = self.stop_attempted = False
        self.usable = True
        self.serial = serial_factory(port=None, baudrate=115200, timeout=.35, write_timeout=.35,
                                     bytesize=8, parity='N', stopbits=1, xonxoff=False,
                                     rtscts=False, dsrdtr=False)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()

    def query(self, command):
        require(command in COMMANDS, 'Command outside fixed whitelist')
        require(self.mode == 'capture' or command in READ_COMMANDS, 'Preparation cannot start or stop')
        require(self.usable, 'Serial link unusable after transport or framing failure')
        if command == 'encstart':
            require(not self.start_attempted, 'Start may only be attempted once')
            self.start_attempted = True  # Before write; uncertain delivery cannot retry.
        if command == 'encstop':
            require(not self.stop_attempted, 'Stop may only be attempted once')
            self.stop_attempted = True
        self.journal.emit('COMMAND', command=command)
        raw = b''
        try:
            require(self.serial.write((command + '\n').encode('ascii')) == len(command) + 1,
                    'Incomplete serial write')
            raw = self.serial.read_until(b'\n', 1025)
        except Exception:
            self.usable = False
            self.journal.emit('SERIAL_EXCEPTION', command=command, partial_hex=raw.hex())
            raise
        self.journal.emit('REPLY', command=command, raw_hex=raw.hex(),
                          raw_reply=raw.decode('ascii', errors='backslashreplace'))
        if not raw.endswith(b'\n') or len(raw) > 1024:
            self.usable = False
            raise CaptureError('Missing/oversize serial reply')
        try:
            reply = raw.decode('ascii').rstrip('\r\n')
        except UnicodeDecodeError:
            self.usable = False
            raise CaptureError('Non-ASCII reply')
        parse(reply)
        return reply


def output_check(link):
    fields_equal(parse(link.query('outputstatus')),
                 {'stage': 'S2_CAN_RX', 's3_state': 0, 'configured': 0, 'transport_bound': 0,
                  'output_allowed': 0, 'request_valid': 0, 'prep_state': 0, 'arm_token': 0})


def can_snapshot(link):
    result = {}
    for command in ('shoulder', 'elbow'):
        f = parse(link.query(command))
        fields_equal(f, {'bus': command, 'started': 1, 'start_error': 0, 'bus_off': 0})
        for key in ('rx', 'drop', 'invalid', 'fov', 'err'):
            f[key] = uint(f, key)
        result[command] = f
    return result


def check_can_delta(first, current):
    for bus in ('shoulder', 'elbow'):
        for key in ('rx', 'drop', 'invalid', 'fov', 'err'):
            require(current[bus][key] >= first[bus][key], 'CAN counter regressed')
        for key in ('drop', 'invalid', 'fov', 'err'):
            require(current[bus][key] == first[bus][key], 'New CAN fault: ' + bus + '/' + key)


def preflight(link, powered):
    require(link.query('ping') == 'OK pong', 'Unexpected ping')
    status = parse(link.query('status'))
    fields_equal(status, {'stage': 'S2_CAN_RX', 'output_allowed': 0, 'uart_errors': 0,
                         'rx_overflows': 0, 'tx_timeouts': 0, 'scheduler_overruns': 0})
    uptime = uint(status, 'uptime_ms')
    fields_equal(parse(link.query('canstatus')),
                 {'can1_started': 1, 'can1_baud': 500000, 'can1_start_error': 0,
                  'can2_started': 1, 'can2_baud': 1000000, 'can2_start_error': 0, 'output_allowed': 0})
    output_check(link)
    legacy = parse(link.query('shoulderfeedback'))
    fields_equal(legacy, {'output_allowed': 0})
    if powered:
        heartbeat_check(link, legacy)
        # Final encstart enforces the MCU-local 250 ms heartbeat gate. This
        # earlier passive snapshot is preserved separately, including 029 zero.
    can = can_snapshot(link)
    state = feedback(link.query('encfeedback'))
    require(state['active'] == 0 and state['reason'] == 0 and state['pairs'] == 0 and state['tx'] == 0,
            'Stream already used or active; no restart/resume')
    return {'uptime_ms': uptime, 'legacy_029_observation': legacy, 'can': can, 'encoder': state}


def heartbeat_check(link, hb):
    fields_equal(hb, {'hb': 1, 'state': 1, 'error': '00000000', 'fault': 0, 'output_allowed': 0})
    status = parse(link.query('status'))
    fields_equal(status, {'output_allowed': 0, 'uart_errors': 0, 'rx_overflows': 0,
                         'tx_timeouts': 0, 'scheduler_overruns': 0})
    age = (uint(status, 'uptime_ms') - uint(hb, 'hb_ms')) & 0xffffffff
    require(age <= 250, 'Heartbeat older than 250 ms')


def analysis(samples):
    if not samples:
        return {'distinct_observed_pairs': 0, 'numerical_variation_observed': False,
                'physical_variation_acceptance': 'review_required'}
    positions = [s['pos_rev'] for s in samples]
    velocities = [s['vel_rev_s'] for s in samples]
    moving = []
    for a, b in zip(samples, samples[1:]):
        delta = b['pos_rev'] - a['pos_rev']
        dt = ((b['pos_ms'] - a['pos_ms']) & 0xffffffff) / 1000
        mean_velocity = (a['vel_rev_s'] + b['vel_rev_s']) / 2
        if delta != 0 or a['vel_rev_s'] != 0 or b['vel_rev_s'] != 0:
            relation = ('same_sign' if delta * mean_velocity > 0 else
                        'opposite_sign' if delta * mean_velocity < 0 else 'zero_or_unresolved')
            moving.append({'from_pair': a['pairs'], 'to_pair': b['pairs'], 'delta_rev': delta,
                           'dt_s': dt, 'observed_delta_rev_s': delta / dt,
                           'mean_sampled_vel_rev_s': mean_velocity, 'signed_relationship': relation})
    return {'distinct_observed_pairs': len(samples), 'first_pair': samples[0], 'last_pair': samples[-1],
            'position_min_rev': min(positions), 'position_max_rev': max(positions),
            'position_range_rev': max(positions) - min(positions),
            'position_net_change_rev': positions[-1] - positions[0],
            'velocity_min_rev_s': min(velocities), 'velocity_max_rev_s': max(velocities),
            'numerical_variation_observed': bool(moving), 'moving_intervals': moving,
            'physical_variation_acceptance': 'review_required',
            'relationship_counts': {key: sum(m['signed_relationship'] == key for m in moving)
                                    for key in ('same_sign', 'opposite_sign', 'zero_or_unresolved')},
            'scope': 'Observed samples only; no amplitude acceptance threshold or hardware acceptance claim. '
                     'MCU float text quantization can leave zero/unresolved relationships.'}


def run(args, serial_factory):
    binding = json.loads(pathlib.Path(args.binding).read_text(encoding='utf-8-sig'))
    ports = binding['ports']
    require(len(ports) == 1 and binding['serial_verified'] is True and binding['parent'] == PARENT,
            'Invalid fixed ST-LINK binding')
    p = ports[0]
    require(re.fullmatch(r'COM[0-9]+', p['DeviceID']) and p['Status'] == 'OK' and
            p['ConfigManagerErrorCode'] == 0, 'Unhealthy serial binding')
    require(args.mode != 'capture' or args.powered_window_confirmed,
            'Main-thread powered window must be confirmed')
    journal = Journal(args.evidence)
    journal.emit('SESSION', mode=args.mode, port=p['DeviceID'], binding=binding,
                 max_duration_s=30, max_can_requests=1200, host_feedback_hz=10,
                 power_condition='Main-thread explicit confirmation' if args.mode == 'capture' else 'USB-only preparation; user power condition external')
    link = None
    tracker = Tracker()
    summary = {'mode': args.mode, 'session_id': journal.session_id, 'success': False,
               'ready_for_movement': False, 'physical_variation_acceptance': 'review_required'}
    try:
        link = Link(p['DeviceID'], journal, args.mode, serial_factory)
        pre = preflight(link, args.mode == 'capture')
        summary['preflight'] = pre
        summary['first_can'] = summary['last_can'] = pre['can']
        journal.emit('PASSIVE_CHECKS', checks=pre)
        if args.mode == 'prepare':
            summary['success'] = True
            journal.emit('PREPARED_USB_ONLY', can_read_requests_sent=0, output_allowed=0)
        else:
            # Refresh heartbeat immediately before start; firmware decides exact
            # MCU-local age and excludes stale/wrong-state HB without retries.
            hb = parse(link.query('shoulderfeedback'))
            heartbeat_check(link, hb)
            require(link.query('encstart') == 'OK encoder_stream_started output_allowed=0', 'Start blocked')
            begun = time.monotonic()
            deadline = begun + MAX_SECONDS
            next_poll = begun
            next_stats = begun + 1
            while time.monotonic() < deadline:
                now = time.monotonic()
                if now < next_poll:
                    time.sleep(min(next_poll - now, deadline - now))
                sample = feedback(link.query('encfeedback'))
                now = time.monotonic()
                journal.emit('ENCODER_SAMPLE', sample=sample)
                if sample['active'] == 0 and sample['reason'] == 2 and (
                        sample['elapsed_ms'] >= 29900 or sample['tx'] == 1200):
                    journal.emit('FIRMWARE_BOUNDED_END', sample=sample)
                    break
                require(sample['active'] == 1 and sample['reason'] == 0, 'Encoder stream stopped unexpectedly')
                if not sample['valid']:
                    require(not tracker.samples and now - begun <= .200, 'Invalid encoder acquisition')
                else:
                    new = tracker.observe(sample, now)
                    if new and len(tracker.samples) >= 2 and not summary['ready_for_movement']:
                        # Fresh CAN/output snapshot before publishing readiness.
                        current_can = can_snapshot(link)
                        summary['last_can'] = current_can
                        check_can_delta(summary['first_can'], current_can)
                        output_check(link)
                        require(time.monotonic() - now <= .100, 'Readiness evidence aged during passive checks')
                        summary['ready_for_movement'] = True
                        journal.emit('READY_FOR_MOVEMENT', output_allowed=0,
                                     distinct_pairs=len(tracker.samples), first_pair=tracker.samples[0],
                                     latest_pair=tracker.samples[-1],
                                     instruction='Main thread may now ask for one small manual variation.')
                if now >= next_stats:
                    summary['last_can'] = can_snapshot(link)
                    check_can_delta(summary['first_can'], summary['last_can'])
                    output_check(link)
                    system = parse(link.query('status'))
                    fields_equal(system, {'output_allowed': 0, 'uart_errors': 0, 'rx_overflows': 0,
                                           'tx_timeouts': 0, 'scheduler_overruns': 0})
                    next_stats = now + 1
                next_poll = max(next_poll + .100, time.monotonic())
            require(summary['ready_for_movement'], 'Ended without two fresh complete pairs')
            summary['success'] = True
    except (Exception, KeyboardInterrupt) as error:
        summary['error'] = type(error).__name__ + ': ' + str(error)
        journal.emit('ABORT', error=summary['error'], required_output_allowed=0)
    finally:
        if link is not None:
            if link.start_attempted and not link.stop_attempted and link.usable and link.serial.is_open:
                try:
                    require(link.query('encstop') == 'OK encoder_stream_stopped output_allowed=0', 'Bad stop reply')
                    summary['stop_acknowledged'] = True
                except Exception as error:
                    summary['success'] = False
                    summary['stop_error'] = type(error).__name__ + ': ' + str(error)
            summary['start_attempted'] = link.start_attempted
            summary['stop_attempted'] = link.stop_attempted
            if link.usable and link.serial.is_open:
                try:
                    if link.start_attempted:
                        summary['final_encoder_status'] = feedback(link.query('encfeedback'))
                        require(summary['final_encoder_status']['active'] == 0,
                                'Encoder still active after termination')
                    summary['last_can'] = can_snapshot(link)
                    if 'first_can' in summary:
                        check_can_delta(summary['first_can'], summary['last_can'])
                    output_check(link)
                except Exception as error:
                    summary['success'] = False
                    summary['final_query_error'] = str(error)
            link.serial.close()
        summary['encoder_analysis'] = analysis(tracker.samples)
        summary['session_completed_without_fault'] = summary['success']
        summary['finished_utc'] = utc()
        (pathlib.Path(args.evidence) / 'summary.json').write_text(
            json.dumps(summary, indent=2, allow_nan=False) + '\n', encoding='utf-8')
        journal.emit('COMPLETE', success=summary['success'], summary_path=str(pathlib.Path(args.evidence) / 'summary.json'))
        journal.file.close()
    return 0 if summary['success'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('prepare', 'capture'), default='prepare')
    parser.add_argument('--binding', required=True)
    parser.add_argument('--evidence', required=True)
    parser.add_argument('--powered-window-confirmed', action='store_true')
    parser.add_argument('--serial-module-directory', help='Existing pyserial module directory; read only')
    args = parser.parse_args()
    module_directory = args.serial_module_directory
    if not module_directory:
        for ancestor in pathlib.Path(__file__).resolve().parents:
            for suffix in ('build/s2-tools/python', 'firmware/Exoskeleton_F446/build/s2-tools/python'):
                candidate = ancestor / suffix
                if (candidate / 'serial/__init__.py').is_file():
                    module_directory = str(candidate)
                    break
            if module_directory:
                break
    if not module_directory and pathlib.Path(SERIAL_ROOT).is_dir():
        module_directory = SERIAL_ROOT  # Optional fallback for this Windows workspace.
    if module_directory:
        sys.path.insert(0, module_directory)
    import serial
    return run(args, serial.Serial)


if __name__ == '__main__':
    sys.exit(main())
