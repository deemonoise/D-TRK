#include "settings.h"
#include <Preferences.h>
#include "midi_devices.h"

namespace storage {
namespace {
constexpr const char* kNs = "tracker";
constexpr const char* kMidiNs = "midi";
constexpr const char* kPadsKey = "pads";  // old: one PadLearn, before "devs"
constexpr const char* kDevsKey = "devs";
constexpr const char* kClockKey = "clock";
constexpr int kModes = static_cast<int>(mt::PadMode::Count);
constexpr size_t kPadsLen = kModes * 2;  // ch[] then base[], written byte by byte (layout-independent)
}

uint8_t loadSetting(const char* key, uint8_t fallback) {
  Preferences p;
  if (!p.begin(kNs, true)) return fallback;
  const uint8_t v = p.getUChar(key, fallback);
  p.end();
  return v;
}

void saveSetting(const char* key, uint8_t v) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putUChar(key, v);
  p.end();
}

uint8_t loadTheme(uint8_t fallback) {
  Preferences p;
  if (!p.begin(kNs, true)) return fallback;
  const uint8_t v = p.getUChar("theme", fallback);
  p.end();
  return v;
}

void saveTheme(uint8_t v) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putUChar("theme", v);
  p.end();
}

namespace {

// The old pad learn (8 bytes, ch[4] then base[4]); a mode with a channel over 15 stays cleared.
void readOldPads(Preferences& p, mt::PadLearn& out) {
  out.clear();
  uint8_t b[kPadsLen];
  const size_t n =
      p.isKey(kPadsKey) && p.getBytesLength(kPadsKey) == kPadsLen ? p.getBytes(kPadsKey, b, kPadsLen) : 0;
  if (n != kPadsLen) return;
  for (int m = 0; m < kModes; ++m) {
    if (b[m] > 15) continue;
    out.ch[m] = b[m];
    out.base[m] = b[kModes + m];
  }
}

constexpr const char* const kOldKeys[] = {"in", "in_t", "out", "out_t", kPadsKey};

void removeOld(Preferences& p) {
  for (const char* k : kOldKeys)
    if (p.isKey(k)) p.remove(k);
}

// The old two-slot keys into an empty list; true when any of them was there (then erased).
bool migrateOld(Preferences& p, mt::MidiDevices& out) {
  bool any = false;
  for (const char* k : kOldKeys) any |= p.isKey(k);  // isKey: no missing-key log
  if (!any) return false;
  char in[mt::kDevAddr] = {}, outA[mt::kDevAddr] = {};
  if (p.isKey("in")) p.getString("in", in, sizeof(in));
  if (p.isKey("out")) p.getString("out", outA, sizeof(outA));
  const uint8_t inT = p.isKey("in_t") ? p.getUChar("in_t", 0) : 0;
  const uint8_t outT = p.isKey("out_t") ? p.getUChar("out_t", 0) : 0;
  mt::PadLearn pads;
  readOldPads(p, pads);
  out.clear();
  out.migrate(in, inT, outA, outT, pads);
  return true;
}

}  // namespace

bool loadDevices(mt::MidiDevices& out) {
  out.clear();
  Preferences p;
  // Read-write: a migration writes "devs" and erases the old keys. Creates the namespace on the first
  // boot (no read-only-open log for a never-written one).
  if (!p.begin(kMidiNs, false)) return false;
  bool ok = false;
  if (p.isKey(kDevsKey)) {
    uint8_t b[mt::MidiDevices::kBlobMax];
    const size_t len = p.getBytesLength(kDevsKey);
    const size_t n = len > 0 && len <= sizeof(b) ? p.getBytes(kDevsKey, b, len) : 0;
    ok = n > 0 && out.load(b, static_cast<int>(n));
    if (!ok) out.clear();
  }
  // A bad "devs" next to old keys (an interrupted migration): the old keys win.
  if (!ok && migrateOld(p, out)) {
    uint8_t b[mt::MidiDevices::kBlobMax];
    const int n = out.save(b, sizeof(b));
    // The old keys go only once "devs" is written: a failed write retries on the next boot.
    if (n > 0 && p.putBytes(kDevsKey, b, static_cast<size_t>(n)) == static_cast<size_t>(n)) removeOld(p);
    ok = true;
  } else if (ok) {
    removeOld(p);  // a good "devs" whose old keys were left behind
  }
  p.end();
  return ok;
}

void saveDevices(const mt::MidiDevices& d) {
  uint8_t b[mt::MidiDevices::kBlobMax];
  const int n = d.save(b, sizeof(b));
  if (n <= 0) return;
  Preferences p;
  if (!p.begin(kMidiNs, false)) return;
  p.putBytes(kDevsKey, b, static_cast<size_t>(n));
  p.end();
}

bool loadBleClock() {
  Preferences p;
  if (!p.begin(kMidiNs, true)) return true;
  const bool on = !p.isKey(kClockKey) || p.getUChar(kClockKey, 1) != 0;  // isKey: no missing-key log
  p.end();
  return on;
}

void saveBleClock(bool on) {
  Preferences p;
  if (!p.begin(kMidiNs, false)) return;
  p.putUChar(kClockKey, on ? 1 : 0);
  p.end();
}

}  // namespace storage
