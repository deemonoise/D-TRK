#pragma once

// MT_HOT: audio hot path, placed in internal RAM on the ESP32 (no flash-cache misses).
// MT_DATA: a constant table read on the hot path, in internal RAM (const data sits in flash).
// MT_INLINE: forced inlining for per-sample helpers.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define MT_HOT IRAM_ATTR
#define MT_DATA DRAM_ATTR
#else
#define MT_HOT
#define MT_DATA
#endif
#define MT_INLINE inline __attribute__((always_inline))
