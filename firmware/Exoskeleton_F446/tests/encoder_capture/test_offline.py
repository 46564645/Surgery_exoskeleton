"""Synthetic fixtures only. This module never imports pyserial or opens hardware."""
import argparse
import contextlib
import io
import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch
import capture


def encoder(pairs=1, **overrides):
    fields = dict(src='encoder_sdo', active=1, reason=0, valid=1, pairs=pairs,
                  pos_rev=str(pairs / 100), vel_rev_s='0.100000',
                  pos_ms=100 + pairs * 50, vel_ms=101 + pairs * 50,
                  age_ms=1, elapsed_ms=pairs * 50, tx=2 * pairs, rejected=0, output_allowed=0)
    fields.update(overrides)
    return 'OK ' + ' '.join(str(k) + '=' + str(v) for k, v in fields.items())


class Journal:
    def emit(self, *args, **kwargs):
        pass


class FakeSerial:
    def __init__(self, **kwargs):
        self.is_open = False
        self.commands = []
        self.started = False
        self.pairs = 0
        self.block_start = False
        self.invalid_after_first = False
    def open(self):
        self.is_open = True
    def close(self):
        self.is_open = False
    def write(self, data):
        self.command = data.decode().strip()
        self.commands.append(self.command)
        return len(data)
    def read_until(self, *_):
        c = self.command
        if c == 'encstart':
            self.started = not self.block_start
            text = 'ERR encoder_stream_blocked' if self.block_start else 'OK encoder_stream_started output_allowed=0'
        elif c == 'encstop':
            self.started = False
            text = 'OK encoder_stream_stopped output_allowed=0'
        elif c == 'encfeedback':
            if self.started:
                self.pairs += 1
                text = encoder(self.pairs, valid=0) if self.invalid_after_first and self.pairs > 1 else encoder(self.pairs)
            else:
                text = encoder(0, active=0, valid=0, tx=0, elapsed_ms=0)
        elif c == 'ping':
            text = 'OK pong'
        elif c == 'status':
            text = 'OK stage=S2_CAN_RX uptime_ms=1000 output_allowed=0 uart_errors=0 rx_overflows=0 tx_timeouts=0 scheduler_overruns=0'
        elif c == 'canstatus':
            text = 'OK can1_started=1 can1_baud=500000 can1_start_error=0 can2_started=1 can2_baud=1000000 can2_start_error=0 output_allowed=0'
        elif c == 'outputstatus':
            text = 'OK stage=S2_CAN_RX s3_state=0 configured=0 transport_bound=0 output_allowed=0 request_valid=0 prep_state=0 arm_token=0'
        elif c == 'shoulderfeedback':
            text = 'OK shoulder profile=v22 verified=0 est=1 hb=1 wire_valid=1 pos_mrad=0 vel_mrad_s=0 est_ms=998 hb_ms=999 state=1 error=00000000 fault=0 output_allowed=0'
        elif c in ('shoulder', 'elbow'):
            text = 'OK bus=' + c + ' started=1 start_error=0 rx=100 drop=0 invalid=0 fov=0 err=0 bus_off=0 seen=1 id=029 dlc=8 data=0000000000000000 age_ms=1'
        else:
            raise AssertionError('Unexpected command ' + c)
        return (text + '\r\n').encode('ascii')


class Tests(unittest.TestCase):
    def test_whitelist_zero_arguments_and_once(self):
        link = capture.Link('COM999', Journal(), 'capture', FakeSerial)
        for command in ('arm', 'prepare', 'shoulderpulse', 'shoulderreadpos', 'encstart 1', 'reset', 'encstop\narm'):
            with self.assertRaises(capture.CaptureError):
                link.query(command)
        self.assertEqual(link.serial.commands, [])
        link.serial.block_start = True
        with self.assertRaises(capture.CaptureError):
            link.query('encstart')
        with self.assertRaises(capture.CaptureError):
            link.query('encstart')
        link.query('encstop')
        with self.assertRaises(capture.CaptureError):
            link.query('encstop')
        self.assertEqual(link.serial.commands, ['encstart', 'encstop'])
        prepare = capture.Link('COM999', Journal(), 'prepare', FakeSerial)
        for command in ('encstart', 'encstop'):
            with self.assertRaises(capture.CaptureError):
                prepare.query(command)

    def test_parser_rejects_invalid_fields_and_limits(self):
        capture.feedback(encoder())
        for text in ('ERR blocked', encoder() + ' pairs=2', encoder(tx=1), encoder(age_ms=101),
                     encoder(pos_rev='nan'), encoder(vel_ms=140, pos_ms=150),
                     encoder(rejected=1), encoder(pairs=601, tx=1202), encoder(output_allowed=1)):
            with self.subTest(text=text), self.assertRaises(capture.CaptureError):
                capture.feedback(text)

    def test_pair_counter_timestamps_and_host_staleness(self):
        tracker = capture.Tracker()
        first = capture.feedback(encoder())
        self.assertTrue(tracker.observe(first, 0))
        self.assertFalse(tracker.observe(first, .05))
        with self.assertRaises(capture.CaptureError):
            tracker.observe(first, .101)
        tracker = capture.Tracker()
        tracker.observe(first, 0)
        with self.assertRaises(capture.CaptureError):
            tracker.observe(capture.feedback(encoder(2, pos_ms=150, vel_ms=151)), .05)
        tracker = capture.Tracker()
        tracker.observe(first, 0)
        with self.assertRaises(capture.CaptureError):
            tracker.observe(capture.feedback(encoder(pos_rev='.2')), .05)
        tracker = capture.Tracker()
        tracker.observe(first, 0)
        self.assertTrue(tracker.observe(capture.feedback(encoder(3)), .09))
        self.assertEqual(len(tracker.samples), 2)  # Skipped MCU pairs are not fabricated.

    def test_previous_committed_pair_with_pending_next_velocity(self):
        # The next position and velocity requests may both be transmitted
        # while feedback still publishes the previous complete pair.
        for skew in (50, 75, 100):
            with self.subTest(skew_ms=skew):
                sample = capture.feedback(encoder(2, tx=6, pos_ms=200,
                                                   vel_ms=200 + skew, age_ms=skew))
                self.assertEqual(sample['pairs'], 2)
                self.assertEqual(sample['tx'], 6)
        for text in (encoder(2, tx=7), encoder(2, tx=6, pos_ms=200, vel_ms=301)):
            with self.subTest(reply=text), self.assertRaises(capture.CaptureError):
                capture.feedback(text)

    def test_malformed_serial_reply_invalidates_link(self):
        for raw in (b'OK partial', b'OK ' + b'x' * 1021 + b'\n', b'OK \xff\n'):
            with self.subTest(raw_prefix=raw[:16]):
                link = capture.Link('COM999', Journal(), 'capture', FakeSerial)
                link.serial.read_until = lambda *args, reply=raw: reply
                with self.assertRaises(capture.CaptureError):
                    link.query('ping')
                self.assertFalse(link.usable)
                with self.assertRaises(capture.CaptureError):
                    link.query('encstop')
                self.assertEqual(link.serial.commands, ['ping'])

    def test_analysis_uses_measured_signed_values(self):
        samples = [capture.feedback(encoder(1, pos_rev='-.5', vel_rev_s='-.1')),
                   capture.feedback(encoder(2, pos_rev='-.6', vel_rev_s='-.1'))]
        result = capture.analysis(samples)
        self.assertEqual(result['position_min_rev'], -.6)
        self.assertEqual(result['position_max_rev'], -.5)
        self.assertAlmostEqual(result['position_net_change_rev'], -.1)
        self.assertEqual(result['relationship_counts']['same_sign'], 1)

    def test_heartbeat_budget_is_separate_from_legacy_wire_valid(self):
        link = capture.Link('COM999', Journal(), 'capture', FakeSerial)
        hb = dict(hb='1', state='1', error='00000000', fault='0', output_allowed='0',
                  hb_ms='750', wire_valid='0')
        capture.heartbeat_check(link, hb)
        hb['hb_ms'] = '749'
        with self.assertRaises(capture.CaptureError):
            capture.heartbeat_check(link, hb)

    def test_invalid_after_acquisition_aborts_without_ready_and_stops_once(self):
        instance = FakeSerial()
        instance.invalid_after_first = True
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            binding = root / 'binding.json'
            binding.write_text(json.dumps({'ports': [{'DeviceID': 'COM999', 'Status': 'OK',
                               'ConfigManagerErrorCode': 0}], 'serial_verified': True,
                               'parent': capture.PARENT}), encoding='utf-8')
            args = argparse.Namespace(binding=str(binding), evidence=str(root), mode='capture',
                                      powered_window_confirmed=True)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(capture.run(args, lambda **kwargs: instance), 1)
            events = [json.loads(line) for line in (root / 'events.jsonl').read_text().splitlines()]
            self.assertNotIn('READY_FOR_MOVEMENT', [e['event'] for e in events])
            self.assertIn('ABORT', [e['event'] for e in events])
            self.assertEqual(instance.commands.count('encstart'), 1)
            self.assertEqual(instance.commands.count('encstop'), 1)

    def test_synthetic_prepare_and_capture_end_to_end(self):
        instances = []
        def factory(**kwargs):
            value = FakeSerial(**kwargs)
            instances.append(value)
            return value
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            binding = root / 'binding.json'
            binding.write_text(json.dumps({'ports': [{'DeviceID': 'COM999', 'Status': 'OK',
                               'ConfigManagerErrorCode': 0}], 'serial_verified': True,
                               'parent': capture.PARENT}), encoding='utf-8')
            for mode in ('prepare', 'capture'):
                evidence = root / mode
                evidence.mkdir()
                args = argparse.Namespace(binding=str(binding), evidence=str(evidence), mode=mode,
                                          powered_window_confirmed=mode == 'capture')
                with contextlib.redirect_stdout(io.StringIO()), patch.object(capture, 'MAX_SECONDS', .35):
                    self.assertEqual(capture.run(args, factory), 0)
                events = [json.loads(line) for line in (evidence / 'events.jsonl').read_text().splitlines()]
                summary = json.loads((evidence / 'summary.json').read_text())
                if mode == 'prepare':
                    self.assertNotIn('encstart', instances[-1].commands)
                    self.assertNotIn('encstop', instances[-1].commands)
                else:
                    self.assertEqual(instances[-1].commands.count('encstart'), 1)
                    self.assertEqual(instances[-1].commands.count('encstop'), 1)
                    ready = [e for e in events if e['event'] == 'READY_FOR_MOVEMENT']
                    self.assertEqual(len(ready), 1)
                    self.assertGreaterEqual(ready[0]['distinct_pairs'], 2)
                    self.assertTrue(summary['stop_acknowledged'])
                    self.assertTrue(summary['ready_for_movement'])
                self.assertTrue(set(instances[-1].commands) <= capture.COMMANDS)


if __name__ == '__main__':
    unittest.main(verbosity=2)
