"""Exercise local sample jobs using real codecs, sample bank, and failing card operations."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class SampleToolsJobsTest(unittest.TestCase):
    def test_local_jobs(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "sample_tools"
            sources = [ROOT / "scripts/tests/sample_tools_host.cpp", ROOT / "src/audio/sample_tools.cpp"]
            sources.extend(sorted((ROOT / "lib/core/src").glob("*.cpp")))
            subprocess.run(["clang++", "-std=c++17", "-g", "-fsanitize=address,undefined",
                            "-I" + str(ROOT / "scripts/tests/sample_tools_stubs"),
                            "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/core/src"),
                            *map(str, sources), "-o", str(binary)], check=True)
            for case in ["exact", "normalize", "stretch", "write", "rename", "cache", "cancel", "collision", "cancel_import", "cancel_publish", "identity", "bad_source", "tempo_cancel", "corrupt", "tempo_positive", "tempo_short"]:
                with self.subTest(case=case):
                    subprocess.run([str(binary), case], check=True)


if __name__ == "__main__":
    unittest.main()
