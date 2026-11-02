"""Guard the verification entry point against skipping checks or uploading firmware."""
import pathlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from scripts import verify


class VerifyTest(unittest.TestCase):
    def test_default_runs_every_check_without_uploading(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            archive = root / ".pio/build/native/lib123/libcore.a"
            archive.parent.mkdir(parents=True)
            archive.touch()
            with patch.object(verify, "ROOT", root), patch.object(verify.sys, "argv", ["verify.py"]), \
                    patch.object(verify, "run") as run:
                verify.main()
            calls = run.call_args_list
            self.assertEqual(len(calls), 4)
            self.assertEqual(calls[0].args[1:], ("-m", "platformio", "test", "-e", "native"))
            self.assertEqual(calls[1].args[1:], ("-m", "unittest", "discover", "-s", "scripts/tests", "-v"))
            self.assertEqual(calls[1].kwargs["env"]["DTRK_NATIVE_CORE"], str(archive))
            self.assertEqual(calls[2].args[1:], ("-m", "unittest", "discover", "-s", "test", "-p", "test_*.py", "-v"))
            self.assertEqual(calls[3].args[1:], ("-m", "platformio", "run", "-e", "wt32", "-e", "teensy41"))
            for call in calls:
                self.assertEqual(call.args[0], verify.sys.executable)
                self.assertNotIn("upload", call.args)

    def test_builds_only_does_not_require_native_archive(self):
        with patch.object(verify.sys, "argv", ["verify.py", "--builds-only"]), \
                patch.object(verify, "run") as run:
            verify.main()
        run.assert_called_once_with(verify.sys.executable, "-m", "platformio", "run", "-e", "wt32", "-e", "teensy41")

    def test_tests_only_and_invalid_archive_stop_before_builds(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            with patch.object(verify, "ROOT", root), \
                    patch.object(verify.sys, "argv", ["verify.py", "--tests-only"]), \
                    patch.object(verify, "run") as run:
                with self.assertRaises(RuntimeError):
                    verify.main()
                self.assertEqual(run.call_count, 1)
                for name in ["a", "b"]:
                    archive = root / f".pio/build/native/{name}/libcore.a"
                    archive.parent.mkdir(parents=True)
                    archive.touch()
                with self.assertRaises(RuntimeError):
                    verify.main()
                (root / ".pio/build/native/b/libcore.a").unlink()
                run.reset_mock()
                verify.main()
                self.assertEqual(run.call_count, 3)

    def test_failed_check_aborts_pipeline(self):
        with patch.object(verify.sys, "argv", ["verify.py"]), \
                patch.object(verify, "run", side_effect=subprocess.CalledProcessError(2, "check")) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                verify.main()
        self.assertEqual(run.call_count, 1)

    def test_commands_use_project_root_and_propagate_failures(self):
        env = {"TEST_ONLY": "1"}
        with patch.object(verify.subprocess, "run") as run, patch("builtins.print") as output:
            verify.run("command", "argument with spaces", env=env)
        output.assert_called_once_with("+ command 'argument with spaces'", flush=True)
        run.assert_called_once_with(("command", "argument with spaces"), cwd=verify.ROOT, env=env, check=True)
        with patch.object(verify.subprocess, "run", side_effect=subprocess.CalledProcessError(3, "command")):
            with self.assertRaises(subprocess.CalledProcessError):
                verify.run("command")
