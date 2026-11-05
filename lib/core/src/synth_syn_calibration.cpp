#include "synth_syn_calibration.h"
#include "fastmath.h"
#include "synth_osc.h"
#include <math.h>

namespace mt {
namespace {
float raw127(float x) { return x < 0.f ? 0.f : (x > 127.f ? 127.f : x); }
uint16_t milliseconds(float x) { return static_cast<uint16_t>(x < 1.f ? 1.f : floorf(x + .5f)); }
}
float synHyperSpread(float normalized) {
  const float x = normalized < 0.f ? 0.f : (normalized > 1.f ? 1.f : normalized);
  // Raw32..127 holdouts follow this exponential; zero is the measured unison setting.
  return x == 0.f ? 0.f : 137.5f * fastExp2((x - 1.f) * 6.66f);
}
uint16_t synModAttackMs(uint8_t raw) {
  return static_cast<uint16_t>(2u * synDecayTauMs(raw));
}
float synModEnv(uint32_t samples, uint8_t attack, uint8_t decay) {
  const float a = synModAttackMs(attack) * (kSynthRate / 1000.f);
  if (samples < a) return samples * fastRcp(a);
  const float tau = synDecayTauMs(decay) * (kSynthRate / 1000.f);
  if (tau <= 0.f) return 1.f;  // existing explicit HOLD at raw0
  return fastExp(-(samples - a) * fastRcp(tau));
}
float synFenvOctaves(float amount) {
  const float a = amount < 0.f ? -amount : amount;
  const float x = a > 64.f ? 64.f : a;
  // Measured raw CC0..96 offsets: 0..16 / 16..48 / 48..88 cutoff units.
  const float units = x <= 16.f ? x : x <= 32.f ? 2.f * x - 16.f
                                            : x <= 48.f ? 2.5f * x - 32.f : 3.f * x - 56.f;
  return (amount < 0.f ? -units : units) * .07725199f;
}
float synLpOctaves(float raw) {
  const float r = raw127(raw);
  // Low range retains the first fit; higher anchors use digital poles and noise-off ring pitch.
  // Independent cutoff56/72/88/104 captures validate the interpolated curve.
  if (r <= 48.f) return .35832215f + r * .07725199f;
  if (r <= 64.f) return 4.06641767f + (r - 48.f) * .078100083f;
  if (r <= 80.f) return 5.31601899f + (r - 64.f) * .078753099f;
  return 6.57606858f + (r - 80.f) * .078197288f;
}
float synLpQ(float raw) {
  const float r = raw127(raw);
  if (r > 96.f) {
    // Small-signal damping from independent noise-off tails, not undersampled saw peaks.
    const float damping = r <= 112.f ? .0625f + (.00993f - .0625f) * (r - 96.f) * (1.f / 16.f)
                                      : .00993f + (.004295f - .00993f) * (r - 112.f) * (1.f / 15.f);
    return fastRcp(damping);
  }
  const float q = .506f * fastExp2(r * .051905126f);  // .506 * 10^(raw/64)
  return q < .714f ? .714f : q;
}
float synLpGain(float raw) {
  const float r = raw127(raw);
  float db;
  if (r <= 32.f) db = -1.21415f * r * (1.f / 32.f);
  else if (r <= 64.f) db = -1.21415f - 2.91114f * (r - 32.f) * (1.f / 32.f);
  else if (r <= 96.f) db = -4.12529f - 3.73139f * (r - 64.f) * (1.f / 32.f);
  else if (r <= 112.f) db = -7.85668f - 1.41779f * (r - 96.f) * (1.f / 16.f);
  else db = -9.27447f - 2.55838f * (r - 112.f) * (1.f / 15.f);
  return fastExp2(db * .166096405f);
}
uint16_t synAttackMs(uint8_t raw) {
  if (!raw) return 0;
  return milliseconds(.3415f * fastExp2((raw > 127 ? 127 : raw) * .125f));
}
uint16_t synDecayTauMs(uint8_t raw) {
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

namespace mt {
float synAnalogOctaves(float raw, int poles) {
  // Weak-saw fits at raw48,64,80,96,112,127; raw0/32 extend the measured low slope.
  MT_CONTROL_DATA static const uint16_t oct[4][8] = {
    {0,2378,3640,5442,7067,8385,9978,10474},
    {0,2707,3996,5532,6943,8169,9329,10474},
    {0,2821,4068,5511,6835,8111,9176,9925},
    {0,2875,4159,5484,6791,8037,9105,9784}};
  raw = fclamp(raw,0.f,127.f); poles = poles < 1 ? 1 : poles > 4 ? 4 : poles;
  int i; float t;
  if (raw < 32.f) { i = 0; t = raw * (1.f/32.f); }
  else { i = static_cast<int>(raw * (1.f/16.f)) - 1; if(i>6)i=6; t=(raw-(i+1)*16.f)*(i==6?1.f/15.f:1.f/16.f); }
  const float a=oct[poles-1][i], b=oct[poles-1][i+1];
  return (a+(b-a)*t)*(1.f/1024.f);
}
}
