"""The actual asynchronous SD query: slow peer, caching, restarts and task failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SpaceQueryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        folder = Path(cls.tmp.name)
        (folder / "freertos").mkdir()
        (folder / "freertos/task.h").write_text('''#pragma once
#include "freertos/FreeRTOS.h"
inline void (*taskFn)(void*) = nullptr;
inline int tasks = 0, deletions = 0;
inline bool taskFails = false;
inline int xTaskCreatePinnedToCore(void (*fn)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) {
  if (taskFails) return pdFALSE;
  taskFn = fn; ++tasks; return pdTRUE;
}
inline void vTaskDelete(void*) { ++deletions; }
''')
        cls.binary = folder / "test"
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I" + str(folder),
                        "-I" + str(ROOT / "scripts/tests/link_stubs"),
                        "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/core/src"),
                        str(ROOT / "scripts/tests/space_query_host.cpp"),
                        str(ROOT / "src/storage/space_query.cpp"),
                        str(ROOT / "lib/core/src/link_fs.cpp"),
                        str(ROOT / "lib/core/src/link_msg.cpp"), "-o", str(cls.binary)], check=True)

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_slow_query_is_nonblocking_and_reused(self):
        self.run_case("cache")

    def test_peer_restart_invalidates_results(self):
        self.run_case("boot")

    def test_allocation_and_link_failures_can_retry(self):
        self.run_case("fail")

    def test_card_error_and_malformed_reply_are_not_free_space(self):
        self.run_case("card")

    def test_cache_clock_wrap(self):
        self.run_case("wrap")


if __name__ == "__main__":
    unittest.main()
