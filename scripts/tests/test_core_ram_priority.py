"""Exercise the production core RAM owner with bounded heaps and failed audio parks."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = (
    "boot", "fallback", "ps_alloc", "park_ps", "int_alloc", "park_int",
    "cycles", "seq_fail", "unreserved", "synth_fail",
)


class CoreRamPriorityTest(unittest.TestCase):
    def test_core_storage_ownership_and_failure_recovery(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = pathlib.Path(directory)
            # Quote-includes must resolve to the boundary stubs, not src/audio or
            # src/engine. Compile unmodified production files from this isolated copy.
            for name in ("core_ram.cpp", "core_ram.h"):
                shutil.copyfile(ROOT / "src" / name, temporary / name)
            binary = temporary / "core_ram_priority"
            subprocess.run([
                "clang++", "-std=c++17", "-g", "-Wall", "-Wextra",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I" + str(ROOT / "scripts/tests/core_ram_priority_stubs"),
                "-I" + str(temporary), "-I" + str(ROOT / "lib/core/src"),
                str(temporary / "core_ram.cpp"),
                str(ROOT / "scripts/tests/core_ram_priority_host.cpp"),
                "-o", str(binary),
            ], check=True)
            # Each process starts with fresh production static ownership state.
            for case in CASES:
                with self.subTest(case=case):
                    subprocess.run([str(binary), case], check=True)


if __name__ == "__main__":
    unittest.main()
