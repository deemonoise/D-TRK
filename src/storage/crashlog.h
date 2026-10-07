#pragma once
#include "audio/audio.h"

namespace storage {

// Firmware version: the git commit it was built from ("+" = local changes), "dev" without git.
const char* firmwareRev();

// Why the device last restarted: "POWER ON", "SOFTWARE", "PANIC", "WATCHDOG", "BROWNOUT", ...
const char* lastResetText();

// At boot, after the card is mounted: a restart by a crash, watchdog or brownout appends a line to
// /projects/crashlog.txt (reason, firmware, and the crashed task, PC and backtrace from the core
// dump when there is one; the dump is then erased). Readable on the Wi-Fi page. Decode addresses
// with: xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/wt32/firmware.elf <addresses>
void logBoot();

// Appends a CPU profile to /projects/cpuprof.txt: the line head (newline included), then the
// time of every stage. False without a card.
bool appendCpuProfile(const char* head, const audio::Profile& pr);

}  // namespace storage
