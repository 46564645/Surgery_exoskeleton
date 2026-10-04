"""Three offline Link contracts; fake serial only, never device discovery."""
import argparse
import importlib.util
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
BAD_REPLY = (b'K shoulder profile=v22 verified=0 est=1 hb=1 wire_valid=1 '
             b'pos_mrad=0 vel_mrad_s=0 est_ms=212734 hb_ms=212694 state=1 '
             b'error=00000000 fault=0 output_allowed=0\r\n')


class Clock:
    def __init__(self): self.now = 0.0
    def __call__(self): return self.now


class Journal:
    def __init__(self): self.events = []
    def emit(self, event, **values): self.events.append(dict(event=event, **values))


class FakeSerial:
    def __init__(self, clock, **kwargs):
        self.clock = clock
        self._timeout, self._write_timeout = kwargs['timeout'], kwargs['write_timeout']
        self.setter_calls, self.writes = [], []
        self.dtr = self.rts = True
        self.is_open = False
    @property
    def timeout(self): return self._timeout
    @timeout.setter
    def timeout(self, value):
        self.setter_calls.append(('read', value))
        self._timeout = value
    @property
    def write_timeout(self): return self._write_timeout
    @write_timeout.setter
    def write_timeout(self, value):
        self.setter_calls.append(('write', value))
        self._write_timeout = value
    def open(self):
        if self.port != 'OFFLINE_FAKE' or self.dtr or self.rts:
            raise RuntimeError('Unexpected fake serial configuration')
        self.is_open = True
    def close(self): self.is_open = False
    def write(self, wire):
        self.writes.append(wire)
        self.clock.now += .01
        return len(wire)
    def read_until(self, expected, size):
        self.clock.now += .001
        replies = {b'ping\n': b'OK pong\r\n',
                   b'status\n': b'OK output_allowed=0\r\n',
                   b'shoulderfeedback\n': BAD_REPLY,
                   b'cancel\n': b'OK cancelled output_allowed=0\r\n'}
        return replies[self.writes[-1]][:size]


class LinkIntegrity(unittest.TestCase):
    def setUp(self):
        self.clock, self.journal = Clock(), Journal()
        self.link = CAPTURE.Link('OFFLINE_FAKE', self.journal, 'trial',
            lambda **kw: FakeSerial(self.clock, **kw), 5.0, 8, self.clock)
        self.serial = self.link.serial
    def tearDown(self): self.serial.close()
    def test_unchanged_timeouts_do_not_reconfigure(self):
        self.assertEqual(self.link.query('ping'), 'OK pong')
        self.assertEqual(self.link.query('status'), 'OK output_allowed=0')
        self.assertEqual(self.serial.setter_calls, [])
    def test_deadline_still_clips_each_timeout(self):
        self.clock.now = 4.8
        self.assertEqual(self.link.query('ping'), 'OK pong')
        self.assertEqual([kind for kind, _ in self.serial.setter_calls], ['write', 'read'])
        self.assertAlmostEqual(self.serial.setter_calls[0][1], .2)
        self.assertAlmostEqual(self.serial.setter_calls[1][1], .19)
        self.clock.now = 5.0
        with self.assertRaisesRegex(CAPTURE.CaptureError, 'budget exhausted'):
            self.link.query('status')
        self.assertEqual(self.serial.writes, [b'ping\n'])
    def test_bad_reply_latches_link_but_allows_one_cleanup(self):
        with self.assertRaisesRegex(CAPTURE.CaptureError, 'Non-OK response: K shoulder'):
            self.link.query('shoulderfeedback')
        recorded = [row for row in self.journal.events if row['event'] == 'REPLY']
        self.assertEqual(recorded[0]['raw_hex'], BAD_REPLY.hex())
        with self.assertRaisesRegex(CAPTURE.CaptureError, 'Unusable serial link'):
            self.link.query('status')
        self.assertFalse(self.link.usable)
        self.assertEqual(self.serial.writes, [b'shoulderfeedback\n'])
        self.assertEqual(self.link.query('cancel'), 'OK cancelled output_allowed=0')
        with self.assertRaisesRegex(CAPTURE.CaptureError, 'only be attempted once'):
            self.link.query('cancel')
        self.assertEqual(self.serial.writes, [b'shoulderfeedback\n', b'cancel\n'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--capture', type=Path, default=Path(__file__).with_name('capture.py'))
    args, remaining = parser.parse_known_args()
    spec = importlib.util.spec_from_file_location('_link_integrity_capture', args.capture)
    CAPTURE = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(CAPTURE)
    unittest.main(argv=[sys.argv[0], *remaining])
