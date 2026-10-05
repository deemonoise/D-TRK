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

}  // namespace storage
