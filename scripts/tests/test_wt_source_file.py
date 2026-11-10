"""Verify canonical wavetable payloads before publishing, including a blind cached importer."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class WtSourceFileTest(unittest.TestCase):
    def test_staged_readback_and_rollback(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "wt_source_file"
            sources = [ROOT / "scripts/tests/wt_source_file_host.cpp", ROOT / "src/storage/wt_source_file.cpp"]
            sources.extend(sorted((ROOT / "lib/core/src").glob("*.cpp")))
            subprocess.run(["clang++", "-std=c++17", "-g", "-fsanitize=address,undefined",
                            "-I" + str(ROOT / "scripts/tests/storage_stubs"),
                            "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/core/src"),
                            *map(str, sources), "-o", str(binary)], check=True)
            for case in ["payload", "metadata", "good", "reject", "rename", "rollback", "backup"]:
                with self.subTest(case=case):
                    subprocess.run([str(binary), case], check=True)


if __name__ == "__main__":
    unittest.main()
