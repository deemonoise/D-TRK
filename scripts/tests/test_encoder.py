"""Run the production PCNT setup/decoder against encoder traces (ASan/UBSan)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EncoderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        folder = Path(cls.tmp.name)
        source = (ROOT / "src/hw/input.cpp").read_text()
        # Keep the real setup and read functions together; only the hardware API is replaced.
        decoder = source.split("void setupEncoder()", 1)[1].split("void task(void*)", 1)[0]
        fixture = (ROOT / "scripts/tests/encoder_host.cpp").read_text()
        (folder / "test.cpp").write_text(fixture.replace("/* DECODER */", "void setupEncoder()" + decoder))
        cls.binary = folder / "test"
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", str(folder / "test.cpp"),
                        "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_trace(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_aligned_clicks(self):
        self.run_trace("aligned")

    def test_reversal_after_counter_phase_error(self):
        self.run_trace("offset")

    def test_offset_does_not_emit_between_detents(self):
        self.run_trace("offset_edges")

    def test_partial_turn_and_contact_bounce(self):
        self.run_trace("bounce")

    def test_fast_turns_between_polls(self):
        self.run_trace("batch")

    def test_edge_during_counter_read(self):
        self.run_trace("read_edge")

    def test_overflow_does_not_drop_edges(self):
        self.run_trace("overflow")

    def test_signed_counter_wrap(self):
        self.run_trace("signed_wrap")


if __name__ == "__main__":
    unittest.main()
