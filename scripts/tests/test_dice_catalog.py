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

    def test_generated_menu_has_ten_families_and_legacy_style_references(self):
        compiled = catalog.compile_styles(self.data)
        menu = compiled.split("static const Group kMenuFamilies[] = {\n", 1)[1]
        names = ("Acid", "Techno", "Drum & Bass", "Trance", "Hardcore & Gabber",
                 "IDM", "Ambient", "Industrial", "UK Garage", "Rave")
        groups = [json.loads("[" + line.strip()[1:-2] + "]") for line in menu.splitlines() if line.startswith('  {"')]
        self.assertEqual(list(names), [name for name, _, _ in groups])
        next_first = 0
        for (name, first, count), expected in zip(groups, (4, 6, 6, 6, 4, 4, 4, 3, 5, 4)):
            self.assertEqual(next_first, first)
            self.assertEqual(expected, count, name)
            next_first += count
        self.assertEqual(46, next_first)
        choices = compiled.split("static const DiceChoice kMenuChoices[] = {\n", 1)[1].split("};", 1)[0]
        self.assertEqual(46, len(set(choices.splitlines())))
        acid_group = next(i for i, group in enumerate(self.data["groups"]) if group["id"] == "acid")
        self.assertTrue(choices.startswith(f"  {{{acid_group},1}},\n"))

    def test_menu_rejects_unknown_or_duplicate_choices(self):
        for styles in (("Missing",), ("Known", "Known")):
            with self.subTest(styles=styles), patch.object(catalog, "MENU_FAMILIES", (("Family", styles),)):
                with self.assertRaisesRegex(ValueError, f"Invalid Dice menu style: {styles[-1]}"):
                    catalog.compile_menu({"Known": (4, 2)})

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

    def test_production_row_preserves_endpoints_and_rejects_invalid_fields(self):
        base = ["Example", "character", "source", "Warm", "1", "-2"]
        def read(cells):
            return catalog.production_row("| " + " | ".join(cells) + " |")
        self.assertEqual(("Example", "{DiceVoicing::Warm,1,-2}"), read(base))
        self.assertEqual(("Example", "{DiceVoicing::Warm,8,2}"), read(base[:4] + ["8", "+2"]))
        for index, value in [(0, ""), (1, ""), (2, ""), (3, "bad name"), (4, "0"),
                             (4, "9"), (5, "-3"), (5, "3"), (5, "blue")]:
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                read(base[:index] + [value] + base[index + 1:])

    def test_production_table_skips_prose_and_rejects_duplicate_style(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "table.md"
            preamble = "Example\n| Стиль | Описание |\n|---|---|\n"
            row = "| Example | character | source | Warm | 3 | 0 |\n"
            path.write_text(preamble + row)
            self.assertEqual({"Example": "{DiceVoicing::Warm,3,0}"}, catalog.production_directions(path))
            path.write_text(preamble + 2 * row)
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                catalog.production_directions(path)

    def test_missing_style_direction_is_rejected(self):
        with patch.object(catalog, "production_directions", return_value={}):
            with self.assertRaisesRegex(ValueError, "do not match"):
                catalog.compile_styles(self.data)

    def test_cli_detects_stale_table_and_rebuilds_it(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            (root / "data").mkdir()
            (root / "data/dice-styles.md").write_bytes((catalog.ROOT / "data/dice-styles.md").read_bytes())
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
