#include "codec.h"
#include <Audio.h>
#include <Wire.h>

namespace codec {

namespace {
AudioControlSGTL5000 sgtl;
bool found = false;
// Above ~0.8 the headphone amplifier clips on a full-scale signal.
constexpr float kMaxVolume = 0.8f;
}  // namespace

bool begin() {
  Wire.begin();
  Wire.beginTransmission(0x0A);
  if (Wire.endTransmission() != 0) return false;  // enable() would wait on a missing chip
  found = sgtl.enable();
  if (found) sgtl.volume(0.5f * kMaxVolume);
  return found;
}

bool present() { return found; }

void phones(uint8_t pct) {
  if (!found) return;
  sgtl.volume((pct > 100 ? 100 : pct) * kMaxVolume / 100.f);
}

}  // namespace codec
