#pragma once
#include <stdint.h>
namespace storage {
enum class WtSourceSaveResult : uint8_t { Ok, WriteFail, ReadbackFail, ImportFail, RecoveryNeeded };
using WtSourceImport = bool (*)(const char* path, void* ctx);
// UI task. Stage a canonical WAV, verify it, rotate the previous file, then import.
// Any failed write/import restores the old file; failed rollback retains .bak for recovery.
WtSourceSaveResult saveWtSourceFile(const char* path, const int16_t* source,
                                    WtSourceImport import, void* ctx);
}
