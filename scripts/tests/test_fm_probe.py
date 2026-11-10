"""The isolated FM probe must preserve the pinned kernel and protect the firmware tree."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

# Mutation workers symlink the unchanged tests; resolving __file__ would import the
# original script and bypass the mutant. All supported commands run from the project root.
ROOT = Path.cwd()
TEMPLATE = ROOT / "tools" / "fm_probe"
SPEC = importlib.util.spec_from_file_location("fm_probe_prepare", TEMPLATE / "prepare.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)
REVISION = "4000b2ec2975f07e76c99aed7d83bb72630720f0"


class FmProbeTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="dtrk-fm-test-")
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name) / "probe"

    def test_prepares_pinned_kernel_at_32k_and_complete_header_closure(self):
        self.assertEqual(self.output.resolve(), probe.prepare(self.output))
        self.assertEqual(f"{REVISION}\nrate=32000\n", (self.output / "SOURCE_REVISION").read_text())
        names = {p.name for p in (self.output / "src").iterdir()}
        self.assertEqual({"main.cpp", "synth_fm.cpp", "synth_fm_tonal.cpp", "synth_fm_percussion.cpp",
                          "synth_fm_machines.cpp", "synth_filter.cpp", "synth_osc.cpp"}, names)
        for source in (self.output / "src").iterdir():
            for header in probe.INCLUDE.findall(source.read_text()):
                self.assertTrue((self.output / "include" / header).is_file(), header)
        for header in (self.output / "include").iterdir():
            for dependency in probe.INCLUDE.findall(header.read_text()):
                self.assertTrue((self.output / "include" / dependency).is_file(), dependency)
        oscillator = (self.output / "include" / "synth_osc.h").read_text()
        self.assertIn("constexpr int kSynthRate = 32000;", oscillator)
        self.assertNotIn("constexpr int kSynthRate = 44100;", oscillator)
        expected = subprocess.check_output(["git", "show", f"{REVISION}:lib/core/src/synth_fm.cpp"], cwd=ROOT)
        self.assertEqual(expected, (self.output / "src" / "synth_fm.cpp").read_bytes())
        self.assertEqual((TEMPLATE / "main.cpp").read_bytes(), (self.output / "src" / "main.cpp").read_bytes())
        self.assertEqual((TEMPLATE / "platformio.ini").read_bytes(), (self.output / "platformio.ini").read_bytes())

    def test_repeated_preparation_preserves_same_source_bytes(self):
        probe.prepare(self.output)
        first = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        probe.prepare(self.output)
        second = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(first, second)

    def test_repository_outputs_rejected_before_source_reads_or_writes(self):
        for target in (ROOT, ROOT / "should-not-create-probe"):
            with self.subTest(target=target), patch.object(probe, "read_source") as read:
                with self.assertRaisesRegex(ValueError, "outside"):
                    probe.prepare(target)
                read.assert_not_called()
        self.assertFalse((ROOT / "should-not-create-probe").exists())

    def test_symlink_into_repository_rejected(self):
        link = Path(self.directory.name) / "repository"
        link.symlink_to(ROOT, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "outside"):
            probe.prepare(link / "should-not-create-probe")
        self.assertFalse((ROOT / "should-not-create-probe").exists())

    def test_missing_revision_creates_no_partial_output(self):
        with patch.object(probe, "SOURCE_REVISION", "0" * 40):
            with self.assertRaises(subprocess.CalledProcessError):
                probe.prepare(self.output)
        self.assertFalse(self.output.exists())

    def test_unexpected_sample_rate_creates_no_partial_output(self):
        read = probe.read_source

        def wrong_rate(repository, name):
            source = read(repository, name)
            return source.replace("constexpr int kSynthRate = 44100;", "constexpr int kSynthRate = 48000;")

        with patch.object(probe, "read_source", side_effect=wrong_rate):
            with self.assertRaisesRegex(RuntimeError, "44.1 kHz"):
                probe.prepare(self.output)
        self.assertFalse(self.output.exists())

    def test_command_line_prints_actual_output_directory(self):
        output = io.StringIO()
        with patch("sys.argv", ["prepare.py", "--output", str(self.output)]), contextlib.redirect_stdout(output):
            probe.main()
        self.assertEqual(str(self.output.resolve()) + "\n", output.getvalue())
        self.assertTrue((self.output / "src" / "main.cpp").is_file())


if __name__ == "__main__":
    unittest.main()
