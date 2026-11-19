"""The firmware table must remain a reproducible copy of the checked-in facts."""
import gzip
import copy
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

from scripts import generate_dice_catalog as catalog


class DiceCatalogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(gzip.decompress((catalog.ROOT / "data/dice_guide.json.gz").read_bytes()))

    def test_generated_firmware_matches_all_source_facts(self):
        expected = (catalog.ROOT / "lib/core/src/dice_catalog.inc").read_text()
        self.assertEqual(expected, catalog.compile_catalog(self.data))

    def test_literals_escape_names_without_changing_numbers(self):
        self.assertEqual('{"quoted \\"name\\"",-12,255}', catalog.row(['quoted "name"', -12, 255]))
        self.assertEqual('const int x[] = {\n  1,2,\n};\n', catalog.table('const int x', ['1,2']))

    def test_event_indices_fit_uint16_at_the_exact_boundary(self):
        sequences = [[[i // 256, i % 256]] for i in range(65536)]
        self.assertIn("65535", catalog.compile_events({"sequences": sequences}))
        sequences.append([[256, 0]])
        with self.assertRaises(AssertionError):
            catalog.compile_events({"sequences": sequences})

    def test_timbre_indices_share_identical_blueprints_and_fit_uint16(self):
        data = {"voices":[{"name":str(i)} for i in range(65536)]}
        data["voices"].append(data["voices"][42])
        with patch.object(catalog.dice_timbres, "build", side_effect=lambda voice: voice), patch.object(catalog, "timbre_row", side_effect=lambda voice: voice["name"]):
            table, refs = catalog.compile_timbres(data)
            self.assertEqual((65535,42), (refs[65535],refs[-1]))
            self.assertEqual(1,table.count("  42,"))
            data["voices"].append({"name":"65536"})
            with self.assertRaises(AssertionError):
                catalog.compile_timbres(data)

    def test_incomplete_variant_catalogue_is_rejected(self):
        data = copy.deepcopy(self.data)
        data["styles"][0]["variants"].pop()
        with self.assertRaises(AssertionError):
            catalog.compile_styles(data)

    def test_cli_detects_stale_table_and_rebuilds_it(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            (root / "data").mkdir()
            (root / "lib/core/src").mkdir(parents=True)
            (root / "data/dice_guide.json.gz").write_bytes(gzip.compress(json.dumps(self.data).encode()))
            target = root / "lib/core/src/dice_catalog.inc"
            target.write_text("stale")
            with patch.object(catalog, "ROOT", root), patch.object(sys, "argv", ["catalog", "--check"]):
                with self.assertRaisesRegex(SystemExit, "stale"):
                    catalog.main()
            with patch.object(catalog, "ROOT", root), patch.object(sys, "argv", ["catalog"]):
                catalog.main()
            with patch.object(catalog, "ROOT", root), patch.object(sys, "argv", ["catalog", "--check"]):
                catalog.main()
            self.assertEqual(catalog.compile_catalog(self.data), target.read_text())


if __name__ == "__main__":
    unittest.main()
