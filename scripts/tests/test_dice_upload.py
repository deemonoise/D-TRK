"""Exercise the real Dice upload handlers with an in-memory card."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class DiceUploadTest(unittest.TestCase):
    def test_upload_validation_atomic_publish_and_failures(self):
        source = (ROOT / "src/net/web.cpp").read_text()
        block = source.split("// ---- Dice library upload ----", 1)[1].split(
            "// ---- End Dice library upload ----", 1)[0]
        fixture = (ROOT / "scripts/tests/dice_upload_host.cpp").read_text()
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            (folder / "test.cpp").write_text(fixture.replace("/* HANDLERS */", block))
            command = ["clang++", "-std=c++17", "-fsanitize=address,undefined",
                       "-ffunction-sections", "-Wl,-dead_strip",
                       "-I" + str(ROOT / "lib/core/src"),
                       str(folder / "test.cpp"), str(ROOT / "lib/core/src/dice_library.cpp"),
                       "-o", str(folder / "test")]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(folder / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
