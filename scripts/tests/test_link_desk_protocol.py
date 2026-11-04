import importlib.util
from pathlib import Path
import re
import struct
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).absolute().parents[2]
SPEC = importlib.util.spec_from_file_location('desk_protocol_check', ROOT / 'scripts/link_desk.py')
DESK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DESK)


class DeskProtocolTests(unittest.TestCase):
    def test_usb_client_announces_current_firmware_protocol(self):
        header = (ROOT / 'lib/core/src/link_msg.h').read_text()
        protocol = int(re.search(r'kProtocol\s*=\s*(\d+)', header).group(1))
        self.assertEqual(protocol, DESK.parse_hello(DESK.hello_payload())[0])


class DeskHelloClockTests(unittest.TestCase):
    def link(self, replies):
        link = DESK.Link.__new__(DESK.Link)
        self.writes = []
        link.ser = SimpleNamespace(write=self.writes.append)
        link.tx_lock = threading.Lock()
        link.seq = 0
        link.wait = Mock(side_effect=replies)
        return link

    def sent(self):
        return [DESK.decode_frame(packet[:-1]) for packet in self.writes]

    def test_first_success_initializes_clock_before_return(self):
        reply = DESK.hello_payload()
        link = self.link([reply])
        with patch.object(DESK, 'now_us', return_value=123456789):
            self.assertEqual(DESK.parse_hello(reply), link.hello())
        self.assertEqual([(DESK.MSG['Hello'], 0, DESK.hello_payload()),
                          (DESK.MSG['Time'], 1, struct.pack('<Q', 123456789))], self.sent())
        link.wait.assert_called_once_with('Hello', 1.0)

    def test_retry_initializes_clock_only_after_success(self):
        reply = DESK.hello_payload()
        link = self.link([None, reply])
        with patch.object(DESK, 'now_us', return_value=987654321):
            self.assertEqual(DESK.parse_hello(reply), link.hello())
        self.assertEqual([(DESK.MSG['Hello'], 0, DESK.hello_payload()),
                          (DESK.MSG['Hello'], 1, DESK.hello_payload()),
                          (DESK.MSG['Time'], 2, struct.pack('<Q', 987654321))], self.sent())
        self.assertEqual(2, link.wait.call_count)

    def test_timeout_keeps_three_attempts_without_time(self):
        link = self.link([None, None, None])
        self.assertIsNone(link.hello())
        self.assertEqual([(DESK.MSG['Hello'], seq, DESK.hello_payload())
                          for seq in range(3)], self.sent())
        self.assertEqual(3, link.wait.call_count)

    def test_invalid_hello_still_raises_without_initializing_clock(self):
        link = self.link([b'short'])
        with self.assertRaises(struct.error):
            link.hello()
        self.assertEqual([(DESK.MSG['Hello'], 0, DESK.hello_payload())], self.sent())
        link.wait.assert_called_once_with('Hello', 1.0)

    def test_immediate_event_follows_time_on_the_wire(self):
        link = self.link([DESK.hello_payload()])
        with patch.object(DESK, 'now_us', return_value=1234):
            link.hello()
            event = DESK.ev_batch([(1234, 0, bytes([0xFF]))])
            link.send('Ev', event)
        self.assertEqual([(DESK.MSG['Hello'], 0, DESK.hello_payload()),
                          (DESK.MSG['Time'], 1, struct.pack('<Q', 1234)),
                          (DESK.MSG['Ev'], 2, event)], self.sent())
