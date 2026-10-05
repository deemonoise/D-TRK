#pragma once
#include <stdint.h>

namespace mt {

// Track button LEDs, bit = on. A flashing track lights up; the selected track stays lit and
// goes dark while it flashes. Muted tracks never flash (the sequencer does not play them).
inline uint8_t trackLedMask(int selected, uint8_t flash) {
  const uint8_t sel = selected >= 0 && selected < 8 ? static_cast<uint8_t>(1u << selected) : 0;
  return static_cast<uint8_t>((flash & ~sel) | (sel & ~flash));
}

// PCF8575 pin maps: entry = port bit (0-7 = P00-P07, 8-15 = P10-P17), kNoPin = not wired.
constexpr uint8_t kNoPin = 0xFF;

// Word to write: lit LEDs sink (low), everything else high. Quasi-bidirectional pins must be
// high to read buttons, so pins without a lit LED stay 1.
inline uint16_t trackPortWord(uint8_t leds, const uint8_t ledBit[8]) {
  uint16_t w = 0xFFFF;
  for (int i = 0; i < 8; ++i)
    if ((leds & (1u << i)) && ledBit[i] < 16) w &= static_cast<uint16_t>(~(1u << ledBit[i]));
  return w;
}

// Bit = button down (pin pulled to GND).
inline uint8_t trackPressed(uint16_t port, const uint8_t btnBit[8]) {
  uint8_t m = 0;
  for (int i = 0; i < 8; ++i)
    if (btnBit[i] < 16 && !(port & (1u << btnBit[i]))) m |= static_cast<uint8_t>(1u << i);
  return m;
}

}  // namespace mt
