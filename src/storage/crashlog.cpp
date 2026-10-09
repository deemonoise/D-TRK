#include "crashlog.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include "hw/sdcard.h"
#include "sdkconfig.h"
#include "synth.h"
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && __has_include(<esp_core_dump.h>)
#include <esp_core_dump.h>
#define DTRK_COREDUMP 1
#endif

#ifndef DTRK_REV
#define DTRK_REV "dev"
#endif

namespace storage {
namespace {

// Logs live in their own folder: a restart in the middle of writing one cannot touch /projects.
constexpr const char* kPath = "/diag/crashlog.txt";
constexpr const char* kProfPath = "/diag/cpuprof.txt";
constexpr size_t kMaxBytes = 16 * 1024;  // older lines go: the file is started again

bool crashed(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
         r == ESP_RST_BROWNOUT;
}

// The card is on the synth board, which may not be up at boot: the record waits in RTC memory (kept
// over a restart, not over power-off) until flushBootLog() finds the card.
constexpr uint32_t kPendingMagic = 0x43524C47;  // "CRLG"
constexpr size_t kPendingMax = 480;
RTC_NOINIT_ATTR uint32_t pendingMagic;
RTC_NOINIT_ATTR uint32_t pendingLen;
RTC_NOINIT_ATTR char pending[kPendingMax];

void pendingPut(const char* t) {
  const size_t n = strlen(t);
  if (pendingLen + n > kPendingMax) return;  // a full record keeps its older lines
  memcpy(pending + pendingLen, t, n);
  pendingLen += n;
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
  if (pendingMagic != kPendingMagic || pendingLen > kPendingMax) {
    pendingMagic = kPendingMagic;
    pendingLen = 0;
  }
  const esp_reset_reason_t r = esp_reset_reason();
  if (crashed(r)) {
    char line[160];
    snprintf(line, sizeof(line), "restart: %s, firmware %s\n", lastResetText(), firmwareRev());
    pendingPut(line);
#ifdef DTRK_COREDUMP
    if (esp_core_dump_image_check() == ESP_OK) {
      esp_core_dump_summary_t s;
      if (esp_core_dump_get_summary(&s) == ESP_OK) {
        char bt[160];
        int n = snprintf(bt, sizeof(bt), "  task %s, pc 0x%08lx, backtrace:", s.exc_task,
                         static_cast<unsigned long>(s.exc_pc));
        for (uint32_t i = 0; i < s.exc_bt_info.depth && i < 16 && n < static_cast<int>(sizeof(bt)); ++i)
          n += snprintf(bt + n, sizeof(bt) - n, " 0x%08lx", static_cast<unsigned long>(s.exc_bt_info.bt[i]));
        pendingPut(bt);
        pendingPut(s.exc_bt_info.corrupted ? " (corrupted)\n" : "\n");
      }
      esp_core_dump_image_erase();
    }
#endif
  }
  flushBootLog();
}

void flushBootLog() {
  if (pendingLen == 0 || !hw::sdReady()) return;
  fs::FS& fs = hw::sdFs();
  // Older firmware kept the logs in /projects: move them over once.
  static const char* const kOld[2][2] = {{"/projects/crashlog.txt", kPath}, {"/projects/cpuprof.txt", kProfPath}};
  for (const auto& o : kOld)
    if (fs.exists(o[0]) && !fs.exists(o[1])) fs.rename(o[0], o[1]);
  {
    fs::File old = fs.open(kPath, FILE_READ);
    const bool big = old && old.size() > kMaxBytes;
    if (old) old.close();
    if (big) fs.remove(kPath);
  }
  fs::File f = fs.open(kPath, FILE_APPEND);
  if (!f) return;
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(pending), pendingLen) == pendingLen;
  f.close();
  if (ok) pendingLen = 0;
}

bool appendCpuProfile(const char* head, const audio::Profile& pr) {
  if (!hw::sdReady()) return false;
  fs::File f = hw::sdFs().open(kProfPath, FILE_APPEND);
  if (!f) return false;
  constexpr float kBlockUs = audio::kBlock * 1e6f / audio::kRate;
  f.print(head);
  char line[64];
  for (int i = 0; i < audio::kProfStages; ++i) {
    snprintf(line, sizeof(line), "  %-8s %7.1f us  %5.1f %%\n", mt::Synth::profName(i), pr.us[i], pr.us[i] * 100 / kBlockUs);
    f.print(line);
  }
  f.close();
  return true;
}

}  // namespace storage
