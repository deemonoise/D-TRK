#pragma once

// MT_HOT: audio hot path, placed in internal RAM on the ESP32 / ITCM on the Teensy (no flash-cache misses).
// MT_DATA: a constant table read on the hot path, in internal RAM (const data sits in flash).
// MT_INLINE: forced inlining for per-sample helpers.
// MT_RAM2: a large buffer in the Teensy's RAM2 (OCRAM, DMAMEM: cached, not zeroed at boot), off RAM1.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define MT_HOT IRAM_ATTR
#define MT_DATA DRAM_ATTR
#elif defined(__IMXRT1062__)
#include <avr/pgmspace.h>  // FASTRUN (not Arduino.h: its F() macro clashes with core names)
#define MT_HOT FASTRUN
#define MT_DATA
#define MT_RAM2 DMAMEM
#else
#define MT_HOT
#define MT_DATA
#endif
#ifndef MT_RAM2
#define MT_RAM2
#endif
#define MT_INLINE inline __attribute__((always_inline))
