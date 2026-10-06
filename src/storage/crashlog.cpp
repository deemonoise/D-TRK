#include "crashlog.h"
#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include "hw/sdcard.h"
#include "sdkconfig.h"
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && __has_include(<esp_core_dump.h>)
#include <esp_core_dump.h>
#define DTRK_COREDUMP 1
#endif

#ifndef DTRK_REV
#define DTRK_REV "dev"
#endif

namespace storage {
namespace {

constexpr const char* kPath = "/projects/crashlog.txt";
constexpr size_t kMaxBytes = 16 * 1024;  // older lines go: the file is started again

bool crashed(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
         r == ESP_RST_BROWNOUT;
}

}  // namespace

const char* firmwareRev() { return DTRK_REV; }

const char* lastResetText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWER ON";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "SLEEP";
    case ESP_RST_EXT: return "RESET PIN";
    default: return "OTHER";
  }
}

void logBoot() {
  const esp_reset_reason_t r = esp_reset_reason();
  if (!crashed(r) || !hw::sdReady()) return;
  fs::FS& fs = hw::sdFs();
  {
    fs::File old = fs.open(kPath, FILE_READ);
    const bool big = old && old.size() > kMaxBytes;
    if (old) old.close();
    if (big) fs.remove(kPath);
  }
  fs::File f = fs.open(kPath, FILE_APPEND);
  if (!f) return;
  char line[160];
  auto put = [&f](const char* t) { f.write(reinterpret_cast<const uint8_t*>(t), strlen(t)); };
  snprintf(line, sizeof(line), "restart: %s, firmware %s\n", lastResetText(), firmwareRev());
  put(line);
#ifdef DTRK_COREDUMP
  if (esp_core_dump_image_check() == ESP_OK) {
    esp_core_dump_summary_t s;
    if (esp_core_dump_get_summary(&s) == ESP_OK) {
      snprintf(line, sizeof(line), "  task %s, pc 0x%08lx, backtrace:", s.exc_task, static_cast<unsigned long>(s.exc_pc));
      put(line);
      for (uint32_t i = 0; i < s.exc_bt_info.depth && i < 16; ++i) {
        snprintf(line, sizeof(line), " 0x%08lx", static_cast<unsigned long>(s.exc_bt_info.bt[i]));
        put(line);
      }
      put(s.exc_bt_info.corrupted ? " (corrupted)\n" : "\n");
    }
    esp_core_dump_image_erase();
  }
#endif
  f.close();
}

}  // namespace storage
