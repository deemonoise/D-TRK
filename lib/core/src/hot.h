#pragma once

// MT_HOT: audio hot path, placed in internal RAM on the ESP32 (no flash-cache misses).
// MT_INLINE: forced inlining for per-sample helpers.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define MT_HOT IRAM_ATTR
#else
#define MT_HOT
#endif
#define MT_INLINE inline __attribute__((always_inline))
