#include "brick_battery.h"
#include "rg351v/port_profile.h"

#include <charconv>
#include <cstdio>
#include <fstream>

namespace brick {
namespace {
std::string readLine(const std::string& path) {
  std::ifstream file(path);
  std::string line;
  std::getline(file, line);
  return line;
}

int readPercent(const std::string& path) {
  const auto text = readLine(path);
  int value;
  const auto end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (text.empty() || result.ec != std::errc{} || result.ptr != end || value < 0 || value > 100) return -1;
  return value;
}
}  // namespace

BatteryState readBattery(const std::string& root) {
  BatteryState out;
  const bool arkos = rg351v::selected();
  const auto usb = readLine(root + (arkos ? "/usb/online" : "/axp2202-usb/online"));
  // ArkOS exposes USB and mains as separate power supplies.
  const auto ac = arkos ? readLine(root + "/ac/online") : "0";
  if (usb == "1" || ac == "1") out.usb = UsbPower::Online;
  if (usb == "0" && ac == "0") out.usb = UsbPower::Offline;

  const auto battery = root + (arkos ? "/battery/" : "/axp2202-battery/");
  if (readLine(battery + "present") == "0") return out;
  out.capacity = readPercent(battery + "capacity");
  const auto status = readLine(battery + "status");
  if (status == "Full") out.status = ChargeStatus::Full;
  if (status == "Charging") out.status = ChargeStatus::Charging;
  if (status == "Discharging") out.status = ChargeStatus::Discharging;
  if (status == "Not charging") out.status = ChargeStatus::NotCharging;
  return out;
}

bool BatteryCache::update(const BatteryState& sample, uint32_t now) {
  const BatteryState before = state_;
  const auto accept = [now](auto& current, auto reading, auto unknown, uint32_t& lastValid) {
    if (reading != unknown) {
      current = reading;
      lastValid = now;
    } else if (now - lastValid >= 3000) {
      current = unknown;
    }
  };
  accept(state_.capacity, sample.capacity, -1, capacityAt_);
  accept(state_.status, sample.status, ChargeStatus::Unknown, statusAt_);
  accept(state_.usb, sample.usb, UsbPower::Unknown, usbAt_);
  return !(before == state_);
}

void BatteryState::percentText(char* text, size_t size) const {
  if (capacity < 0) std::snprintf(text, size, "--%%");
  else std::snprintf(text, size, "%d%%", capacity);
}

void BatteryState::describe(char* text, size_t size) const {
  const char* charge = "STATUS ?";
  switch (status) {
    case ChargeStatus::Charging: charge = "CHARGING"; break;
    case ChargeStatus::Full: charge = "FULL"; break;
    case ChargeStatus::Discharging: charge = "DISCHARGING"; break;
    case ChargeStatus::NotCharging: charge = "NOT CHARGING"; break;
    case ChargeStatus::Unknown: break;
  }
  char percent[8];
  percentText(percent, sizeof(percent));
  const char* power = usb == UsbPower::Online ? "ON" : usb == UsbPower::Offline ? "OFF" : "?";
  std::snprintf(text, size, "%s  USB %s  %s", percent, power, charge);
}
}  // namespace brick
