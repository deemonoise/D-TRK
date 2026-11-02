"""Exercise production storage with real codecs and a card that rejects chosen operations."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class StorageTransactionsTest(unittest.TestCase):
    def test_storage_failures_and_recovery(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "storage_test"
            sources = ["scripts/tests/storage_transactions_host.cpp",
                       "src/storage/storage.cpp", "src/storage/presets.cpp",
                       "src/storage/project_memory.cpp"]
            # Compile the codecs from source too: sanitizers cover their reads and writes.
            sources += [str(p.relative_to(ROOT)) for p in sorted((ROOT / "lib/core/src").glob("*.cpp"))]
            subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", "-g",
                            "-I" + str(ROOT / "scripts/tests/storage_stubs"),
                            "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/core/src"),
                            *[str(ROOT / s) for s in sources], "-o", str(binary)], check=True)
            for case in ["preset", "resources", "other_files", "queue", "recovery", "matrix", "cleanup", "project_failure", "corrupt", "backup_read"]:
                with self.subTest(case=case):
                    subprocess.run([str(binary), case], check=True)


if __name__ == "__main__":
    unittest.main()
