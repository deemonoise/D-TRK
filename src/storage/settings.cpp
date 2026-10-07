#include "settings.h"
#include <Preferences.h>
#include "model.h"

namespace storage {
namespace {
constexpr const char* kNs = "tracker";
}

uint8_t loadVolume(uint8_t fallback) {
  Preferences p;
  if (!p.begin(kNs, true)) return fallback;  // namespace not created yet
  const uint8_t v = p.getUChar("vol", fallback);
  p.end();
  return v > mt::kMasterVolMax ? fallback : v;
}

void saveVolume(uint8_t v) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putUChar("vol", v);
  p.end();
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

}  // namespace storage
