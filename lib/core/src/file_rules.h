#pragma once

#include <stdint.h>

namespace mt {

// 1..16 chars of [A-Za-z0-9_-]: names that load back under themselves.
bool projectBaseValid(const char* base);

// Wi-Fi firmware page. Firmware image name: ends with .bin.
bool webFirmwareName(const char* name);
// Synth board firmware (Intel HEX): ends with .hex.
bool webSynthFirmwareName(const char* name);

// Wi-Fi export download: a path under /export/ (a folder or file below it), no "..", "." or empty
// segments, no backslashes.
bool webExportPath(const char* path);
// HTTP Range "bytes=N-" (or "bytes=N-M": served from N to the end): N. Other forms: false.
bool parseRangeStart(const char* range, uint32_t& at);

}  // namespace mt
