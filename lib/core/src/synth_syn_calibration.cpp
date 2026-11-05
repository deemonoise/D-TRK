#include "synth_syn_calibration.h"
#include "fastmath.h"
#include <math.h>

namespace mt {
namespace {
float raw127(float x) { return x < 0.f ? 0.f : (x > 127.f ? 127.f : x); }
uint16_t milliseconds(float x) { return static_cast<uint16_t>(x < 1.f ? 1.f : floorf(x + .5f)); }
}
MT_CONTROL float synLpOctaves(float raw) {
  // fc = 25.63868 * exp(.053546996 * raw), held-out cutoff errors <= .31%.
  return .35832215f + raw127(raw) * .07725199f;
}
MT_CONTROL float synLpQ(float raw) {
  const float q = .506f * fastExp2(raw127(raw) * .051905126f);  // .506 * 10^(raw/64)
  return q < .714f ? .714f : q;
}
MT_CONTROL float synLpGain(float raw) {
  const float r = raw127(raw);
  float db;
  if (r <= 32.f) db = -1.21415f * r * (1.f / 32.f);
  else if (r <= 64.f) db = -1.21415f - 2.91114f * (r - 32.f) * (1.f / 32.f);
  else if (r <= 96.f) db = -4.12529f - 3.73139f * (r - 64.f) * (1.f / 32.f);
  else db = -7.85668f - 1.52469f * (r - 96.f) * (1.f / 16.f);
  return fastExp2(db * .166096405f);
}
MT_CONTROL uint16_t synAttackMs(uint8_t raw) {
  if (!raw) return 0;
  return milliseconds(.3415f * fastExp2((raw > 127 ? 127 : raw) * .125f));
}
MT_CONTROL uint16_t synDecayTauMs(uint8_t raw) {
  if (!raw) return 0;
  const float r = raw > 127 ? 127 : raw;
  // Time constants fitted to the sine envelope, not to transient peak levels.
  float ms;
  if (r <= 16.f) ms = 21.72f * r * (1.f / 16.f);
  else if (r <= 32.f) ms = 21.72f + 54.18f * (r - 16.f) * (1.f / 16.f);
  else if (r <= 48.f) ms = 75.9f + 98.7f * (r - 32.f) * (1.f / 16.f);
  else ms = 174.6f * fastExp2((r - 48.f) * (1.f / 16.f));
  return milliseconds(ms);
}
}
