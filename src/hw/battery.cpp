#include "battery.h"
#include <Arduino.h>
#include <Wire.h>
#include <atomic>

namespace hw {
namespace {

// INA226 answers at 0x40..0x4F by A1/A0 straps; modules ship with them in any state.
constexpr uint8_t kFirstAddr = 0x40, kLastAddr = 0x4F;
constexpr uint8_t kRegConfig = 0x00, kRegShunt = 0x01, kRegBus = 0x02, kRegMfgId = 0xFE;
constexpr uint16_t kMfgId = 0x5449;      // "TI"
// AVG 16, bus and shunt conversion 1.1 ms, continuous shunt + bus: ~35 ms per averaged sample.
constexpr uint16_t kConfig = 0x4527;
constexpr float kShuntLsbV = 2.5e-6f;    // shunt voltage register, V per LSB
constexpr float kBusLsbV = 1.25e-3f;     // bus voltage register, V per LSB
// R100 on the module; after swapping to R010 set 0.01f.
constexpr float kShuntOhm = 0.1f;
constexpr uint32_t kPollMs = 1000;

constexpr uint32_t kFirstReadMs = 50;  // > one averaged conversion (~35 ms) after the config write

// Set once in batteryProbe; atomic because the UI reads it from another core.
std::atomic<bool> present{false};
uint8_t addr = kFirstAddr;  // set by batteryProbe, then read only by the trackio task
// Touched only by the trackio task (batteryPoll).
uint32_t lastPollMs = 0;
bool polled = false;

// Last sample for readers on any core; the trackio task is the only writer.
portMUX_TYPE sampleMux = portMUX_INITIALIZER_UNLOCKED;
BatterySample sample{};
bool valid = false;

bool writeReg(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(value >> 8));
  Wire.write(static_cast<uint8_t>(value & 0xFF));
  return Wire.endTransmission() == 0;
}

// Pointer byte, then 2 bytes big-endian.
bool readReg(uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2)) != 2) return false;
  const uint8_t hi = Wire.read();
  const uint8_t lo = Wire.read();
  value = static_cast<uint16_t>(hi << 8 | lo);
  return true;
}

void publish(const BatterySample& next) {
  portENTER_CRITICAL(&sampleMux);
  sample = next;
  valid = true;
  portEXIT_CRITICAL(&sampleMux);
}

}  // namespace

bool batteryProbe() {
  // The first address whose manufacturer ID reads "TI".
  bool ok = false;
  for (addr = kFirstAddr; addr <= kLastAddr; ++addr) {
    uint16_t id = 0;
    if (readReg(kRegMfgId, id) && id == kMfgId) {
      ok = writeReg(kRegConfig, kConfig);
      break;
    }
  }
  present.store(ok, std::memory_order_relaxed);
  if (ok) Serial.printf("battery: INA226 at 0x%02X\n", addr);
  else Serial.println("battery: INA226 not found");
  return ok;
}

void batteryPoll(uint32_t nowMs) {
  if (!present.load(std::memory_order_relaxed)) return;
  if (!polled) {
    // First call: the first read lands kFirstReadMs later, so it is a full averaged conversion.
    polled = true;
    lastPollMs = nowMs - kPollMs + kFirstReadMs;
    return;
  }
  if (nowMs - lastPollMs < kPollMs) return;
  lastPollMs = nowMs;
  uint16_t shunt, bus;
  if (!readReg(kRegShunt, shunt) || !readReg(kRegBus, bus)) return;  // keep the last sample
  const float amps = static_cast<int16_t>(shunt) * kShuntLsbV / kShuntOhm;
  const float volts = bus * kBusLsbV;
  publish({volts, amps, nowMs});
}

bool batteryPresent() { return present.load(std::memory_order_relaxed); }

bool batteryLast(BatterySample& out) {
  portENTER_CRITICAL(&sampleMux);
  const bool ok = valid;
  if (ok) out = sample;
  portEXIT_CRITICAL(&sampleMux);
  return ok;
}

}  // namespace hw
