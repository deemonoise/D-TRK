#pragma once

namespace mt {

// 1..16 chars of [A-Za-z0-9_-]: names that load back under themselves.
bool projectBaseValid(const char* base);

// Wi-Fi firmware page. Firmware image name: ends with .bin.
bool webFirmwareName(const char* name);
// Synth board firmware (Intel HEX): ends with .hex.
bool webSynthFirmwareName(const char* name);

}  // namespace mt
