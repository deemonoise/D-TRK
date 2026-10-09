#pragma once
#include <stdint.h>

namespace storage {

// Device settings in NVS (survive power-off, independent of projects).
// Any small device setting by NVS key (up to 15 characters); fallback when nothing is stored yet.
uint8_t loadSetting(const char* key, uint8_t fallback);
void saveSetting(const char* key, uint8_t v);

// Colour theme index (ui::themeAt); fallback when nothing is stored yet.
uint8_t loadTheme(uint8_t fallback);
void saveTheme(uint8_t v);

}  // namespace storage
