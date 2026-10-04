"""Actual trace + adversarial host display checks. No serial, capture or hardware."""
import copy
import datetime
import contextlib
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import visible_capture
from visible_state import DisplayState, HOST_FRESH_S, timestamp

TRACE = [json.loads(line) for line in (Path(__file__).parent / 'fixtures/actual-events.jsonl').read_text(encoding='utf-8').splitlines()]
READY_INDEX = next(i for i, value in enumerate(TRACE) if value['event'] == 'READY_FOR_MOVEMENT')


class Clocks:
    def __init__(self):
        self.mono = TRACE[0]['host_monotonic_s'] - .010
        self.wall = timestamp(TRACE[0]['at_utc']) - .010

    def at(self, record, delay=0):
        self.mono = record['host_monotonic_s'] + delay
        self.wall = timestamp(record['at_utc']) + delay

    def advance(self, seconds):
        self.mono += seconds
        self.wall += seconds


def fresh_state():
    clock = Clocks()
    state = DisplayState(TRACE[0]['session_id'], monotonic=lambda: clock.mono, wall=lambda: clock.wall)
    return state, clock


def play_until_ready():
    state, clock = fresh_state()
    for record in TRACE[:READY_INDEX + 1]:
        clock.at(record)
        if not state.accept(record, clock.mono):
            raise AssertionError(state.detail)
    return state, clock


class DisplayTests(unittest.TestCase):
    def test_initial_invalid_warmup_waits_only_before_first_pair_within_200ms(self):
        first_index = next(i for i, value in enumerate(TRACE) if value['event'] == 'ENCODER_SAMPLE')
        for expiry in (False, True):
            with self.subTest(expiry=expiry):
                state, clock = fresh_state()
                for record in TRACE[:first_index]:
                    clock.at(record)
                    self.assertTrue(state.accept(record, clock.mono), state.detail)
                warmup = copy.deepcopy(TRACE[first_index])
                warmup['sample'].update(valid=0, pairs=0, tx=1, elapsed_ms=1)
                clock.at(warmup)
                self.assertTrue(state.accept(warmup, clock.mono), state.detail)
                self.assertFalse(state.ready)
                self.assertFalse(state.terminal)
                if expiry:
                    clock.advance(.201)
                    self.assertFalse(state.ready)
                    self.assertTrue(state.terminal)
                else:
                    for original in TRACE[first_index:READY_INDEX + 1]:
                        record = copy.deepcopy(original)
                        record['host_monotonic_s'] += .005
                        record['at_utc'] = datetime.datetime.fromtimestamp(timestamp(record['at_utc']) + .005,
                                                                         datetime.timezone.utc).isoformat()
                        clock.at(record)
                        self.assertTrue(state.accept(record, clock.mono), state.detail)
                    self.assertTrue(state.ready)
    def test_actual_trace_green_only_ready_then_actual_invalid_red(self):
        state, clock = fresh_state()
        ready_events = []
        invalid_seen = False
        for record in TRACE:
            clock.at(record)
            state.accept(record, clock.mono)
            if record['event'] == 'ENCODER_SAMPLE' and record['sample']['valid'] == 0:
                invalid_seen = True
                self.assertFalse(state.ready)
                self.assertTrue(state.terminal)
            if state.ready:
                ready_events.append(record['event'])
                self.assertFalse(invalid_seen)
            if record['event'] == 'READY_FOR_MOVEMENT':
                self.assertTrue(state.ready, state.detail)
        self.assertEqual(ready_events[0], 'READY_FOR_MOVEMENT')
        self.assertTrue(invalid_seen)
        self.assertFalse(state.ready)
        self.assertEqual(len(state.tracker.samples), 50)
        self.assertEqual(state.tracker.samples[-1]['pairs'], 98)

    def test_stall_revokes_within_budget_and_cannot_resurrect(self):
        state, clock = play_until_ready()
        clock.advance(HOST_FRESH_S)
        self.assertFalse(state.ready)
        self.assertTrue(state.terminal)
        record = TRACE[READY_INDEX + 2]
        clock.at(record)
        state.accept(record, clock.mono)
        self.assertFalse(state.ready)

    def test_utc_or_receipt_delayed_ready_never_green(self):
        for kind in ('utc', 'source', 'receipt'):
            with self.subTest(kind=kind):
                state, clock = fresh_state()
                for record in TRACE[:READY_INDEX]:
                    clock.at(record)
                    self.assertTrue(state.accept(record, clock.mono), state.detail)
                record = copy.deepcopy(TRACE[READY_INDEX])
                clock.at(record)
                receipt = clock.mono
                if kind == 'utc':
                    record['at_utc'] = TRACE[0]['at_utc']
                elif kind == 'source':
                    record['host_monotonic_s'] -= .250
                else:
                    receipt -= .250
                self.assertFalse(state.accept(record, receipt))
                self.assertFalse(state.ready)

    def test_backlog_actual_session_received_seconds_late_never_green(self):
        state, clock = fresh_state()
        for record in TRACE[:READY_INDEX + 1]:
            clock.at(record, 3)
            state.accept(record, clock.mono)
            self.assertFalse(state.ready)

    def test_wrong_session_and_ready_pair_mismatch(self):
        for kind in ('session', 'pair', 'distinct', 'stamp', 'output'):
            with self.subTest(kind=kind):
                state, clock = fresh_state()
                for record in TRACE[:READY_INDEX]:
                    clock.at(record)
                    self.assertTrue(state.accept(record, clock.mono), state.detail)
                record = copy.deepcopy(TRACE[READY_INDEX])
                if kind == 'session':
                    record['session_id'] = 'prior-run'
                elif kind == 'pair':
                    record['latest_pair']['pairs'] += 1
                elif kind == 'distinct':
                    record['distinct_pairs'] = 1
                elif kind == 'stamp':
                    record['latest_pair']['pos_ms'] += 1
                else:
                    record['output_allowed'] = 1
                clock.at(record)
                self.assertFalse(state.accept(record, clock.mono))
                self.assertFalse(state.ready)

    def test_reordered_event_and_nonadvancing_pair_abort(self):
        for kind in ('order', 'stamp', 'counter'):
            with self.subTest(kind=kind):
                state, clock = play_until_ready()
                record = copy.deepcopy(next(x for x in TRACE[READY_INDEX + 1:] if x['event'] == 'ENCODER_SAMPLE'))
                if kind == 'order':
                    record['host_monotonic_s'] = TRACE[READY_INDEX]['host_monotonic_s']
                elif kind == 'stamp':
                    record['sample']['pos_ms'] = state.tracker.samples[-1]['pos_ms']
                else:
                    record['sample']['pairs'] = 1
                clock.at(record)
                self.assertFalse(state.accept(record, clock.mono))
                self.assertFalse(state.ready)

    def test_runtime_invalid_age_and_flags_immediately_red(self):
        for key, value in (('active', 0), ('reason', 5), ('valid', 0), ('age_ms', 101),
                           ('rejected', 1), ('output_allowed', 1), ('pos_rev', 'nan')):
            with self.subTest(key=key):
                state, clock = play_until_ready()
                record = copy.deepcopy(next(x for x in TRACE[READY_INDEX + 1:] if x['event'] == 'ENCODER_SAMPLE'))
                record['sample'][key] = value
                clock.at(record)
                self.assertFalse(state.accept(record, clock.mono))
                self.assertFalse(state.ready)

    def test_terminal_events_and_exit_cannot_reenable(self):
        for terminal in ('ABORT', 'COMPLETE', 'FIRMWARE_BOUNDED_END', 'exit'):
            with self.subTest(terminal=terminal):
                state, clock = play_until_ready()
                if terminal == 'exit':
                    state.process_exit(1)
                else:
                    # Even a delayed terminal event must withdraw the prompt.
                    state.accept(dict(TRACE[0], event=terminal), clock.mono)
                self.assertFalse(state.ready)
                state.accept(TRACE[READY_INDEX], clock.mono)
                self.assertFalse(state.ready)

    def test_clock_jumps_and_monotonic_deadline_latch_red(self):
        for jump in (-5, 5):
            with self.subTest(jump=jump):
                state, clock = play_until_ready()
                clock.wall += jump
                self.assertFalse(state.ready)
                clock.wall -= jump
                self.assertFalse(state.ready)
        state, clock = play_until_ready()
        clock.advance(state.stream_deadline - clock.mono)
        self.assertFalse(state.ready)
        self.assertEqual(state.phase, 'COMPLETE')

    def test_default_window_arguments_do_not_start_process_and_live_needs_both(self):
        with patch('visible_capture.subprocess.Popen') as process:
            args = visible_capture.arguments([])
            self.assertFalse(args.live)
            self.assertFalse(args.demo)
            self.assertFalse(args.powered_window_confirmed)
            process.assert_not_called()
        for argv in (['--live'], ['--powered-window-confirmed'],
                     ['--live', '--powered-window-confirmed', '--demo']):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                visible_capture.arguments(argv)
        self.assertTrue(visible_capture.arguments(['--live', '--powered-window-confirmed']).live)
        command = visible_capture.child_command(Path(__file__).parent / 'evidence-unit', 'python')
        self.assertNotIn('-ExecutionPolicy', command)
        self.assertIn('capture.ps1', Path(command[5]).name)


if __name__ == '__main__':
    unittest.main(verbosity=2)
