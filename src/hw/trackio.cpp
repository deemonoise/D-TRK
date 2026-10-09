#include "trackio.h"
#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include "input.h"
#include "pins.h"
#include "track_leds.h"

namespace hw {
namespace {

// PCF8575 answers at 0x20 + A2A1A0; modules ship with the address pads in any state.
constexpr uint8_t kFirstAddr = 0x20, kLastAddr = 0x27;

uint8_t addr = kFirstAddr;
std::atomic<uint8_t> ledWant{0};
std::atomic<uint16_t> portIn{0xFFFF};  // last read port word, low = pressed

bool writePort(uint8_t leds) {
  const uint16_t w = mt::trackPortWord(leds, pins::kTrackLedBit);
  Wire.beginTransmission(addr);
  Wire.write(static_cast<uint8_t>(w));
  Wire.write(static_cast<uint8_t>(w >> 8));
  return Wire.endTransmission() == 0;
}

bool readButtons(uint8_t& pressed) {
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2)) != 2) return false;
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  const uint16_t port = static_cast<uint16_t>(lo | hi << 8);
  portIn.store(port, std::memory_order_relaxed);
  pressed = mt::trackPressed(port, pins::kTrackBtnBit);
  return true;
}

void task(void*) {
  uint8_t state = 0, last = 0;  // debounced / previous raw sample
  uint8_t ledsOut = 0;
  for (;;) {
    // No INT line: poll every 5 ms (2 bytes at 400 kHz take ~75 us).
    // Debounce: a change counts after two equal reads 5 ms apart.
    uint8_t raw;
    if (readButtons(raw)) {
      if (raw == last && raw != state) {
        const uint8_t down = raw & ~state, up = state & ~raw;
        for (int i = 0; i < 8; ++i) {
          if (down & (1u << i)) inputPush(InputType::TrackPress, static_cast<int8_t>(i));
          if (up & (1u << i)) inputPush(InputType::TrackRelease, static_cast<int8_t>(i));
        }
        state = raw;
      }
      last = raw;
    }
    const uint8_t want = ledWant.load(std::memory_order_relaxed);
    if (want != ledsOut && writePort(want)) ledsOut = want;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

bool trackioBegin() {
  Wire.begin(pins::kXSda, pins::kXScl, 400000);
  for (addr = kFirstAddr; addr <= kLastAddr; ++addr)
    if (writePort(0)) break;
  if (addr > kLastAddr) {
    Serial.println("trackio: PCF8575 not found");
    return false;
  }
  Serial.printf("trackio: PCF8575 at 0x%02X\n", addr);
  uint8_t pressed;
  readButtons(pressed);  // port state for the safe-boot check before the task runs
  xTaskCreatePinnedToCore(task, "trackio", 3072, nullptr, 4, nullptr, 1);
  return true;
}

bool expanderDown(uint8_t bit) {
  return bit < 16 && !(portIn.load(std::memory_order_relaxed) & (1u << bit));
}

void trackLeds(uint8_t mask) { ledWant.store(mask, std::memory_order_relaxed); }

}  // namespace hw
