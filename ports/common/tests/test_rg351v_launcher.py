import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


LAUNCHER = Path(__file__).resolve().parents[2] / "rg351v/D-TRK.sh"


class RG351VLauncherTest(unittest.TestCase):
    def test_launcher_sets_its_own_ports_mount_and_rg351v_mappings(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch) / "SD card"
            ports = root / "ports"
            app = ports / "d-trk-rg351v"
            app.mkdir(parents=True)
            launcher = ports / LAUNCHER.name
            shutil.copyfile(LAUNCHER, launcher)
            binary = app / "d-trk"
            binary.write_text(
                "#!/usr/bin/env python3\n"
                "import json, os, sys\n"
                "names = ('DTRK_DATA', 'DTRK_DEVICE', 'DTRK_SP_NATIVE_TEXT',"
                " 'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER')\n"
                "print(json.dumps([os.getcwd(), sys.argv[1:], [os.environ[name] for name in names],"
                " os.environ['SDL_GAMECONTROLLERCONFIG']]))\n"
            )
            binary.chmod(0o755)
            names = ("DTRK_DATA", "DTRK_DEVICE", "DTRK_SP_NATIVE_TEXT",
                     "SDL_VIDEODRIVER", "SDL_AUDIODRIVER",
                     "SDL_GAMECONTROLLERCONFIG", "SDL_GAMECONTROLLERCONFIG_FILE")
            env = {key: value for key, value in os.environ.items() if key not in names}
            env["SDL_AUDIODRIVER"] = "dummy"
            subprocess.run(["sh", str(launcher), "--self-test"], cwd=scratch, env=env, check=True)

            data = root / "saves/d-trk-rg351v"
            actual = json.loads((data / "launcher.log").read_text())
            self.assertEqual(
                [str(app.resolve()), ["--self-test"], [str(data.resolve()), "rg351v", "1", "kmsdrm", "dummy"]],
                actual[:3],
            )
            physical = {
                "03000000091200000031000011010000": {
                    "a": "b1", "b": "b0", "x": "b3", "y": "b2",
                    "leftshoulder": "b4", "rightshoulder": "b5",
                    "lefttrigger": "b10", "righttrigger": "b11",
                    "back": "b7", "start": "b6",
                },
                "03002758091200000031000011010000": {
                    "a": "b1", "b": "b0", "x": "b3", "y": "b2",
                    "leftshoulder": "b4", "rightshoulder": "b5",
                    "lefttrigger": "b10", "righttrigger": "b11",
                    "back": "b7", "start": "b6",
                },
            }
            for line in actual[3].splitlines():
                guid, _, *bindings = line.rstrip(",").split(",")
                mapping = dict(binding.split(":", 1) for binding in bindings)
                with self.subTest(guid=guid):
                    expected = {
                        **physical.pop(guid),
                        "dpup": "h0.1", "dpright": "h0.2", "dpdown": "h0.4", "dpleft": "h0.8",
                    }
                    self.assertEqual(expected, {action: mapping[action] for action in expected})
            self.assertFalse(physical)
