#include "codec.h"
#include <Audio.h>
#include <Wire.h>
#include "rec.h"

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
  if (found) {
    sgtl.volume(0.5f * kMaxVolume);
    sgtl.adcHighPassFilterEnable();
  }
  return found;
}

bool present() { return found; }

void phones(uint8_t pct) {
  if (!found) return;
  sgtl.volume((pct > 100 ? 100 : pct) * kMaxVolume / 100.f);
}

void input(Input in) {
  if (!found) return;
  sgtl.inputSelect(in == Input::Mic ? AUDIO_INPUT_MIC : AUDIO_INPUT_LINEIN);
  sgtl.adcHighPassFilterEnable();
}

void micGain(int db) {
  if (found) sgtl.micGain(static_cast<unsigned>(mt::recGainClamp(mt::RecSource::Mic, db)));
}

void lineLevel(int lvl) {
  if (found) sgtl.lineInLevel(static_cast<uint8_t>(mt::recGainClamp(mt::RecSource::Line, lvl)));
}

}  // namespace codec
