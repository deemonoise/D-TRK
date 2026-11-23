#include <FS.h>

#include <fstream>

#include "brick_battery.h"
#include <charconv>
#include <cstdio>
#include "link/link.h"
#include "link/synth_fw.h"
#include "offline_render.h"
#include "storage/crashlog.h"
#include "synth.h"
namespace slink {
// Local filesystem and renderer do not require an external synth handshake.
bool synthUp() { return true; }
const char* synthFw() { return audio::synthFw(); }
uint32_t bootId() { return 1; }
bool request(mt::link::Msg t, const uint8_t* p, int n, mt::link::Msg replyType, uint8_t* reply, int& len,
             uint32_t) {
  if (t != replyType) return false;
  len = brick::renderRequest(t, p, n, reply);
  return len > 0;
}
bool requestLong(mt::link::Msg t, const uint8_t* p, int n, uint8_t* r, int& len, ProgressFn, void*,
                 uint32_t) {
  return request(t, p, n, t, r, len);
}
}  // namespace slink
namespace synthfw {
Result update(const char*, uint8_t& reason, uint32_t& bytes, ProgressFn, void*) {
  reason = uint8_t(mt::link::FwResult::BadImage);
  bytes = 0;
  return Result::Rejected;
}
const char* rejectText(uint8_t) { return "USE KNULLI PORTS"; }
bool rebooted(uint32_t) { return false; }
}  // namespace synthfw
namespace storage {
const char* firmwareRev() { return "D-TRK Brick"; }
const char* lastResetText() { return "APP START"; }
bool appendCpuProfile(const char* head, const audio::Profile& profile) {
  std::ofstream out(fs::resolve("/diag/cpuprof.txt"), std::ios::app);
  out << head << "Peak " << profile.peakUs << " us\n";
  for (int i = 0; i < audio::kProfStages; ++i)
    out << mt::Synth::profName(i) << " " << profile.us[i] << " us\n";
  return bool(out);
}
}  // namespace storage
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
  const auto online = readLine(root + "/axp2202-usb/online");
  if (online == "1") out.usb = UsbPower::Online;
  if (online == "0") out.usb = UsbPower::Offline;

  const auto battery = root + "/axp2202-battery/";
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
