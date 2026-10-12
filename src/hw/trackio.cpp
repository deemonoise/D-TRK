#include "trackio.h"
#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include "battery.h"
#include "input.h"
#include "pins.h"
#include "track_buttons.h"

namespace hw {
namespace {

// PCF8575 answers at 0x20 + A2A1A0; modules ship with the address pads in any state.
constexpr uint8_t kFirstAddr = 0x20, kLastAddr = 0x27;

uint8_t addr = kFirstAddr;
bool pcfFound = false;
std::atomic<uint16_t> portIn{0xFFFF};  // last read port word, low = pressed

// All pins high: quasi-bidirectional inputs, a button pulls its pin to GND.
bool writePort() {
  Wire.beginTransmission(addr);
  Wire.write(0xFF);
  Wire.write(0xFF);
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
  for (;;) {
    // No INT line: poll every 5 ms (2 bytes at 400 kHz take ~75 us).
    // Debounce: a change counts after two equal reads 5 ms apart.
    uint8_t raw;
    if (pcfFound && readButtons(raw)) {
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
    batteryPoll(millis());  // INA226 shares the bus: read here, once per second
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

bool trackioBegin() {
  Wire.begin(pins::kXSda, pins::kXScl, 400000);
  const bool ina = batteryProbe();
  for (addr = kFirstAddr; addr <= kLastAddr; ++addr)
    if (writePort()) break;
  pcfFound = addr <= kLastAddr;
  if (pcfFound) {
    Serial.printf("trackio: PCF8575 at 0x%02X\n", addr);
    uint8_t pressed;
    readButtons(pressed);  // port state for the safe-boot check before the task runs
  } else {
    Serial.println("trackio: PCF8575 not found");
  }
  // The task owns Wire: runs if either device answered.
  if (pcfFound || ina) xTaskCreatePinnedToCore(task, "trackio", 3072, nullptr, 4, nullptr, 1);
  return pcfFound;
}

bool expanderDown(uint8_t bit) {
  return bit < 16 && !(portIn.load(std::memory_order_relaxed) & (1u << bit));
}

}  // namespace hw
