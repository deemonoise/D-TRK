"""Opening/polling the production REC window must not wait for a cold SD query."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    depth = 0
    for end in range(source.index("{", start), len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise AssertionError("unterminated function: " + signature)


class RecOpenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        folder = Path(cls.tmp.name)
        source = (ROOT / "src/ui/rec_dialog.cpp").read_text()
        methods = "\n".join(function(source, name) for name in (
            "bool RecDialog::open(", "bool RecDialog::openSd()", "void RecDialog::poll()",
            "void RecDialog::closeWindow()", "mt::RecBtn RecDialog::btnState() const",
            "void RecDialog::toggle()"))
        fixture = (ROOT / "scripts/tests/rec_open_host.cpp").read_text().replace("/* METHODS */", methods)
        (folder / "test.cpp").write_text(fixture)
        cls.binary = folder / "test"
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-ffunction-sections",
                        "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                        "-I" + str(ROOT / "lib/core/src"),
                        str(folder / "test.cpp"), str(ROOT / "lib/core/src/rec_ui.cpp"),
                        "-o", str(cls.binary)], check=True)

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_first_open_and_polls_do_not_wait(self):
        self.run_case("cold")

    def test_close_and_reopen_during_preparation(self):
        self.run_case("reopen")

    def test_completion_uses_latest_settings(self):
        self.run_case("ready")

    def test_failed_space_query_leaves_limit_unknown(self):
        self.run_case("failed")

    def test_background_take_does_not_prepare_again(self):
        self.run_case("taking")

    def test_sample_open_while_old_sd_query_finishes(self):
        self.run_case("sample")


if __name__ == "__main__":
    unittest.main()
