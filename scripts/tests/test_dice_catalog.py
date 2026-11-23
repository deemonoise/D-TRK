"""Check the Dice catalogue generator without local source data."""
from contextlib import redirect_stderr
import io
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

from scripts import generate_dice_catalog as catalog


class DiceCatalogTest(unittest.TestCase):
    def test_literals_escape_names_without_changing_numbers(self):
        self.assertEqual('{"quoted \\"name\\"",-12,255}', catalog.row(['quoted "name"', -12, 255]))
        self.assertEqual('const int x[] = {\n  1,2,\n};\n', catalog.table('const int x', ['1,2']))

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

    def test_cli_names_missing_local_inputs(self):
        errors = io.StringIO()
        with tempfile.TemporaryDirectory() as folder, patch.object(catalog, "ROOT", pathlib.Path(folder)), \
                patch.object(sys, "argv", ["catalog", "--check"]), redirect_stderr(errors):
            with self.assertRaises(SystemExit) as failure:
                catalog.main()
        self.assertEqual(2, failure.exception.code)
        self.assertIn("missing local Dice catalogue inputs", errors.getvalue())
        self.assertIn(str(catalog.CATALOG_SOURCE), errors.getvalue())
        self.assertIn(str(catalog.STYLE_SOURCE), errors.getvalue())


if __name__ == "__main__":
    unittest.main()
