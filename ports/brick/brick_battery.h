#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace brick {
enum class ChargeStatus { Unknown, Charging, Full, Discharging, NotCharging };
enum class UsbPower { Unknown, Offline, Online };

struct BatteryState {
  int capacity = -1;
  ChargeStatus status = ChargeStatus::Unknown;
  UsbPower usb = UsbPower::Unknown;

  bool operator==(const BatteryState& b) const {
    return capacity == b.capacity && status == b.status && usb == b.usb;
  }
  void percentText(char* text, size_t size) const;
  void describe(char* text, size_t size) const;
};

class BatteryCache {
 public:
  bool update(const BatteryState& sample, uint32_t now);
  const BatteryState& state() const { return state_; }

 private:
  BatteryState state_;
  uint32_t capacityAt_ = 0, statusAt_ = 0, usbAt_ = 0;
};

BatteryState readBattery(const std::string& root = "/sys/class/power_supply");
}  // namespace brick
