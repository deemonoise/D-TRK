"""A malformed frame must not kill or grow the USB receive loop indefinitely."""
import importlib.util
from pathlib import Path
import queue
import struct
import unittest
from unittest.mock import patch


ROOT = Path(__file__).absolute().parents[2]
SPEC = importlib.util.spec_from_file_location("desk_rx_check", ROOT / "scripts/link_desk.py")
DESK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DESK)


class UsbReceiveTests(unittest.TestCase):
    def receive(self, chunks):
        link = DESK.Link.__new__(DESK.Link)
        link.frames, link.errors, link.alive = queue.Queue(), 0, True

        class Serial:
            @property
            def in_waiting(self):
                return len(chunks[0])

            def read(self, count):
                self_test.assertEqual(len(chunks[0]) or 1, count)
                data = chunks.pop(0)
                if not chunks:
                    link.alive = False
                return data

        self_test = self
        link.ser = Serial()
        try:
            link._rx()
        except Exception as exc:
            self.fail("receive loop terminated: %r" % exc)
        return link

    def test_malformed_log_does_not_drop_following_hello(self):
        hello = DESK.hello_payload()
        for bad in [b"", b"\x03ab"]:
            with self.subTest(payload=bad), patch("builtins.print") as output:
                link = self.receive([DESK.encode_frame(DESK.MSG["Log"], 1, bad)
                                     + DESK.encode_frame(DESK.MSG["Hello"], 2, hello)])
                output.assert_not_called()
                self.assertEqual(1, link.errors)
                self.assertEqual((DESK.MSG["Hello"], 2, hello), link.frames.get_nowait())
                self.assertTrue(link.frames.empty())

    def test_crc_valid_payload_over_firmware_limit_is_rejected(self):
        body = bytes([DESK.MSG["Status"], 1]) + bytes(DESK.MAX_PAYLOAD + 1)
        packet = DESK.cobs_encode(body + struct.pack("<H", DESK.crc16(body)))
        self.assertIsNone(DESK.decode_frame(packet))

    def test_maximum_payload_roundtrips_across_usb_chunks(self):
        data = bytes(range(256)) * 2
        packet = DESK.encode_frame(DESK.MSG["Status"], 255, data)
        link = self.receive([packet[:1], packet[1:253], packet[253:-1], packet[-1:]])
        self.assertEqual(0, link.errors)
        self.assertEqual((DESK.MSG["Status"], 255, data), link.frames.get_nowait())

    def test_empty_payload_is_a_valid_minimum_frame(self):
        packet = DESK.encode_frame(DESK.MSG["Time"], 0, b"")
        self.assertEqual((DESK.MSG["Time"], 0, b""), DESK.decode_frame(packet[:-1]))

    def test_bad_cobs_and_crc_are_rejected(self):
        self.assertIsNone(DESK.decode_frame(b"\x03\x01"))
        packet = bytearray(DESK.encode_frame(DESK.MSG["Time"], 0, b""))
        packet[-2] ^= 1
        self.assertIsNone(DESK.decode_frame(packet[:-1]))

    def test_empty_log_is_valid_and_empty_delimiters_are_ignored(self):
        with patch("builtins.print") as output:
            link = self.receive([b"\0\0" + DESK.encode_frame(DESK.MSG["Log"], 1, b"\0")
                                 + b"\0\0"])
        output.assert_called_once_with("[synth]", "", flush=True)
        self.assertEqual(0, link.errors)
        self.assertEqual((DESK.MSG["Log"], 1, b"\0"), link.frames.get_nowait())

    def test_overlong_frame_is_bounded_and_resyncs_on_delimiter(self):
        # Same wire bound as core/link_frame.h, including its one-byte delimiter allowance.
        wire_bound = DESK.MAX_PAYLOAD + 4 + (DESK.MAX_PAYLOAD + 4) // 254 + 2
        hello = DESK.hello_payload()
        with patch.object(DESK, "decode_frame", wraps=DESK.decode_frame) as decode:
            link = self.receive([b"\x01" * (wire_bound + 1), b"\x00",
                                 DESK.encode_frame(DESK.MSG["Hello"], 3, hello)])
        self.assertTrue(all(len(call.args[0]) <= wire_bound for call in decode.call_args_list))
        self.assertEqual(1, decode.call_count)  # overflow is rejected before COBS/CRC work
        self.assertEqual(1, link.errors)
        self.assertEqual((DESK.MSG["Hello"], 3, hello), link.frames.get_nowait())
        self.assertTrue(link.frames.empty())

    def test_corruption_after_valid_frame_remains_an_error(self):
        hello = DESK.hello_payload()
        link = self.receive([DESK.encode_frame(DESK.MSG["Hello"], 4, hello)
                             + b"\x02\x01\x00"
                             + DESK.encode_frame(DESK.MSG["Hello"], 5, hello)])
        self.assertEqual(1, link.errors)
        self.assertEqual((DESK.MSG["Hello"], 4, hello), link.frames.get_nowait())
        self.assertEqual((DESK.MSG["Hello"], 5, hello), link.frames.get_nowait())

    def test_initial_corruption_is_not_silently_discarded(self):
        hello = DESK.hello_payload()
        link = self.receive([b"\x02\x01\x00" + DESK.encode_frame(DESK.MSG["Hello"], 6, hello)])
        self.assertEqual(1, link.errors)
        self.assertEqual((DESK.MSG["Hello"], 6, hello), link.frames.get_nowait())

    def test_empty_serial_read_keeps_waiting_for_a_frame(self):
        hello = DESK.hello_payload()
        link = self.receive([b"", DESK.encode_frame(DESK.MSG["Hello"], 7, hello)])
        self.assertEqual(0, link.errors)
        self.assertEqual((DESK.MSG["Hello"], 7, hello), link.frames.get_nowait())

    def test_serial_error_is_reported_while_live(self):
        link = DESK.Link.__new__(DESK.Link)
        link.alive = True

        class Serial:
            in_waiting = 0

            def read(self, count):
                raise OSError("unplugged")

        link.ser = Serial()
        with self.assertRaisesRegex(OSError, "unplugged"):
            link._rx()

    def test_serial_close_during_read_exits_cleanly(self):
        link = DESK.Link.__new__(DESK.Link)
        link.alive = True

        class Serial:
            in_waiting = 0

            def read(self, count):
                link.alive = False
                raise OSError("closed")

        link.ser = Serial()
        link._rx()


if __name__ == "__main__":
    unittest.main()
