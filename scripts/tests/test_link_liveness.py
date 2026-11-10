"""Run the production ESP link with a slow SD peer and deterministic task scheduling."""

import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class LinkLivenessTest(unittest.TestCase):
    def test_slow_filesystem_reply_does_not_disconnect_or_restart_peer(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "link_liveness"
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined", "-g", '-DDTRK_REV="test"',
                            "-I" + str(ROOT / "scripts/tests/link_stubs"),
                            "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/core/src"),
                            str(ROOT / "scripts/tests/link_liveness_host.cpp"),
                            str(ROOT / "lib/core/src/link_frame.cpp"),
                            str(ROOT / "lib/core/src/link_msg.cpp"),
                            str(ROOT / "lib/core/src/link_fs.cpp"),
                            str(ROOT / "src/storage/remote_fs.cpp"),
                            str(ROOT / "src/hw/sdcard.cpp"),
                            "-o", str(binary)], check=True)
            for case in ["concurrent", "reply", "progress", "noise", "timeout", "slow_file",
                         "long_timeout", "long_progress", "wrap",
                         "remote_open", "remote_write", "remote_read", "remote_close"]:
                with self.subTest(case=case):
                    subprocess.run([str(binary), case], check=True, cwd=ROOT,
                                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})


if __name__ == "__main__":
    unittest.main()
