#pragma once
#include "audio/audio.h"

namespace storage {

// Firmware version: the git commit it was built from ("+" = local changes), "dev" without git.
const char* firmwareRev();

// Why the device last restarted: "POWER ON", "SOFTWARE", "PANIC", "WATCHDOG", "BROWNOUT", ...
const char* lastResetText();

// At boot: a restart by a crash, watchdog or brownout appends a line to /diag/crashlog.txt on the
// synth board's card (reason, firmware, and the crashed task, PC and backtrace from the core dump
// when there is one; the dump is then erased). Without the card yet the record waits for
// flushBootLog(). Decode addresses with:
// xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/wt32/firmware.elf <addresses>
void logBoot();
// Writes a waiting record once the card is there. Cheap when there is none; UI task.
void flushBootLog();

// Appends a CPU profile to /diag/cpuprof.txt: the line head (newline included), then the
// time of every stage. False without a card.
bool appendCpuProfile(const char* head, const audio::Profile& pr);

}  // namespace storage
