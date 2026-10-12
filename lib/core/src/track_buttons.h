#pragma once
#include <stdint.h>

namespace mt {

constexpr int kTrackButtons = 8;  // physical track buttons

// PCF8575 pin maps: entry = port bit (0-7 = P00-P07, 8-15 = P10-P17), kNoPin = not wired.
constexpr uint8_t kNoPin = 0xFF;

// Bit = button down (pin pulled to GND).
inline uint8_t trackPressed(uint16_t port, const uint8_t btnBit[8]) {
  uint8_t m = 0;
  for (int i = 0; i < 8; ++i)
    if (btnBit[i] < 16 && !(port & (1u << btnBit[i]))) m |= static_cast<uint8_t>(1u << i);
  return m;
}

}  // namespace mt
