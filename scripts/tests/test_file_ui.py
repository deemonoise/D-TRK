"""Drive real FILE dialogs and BANK keyboard through input/touch and inspect rendered labels."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FileUiTest(unittest.TestCase):
    def test_navigation(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            # App and the hardware are the boundary. Dialog, menu, keyboard and ParamList logic is real.
            for path in (ROOT / "src/ui").glob("*.h"):
                shutil.copyfile(path, directory / path.name)
            shutil.copyfile(ROOT / "scripts/tests/file_ui_stubs/app.h", directory / "app.h")
            shutil.copyfile(ROOT / "scripts/tests/file_ui_stubs/wifi_dialog.h", directory / "wifi_dialog.h")
            sources = []
            for name in ["bank_screen", "file_screen", "file_list", "import_dialog", "render_dialog", "bounce_dialog", "param_list", "menu", "keyboard", "theme", "action_dialog"]:
                output = directory / (name + ".cpp")
                shutil.copyfile(ROOT / "src/ui" / (name + ".cpp"), output)
                sources.append(output)
            sources.extend(sorted((ROOT / "lib/core/src").glob("*.cpp")))
            binary = directory / "file_ui"
            subprocess.run(["clang++", "-std=c++17", "-g", "-fsanitize=address,undefined",
                            "-I" + str(directory), "-I" + str(ROOT / "scripts/tests/file_ui_stubs"),
                            "-I" + str(ROOT / "src"), "-I" + str(ROOT / "scripts/tests/storage_stubs"), "-I" + str(ROOT / "lib/core/src"),
                            str(ROOT / "scripts/tests/file_ui_host.cpp"), *map(str, sources), "-o", str(binary)], check=True)
            for case in ["profile_metadata", "project_frame_retry", "project_frame_both_fail", "project_load_budget", "project_load_failure", "project_load_backup", "psram_diagnostics", "rename", "rename_nomemory", "render_footer", "bounce_footer", "import_footer", "system_diagnostics", "wifi_system", "render_stops_transport", "bounce_stops_countin", "action_apply_touch", "action_cancel_touch", "action_encoder"]:
                with self.subTest(case=case):
                    subprocess.run([str(binary), case], check=True)


if __name__ == "__main__":
    unittest.main()
