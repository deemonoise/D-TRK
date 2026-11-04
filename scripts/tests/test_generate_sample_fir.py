import importlib.util
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).absolute().parents[1] / 'generate_sample_fir.py'
SPEC = importlib.util.spec_from_file_location('sample_fir_generator', SOURCE)
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


class SampleFirGenerationTests(unittest.TestCase):
    def test_every_phase_and_band_preserves_dc(self):
        for band in range(GEN.BANDS):
            for phase in range(GEN.PHASES + 1):
                coefficients = GEN.coefficients(band, phase)
                self.assertEqual(GEN.tap_count(band), len(coefficients))
                self.assertTrue(all(math.isfinite(value) for value in coefficients))
                self.assertAlmostEqual(1, sum(coefficients), places=13)

    def test_integer_phase_advance_is_one_source_frame(self):
        for band in (0, 8, 16, 24, 32):
            first = GEN.coefficients(band, 0)
            last = GEN.coefficients(band, GEN.PHASES)
            self.assertAlmostEqual(0, last[0], places=15)
            self.assertAlmostEqual(0, first[-1], places=15)
            for a, b in zip(first[:-1], last[1:]):
                self.assertAlmostEqual(a, b, places=14)

    def test_octave_filter_keeps_low_tone_and_rejects_alias_source(self):
        coefficients = GEN.coefficients(8, 0)

        def amplitude(hz):
            real = sum(value * math.cos(2 * math.pi * hz * (tap + GEN.FIRST) / 44100)
                       for tap, value in enumerate(coefficients))
            imag = sum(value * math.sin(2 * math.pi * hz * (tap + GEN.FIRST) / 44100)
                       for tap, value in enumerate(coefficients))
            return math.hypot(real, imag)

        self.assertLess(abs(20 * math.log10(amplitude(1000))), .3)
        self.assertLess(20 * math.log10(amplitude(15000) / amplitude(1000)), -35)

    def test_generated_source_is_deterministic_and_matches_checked_in_bank(self):
        first = GEN.render_table()
        self.assertEqual(first, GEN.render_table())
        checked_in = SOURCE.parents[1] / 'lib/core/src/synth_resample_table.inc'
        self.assertEqual(first, checked_in.read_text())
        self.assertEqual(GEN.BANDS * (GEN.PHASES + 1), first.count('    {'))

    def test_main_writes_only_the_requested_repository_coefficient_bank(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'scripts').mkdir()
            output = root / 'lib/core/src/synth_resample_table.inc'
            output.parent.mkdir(parents=True)
            with patch.object(GEN, '__file__', str(root / 'scripts/generate_sample_fir.py')):
                GEN.main()
            self.assertEqual(GEN.render_table(), output.read_text())
            self.assertEqual([output], [p for p in root.rglob('*') if p.is_file()])

    def test_adaptive_kernel_is_even_compact_and_matches_last_polyphase_bank(self):
        self.assertEqual(0, GEN.blackman(1))
        self.assertEqual(0, GEN.blackman(-1))
        self.assertAlmostEqual(1, GEN.kernel_value(0), places=15)
        for distance in (0, .25, 1, 2.5, 5.9, 6, 7):
            self.assertEqual(GEN.kernel_value(distance), GEN.kernel_value(-distance))
        self.assertEqual(0, GEN.kernel_value(6))
        self.assertEqual(0, GEN.kernel_value(7))
        phase = 13
        taps = GEN.tap_count(32)
        kernel = [GEN.kernel_value((j + 1 - taps // 2 - phase / GEN.PHASES) / 16)
                  for j in range(taps)]
        total = sum(kernel)
        for a, b in zip(GEN.coefficients(32, phase), kernel):
            self.assertAlmostEqual(a, b / total, places=14)

    def test_band_support_grows_with_rate_and_stays_even(self):
        previous = 0
        for band in range(GEN.BANDS):
            taps = GEN.tap_count(band)
            self.assertGreaterEqual(taps, previous)
            self.assertEqual(0, taps % 2)
            self.assertGreaterEqual(taps, 9 * 2 ** (band / 8))
            previous = taps
        self.assertEqual(20, GEN.tap_count(8))
        self.assertEqual(36, GEN.tap_count(16))
        self.assertEqual(72, GEN.tap_count(24))
        self.assertEqual(144, GEN.tap_count(32))

    def test_q15_banks_have_exact_dc_and_safe_signed_dot_bound(self):
        for band in range(GEN.BANDS):
            for phase in range(GEN.PHASES + 1):
                coefficients = GEN.quantized_coefficients(band, phase)
                self.assertEqual(32768, sum(coefficients))
                self.assertLess(sum(abs(value) for value in coefficients), 65536)
                self.assertTrue(all(-32768 <= value <= 32767 for value in coefficients))

    def test_q15_phase_wrap_matches_one_frame_shift_exactly(self):
        for band in range(GEN.BANDS):
            first = GEN.quantized_coefficients(band, 0)
            last = GEN.quantized_coefficients(band, GEN.PHASES)
            self.assertEqual(0, last[0])
            self.assertEqual(0, first[-1])
            self.assertEqual(first[:-1], last[1:])

    def test_high_half_kernels_have_exact_dc_symmetry_and_safe_dot_bound(self):
        for key in range(257, 513):
            packed = GEN.high_kernel(key)
            full = list(reversed(packed[1:-1])) + packed
            self.assertEqual(0, packed[-1])
            self.assertLessEqual(len(full) + 1, 2049)
            self.assertEqual(32768, sum(full))
            self.assertLess(sum(abs(value) for value in full), 65536)
            self.assertTrue(all(-32768 <= value <= 32767 for value in full))
            radius = len(packed) - 1
            for distance in range(1, radius):
                self.assertEqual(full[radius - 1 - distance], full[radius - 1 + distance])

    def test_high_half_kernel_frequency_response(self):
        for key in range(257, 513):
            packed = GEN.high_kernel(key)
            radius, rate = len(packed) - 1, 2 ** (key / 64)
            for frequency, stop in ((.675 / rate, True), (.05 / rate, False)):
                real = packed[0] + 2 * sum(value * math.cos(2 * math.pi * frequency * distance)
                                          for distance, value in enumerate(packed[1:-1], 1))
                gain = 20 * math.log10(max(abs(real) / 32768, 1e-12))
                if stop:
                    self.assertLess(gain, -35)
                else:
                    self.assertLess(abs(gain), .3)

    def test_coefficient_banks_are_signed16_in_explicit_teensy_flash(self):
        source = GEN.render_table()
        self.assertIn('#define MT_SAMPLE_COEFS PROGMEM', source)
        self.assertEqual(GEN.BANDS, source.count('static const int16_t kSampleFirBand'))
        self.assertEqual(256, source.count('static const int16_t kSampleFirHigh'))
        self.assertNotIn('const float', source)


if __name__ == '__main__':
    unittest.main()
