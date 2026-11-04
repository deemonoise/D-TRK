"""A benchmark succeeds only when the board confirms a completed measurement."""
import importlib.util
from pathlib import Path
import queue
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).absolute().parents[2]
SPEC = importlib.util.spec_from_file_location("stream_bench_desk", ROOT / "scripts/link_desk.py")
DESK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DESK)

DONE = ("sbench: done, 0 underruns in 1 s, min reserve 42 ms, "
        "cpu avg 12.0 % peak 18.0 %, cap min 32")


class Inbox(queue.Queue):
    def get(self, *_, **__):
        return super().get(block=False)


class Board:
    def __init__(self, lines, errors=0):
        self.frames = Inbox()
        self.lines = lines
        self.errors = errors
        self.sent = []

    def send(self, name, payload=b""):
        self.sent.append((name, payload))
        if name == "Debug" and payload[0] == 2:
            for line in self.lines:
                if isinstance(line, tuple):
                    self.frames.put(line)
                    continue
                raw = line.encode()
                self.frames.put((DESK.MSG["Log"], 0, bytes([len(raw)]) + raw))

    def wait(self, *_):
        return None


class StreamBenchTests(unittest.TestCase):
    def run_bench(self, lines, errors=0, clock=None, **options):
        board = Board(lines, errors)
        self.last_board = board
        args = SimpleNamespace(sd=True, setup=False, heavy=False, wt=False, fx=False,
                               semis=0, secs=1, fault_at=None)
        args.__dict__.update(options)
        with patch.object(DESK, "cmd_hello"), \
                patch.object(DESK.time, "monotonic", side_effect=clock or range(100, 1100)):
            DESK.cmd_bench_stream(board, args)
        return board

    def test_no_card_is_failure(self):
        with self.assertRaisesRegex(SystemExit, "no card"):
            self.run_bench(["sbench: no card"])

    def test_failed_sample_preparation_is_failure_even_with_later_summary(self):
        for reason in ["only 13 store files", "registered 13 of 16 samples",
                       "stream data mismatch"]:
            with self.assertRaisesRegex(SystemExit, reason):
                self.run_bench(["sbench: error: " + reason, DONE])

    def test_no_measurement_summary_is_failure(self):
        with self.assertRaisesRegex(SystemExit, "summary"):
            self.run_bench([])

    def test_underruns_are_failure(self):
        with self.assertRaisesRegex(SystemExit, "underrun"):
            self.run_bench([DONE.replace("0 underruns", "7 underruns")])

    def test_short_measurement_is_failure(self):
        with self.assertRaisesRegex(SystemExit, "duration"):
            self.run_bench([DONE.replace("in 1 s", "in 0 s")])

    def test_corrupt_link_is_failure(self):
        with self.assertRaisesRegex(SystemExit, "CRC"):
            self.run_bench([DONE], errors=1)

    def test_completed_measurement_succeeds_and_stops(self):
        board = self.run_bench(["sbench: 16 samples, 100..100000 frames, SD only", DONE])
        self.assertEqual(("Debug", DESK.debug_payload(3)), board.sent[-1])

    def test_waits_for_psram_before_measurement(self):
        self.run_bench(["sbench: filling PSRAM"] * 12 + [DONE])

    def test_setup_requires_board_confirmation(self):
        board = self.run_bench(["sbench: 16 samples, 100..100000 frames, SD only"], setup=True)
        self.assertEqual(("Debug", DESK.debug_payload(2, 0, 3, 1)), board.sent[-1])
        with self.assertRaisesRegex(SystemExit, "no card"):
            self.run_bench(["sbench: no card"], setup=True)

    def test_requested_fault_is_sent(self):
        board = self.run_bench(["progress"] * 3 + [DONE], fault_at=0)
        self.assertEqual(1, board.sent.count(("Debug", DESK.debug_payload(1))))

    def test_fault_waits_for_requested_time(self):
        board = self.run_bench([DONE], fault_at=3)
        self.assertNotIn(("Debug", DESK.debug_payload(1)), board.sent)

    def test_bench_flags_reach_board(self):
        for options, flags in [({"sd": False}, 0),
                               ({"sd": False, "heavy": True, "fx": True}, 20),
                               ({"heavy": True, "wt": True, "fx": True}, 29)]:
            board = self.run_bench([DONE], **options)
            self.assertEqual(("Debug", DESK.debug_payload(2, 0, flags, 1)), board.sent[0])

    def test_setup_ignores_unrelated_or_incomplete_logs(self):
        for line in ["unrelated samples, 16", "sbench: waiting"]:
            with self.assertRaisesRegex(SystemExit, "summary"):
                self.run_bench([line], setup=True)

    def test_summary_metrics_and_declared_log_length(self):
        expected = dict(underruns=0, seconds=1, min_reserve_ms=42, cpu_avg=12.0,
                        cpu_peak=18.0, cap_min=32)
        self.assertEqual(expected, DESK.parse_stream_bench(DONE))
        raw = DONE.encode()
        self.assertEqual((True, expected), DESK.stream_bench_log(
            bytes([len(raw)]) + raw + b" ignored extension", False))

    def test_status_throttle_preserves_five_second_boundary(self):
        status = (struct.pack("<BHHBH", 12, 0, 0, 16, 1000) + bytes(16)
                  + struct.pack("<HBH", 0, 0, 0) + struct.pack("<BHHBB", 16, 0, 42, 0, 100)
                  + bytes(18) + struct.pack("<I", 10000))
        frame = (DESK.MSG["Status"], 0, status)
        with patch.object(DESK, "print_status") as show:
            self.run_bench([frame, frame, DONE], clock=[100, 101, 102, 103, 107, 108, 109])
        self.assertEqual(2, show.call_count)

    def test_status_waits_until_five_seconds_since_last_print(self):
        status = (struct.pack("<BHHBH", 12, 0, 0, 16, 1000) + bytes(16)
                  + struct.pack("<HBH", 0, 0, 0) + struct.pack("<BHHBB", 16, 0, 42, 0, 100)
                  + bytes(18) + struct.pack("<I", 10000))
        frame = (DESK.MSG["Status"], 0, status)
        with patch.object(DESK, "print_status") as show:
            self.run_bench([frame, frame, (DESK.MSG["Hello"], 0, b""), DONE],
                           clock=[100, 101, 102, 103, 106, 107, 108, 109, 110])
        self.assertEqual(1, show.call_count)
        with patch.object(DESK, "print_status") as show:
            self.run_bench([frame, DONE], clock=[0, 1, 5, 6, 7])
        self.assertEqual(1, show.call_count)

    def test_fault_injection_at_boundary(self):
        board = self.run_bench([DONE], fault_at=0)
        self.assertIn(("Debug", DESK.debug_payload(1)), board.sent)

    def test_completion_after_deadline_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, "summary"):
            self.run_bench([DONE], clock=[100, 126, 127])

    def test_interrupt_is_failure_and_stops_board(self):
        with patch.object(DESK, "wait_stream_bench", side_effect=KeyboardInterrupt):
            with self.assertRaisesRegex(SystemExit, "interrupted"):
                self.run_bench([])
        self.assertEqual(("Debug", DESK.debug_payload(3)), self.last_board.sent[-1])

    def test_machine_report_is_flushed_and_stable(self):
        with patch("builtins.print") as output:
            self.run_bench([DONE])
        output.assert_called_once_with(
            'BENCH_RESULT {"cap_min": 32, "cpu_avg": 12.0, "cpu_peak": 18.0, '
            '"min_reserve_ms": 42, "seconds": 1, "underruns": 0}', flush=True)

    def test_rx_keeps_log_available_to_bench(self):
        link = DESK.Link.__new__(DESK.Link)
        link.frames, link.errors, link.alive = queue.Queue(), 0, True
        raw = DONE.encode()
        payload = bytes([len(raw)]) + raw
        hello = DESK.hello_payload()
        packet = (DESK.encode_frame(DESK.MSG["Log"], 7, payload)
                  + DESK.encode_frame(DESK.MSG["Hello"], 8, hello) + b"\x02\x01\x00")

        class Serial:
            in_waiting = len(packet)

            def read(self, _):
                link.alive = False
                return packet

        link.ser = Serial()
        with patch("builtins.print") as output:
            link._rx()
        output.assert_called_once_with("[synth]", DONE, flush=True)
        self.assertEqual((DESK.MSG["Log"], 7, payload), link.frames.get_nowait())
        self.assertEqual((DESK.MSG["Hello"], 8, hello), link.frames.get_nowait())
        self.assertEqual(1, link.errors)

    def test_log_printing_respects_declared_length(self):
        link = DESK.Link.__new__(DESK.Link)
        link.frames, link.errors = queue.Queue(), 0
        frame = (DESK.MSG["Log"], 7, b"\x03abc trailing bytes")
        with patch("builtins.print") as output:
            link._receive_frame(frame)
        output.assert_called_once_with("[synth]", "abc", flush=True)
        self.assertEqual(frame, link.frames.get_nowait())


if __name__ == "__main__":
    unittest.main()
