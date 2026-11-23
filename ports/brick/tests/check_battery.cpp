#include "brick_battery.h"

#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using brick::ChargeStatus;
using brick::UsbPower;

namespace {
void check(bool ok, const char* label) {
  if (!ok) throw std::runtime_error(label);
}

void run(const fs::path& root) {
  const auto battery = root / "axp2202-battery";
  const auto usb = root / "axp2202-usb";
  fs::create_directory(battery);
  fs::create_directory(usb);
  auto read = [&] { return brick::readBattery(root.string()); };
  auto expect = [&](int capacity, ChargeStatus status, UsbPower power, const char* label) {
    check(read() == brick::BatteryState{capacity, status, power}, label);
  };
  expect(-1, ChargeStatus::Unknown, UsbPower::Unknown, "missing nodes");
  std::ofstream(battery / "present") << "1\n";
  std::ofstream(battery / "status") << "Discharging\n";
  std::ofstream(usb / "online") << "0\n";
  for (const int capacity : {0, 1, 10, 11, 20, 21, 99, 100}) {
    std::ofstream(battery / "capacity") << capacity << '\n';
    expect(capacity, ChargeStatus::Discharging, UsbPower::Offline, "unplugged capacity");
  }

  std::ofstream(usb / "online") << "1\n";
  std::ofstream(battery / "status") << "Full\n";
  for (int i = 0; i < 20; ++i) expect(100, ChargeStatus::Full, UsbPower::Online, "stable plugged/full");
  char details[48];
  read().describe(details, sizeof(details));
  check(std::string(details) == "100%  USB ON  FULL", "full diagnostics");
  std::ofstream(battery / "capacity") << "73\n";
  std::ofstream(battery / "status") << "Charging\n";
  expect(73, ChargeStatus::Charging, UsbPower::Online, "charging without current_now");
  std::ofstream(battery / "status") << "Not charging\n";
  expect(73, ChargeStatus::NotCharging, UsbPower::Online, "connected but idle");
  brick::BatteryCache cache;
  constexpr uint32_t start = UINT32_MAX - 1000;  // exercise clock rollover during a transient error
  const auto good = read();
  check(cache.update(good, start), "first good sample");
  fs::remove(usb / "online");
  check(!cache.update(read(), start + 1000u) && cache.state() == good, "brief USB failure");
  fs::remove(battery / "status");
  check(!cache.update(read(), start + 2999u), "hold through 2999ms");
  check(cache.update(read(), start + 3000u) && cache.state().usb == UsbPower::Unknown &&
        cache.state().status == ChargeStatus::NotCharging, "expire only USB");
  check(cache.update(read(), start + 4000u) && cache.state().status == ChargeStatus::Unknown &&
        cache.state().capacity == 73, "independent status expiry");
  fs::remove(battery / "capacity");
  check(!cache.update(read(), start + 6000u) && cache.state().capacity == 73, "keep recent capacity");
  check(cache.update(read(), start + 7000u) && cache.state().capacity == -1, "expire capacity");
  check(cache.update(good, start + 7001u) && cache.state() == good, "recover immediately");
  std::ofstream(battery / "capacity") << "73\n";
  std::ofstream(battery / "status") << "Not charging\n";
  expect(73, ChargeStatus::NotCharging, UsbPower::Unknown, "missing USB state");
  std::ofstream(usb / "online") << "2\n";
  std::ofstream(battery / "status") << "invalid\n";
  expect(73, ChargeStatus::Unknown, UsbPower::Unknown, "bad USB and status keep percentage");
  for (const char* value : {"", "-1", "101", "999999999999999", "82%", "low"}) {
    std::ofstream(battery / "capacity") << value << '\n';
    expect(-1, ChargeStatus::Unknown, UsbPower::Unknown, "invalid capacity");
  }
  read().describe(details, sizeof(details));
  check(std::string(details) == "--%  USB ?  STATUS ?", "unknown diagnostics");
  fs::remove(battery / "capacity");
  std::ofstream(usb / "online") << "1\n";
  std::ofstream(battery / "status") << "Charging\n";
  expect(-1, ChargeStatus::Charging, UsbPower::Online, "missing capacity preserves power data");
  std::ofstream(battery / "present") << "0\n";
  std::ofstream(battery / "capacity") << "0\n";
  expect(-1, ChargeStatus::Unknown, UsbPower::Online, "absent battery with USB");
}
}  // namespace

int main() {
  char path[] = "/tmp/d-trk-battery-XXXXXX";
  if (!mkdtemp(path)) return 1;
  int result = 0;
  try {
    run(path);
    std::cout << "Brick battery: capacity boundaries, USB, charging, full, unplugged and unknown states OK\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  fs::remove_all(path);
  return result;
}
