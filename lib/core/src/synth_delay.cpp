#include "synth_delay.h"
#include <math.h>
#include <string.h>
#include "hot.h"
#include "synth_osc.h"

namespace mt {
namespace {

constexpr float kFbMax = 0.95f;  // fb 127: repeats still die out

// TONE 0..126 -> LP cutoff 300 Hz .. 16 kHz exponentially; one-pole coefficient.
float toneCoef(uint8_t tone) {
  if (tone >= 127) return 1.f;
  const float hz = 300.f * powf(16000.f / 300.f, tone / 127.f);
  return 1.f - expf(-6.2831853f * hz / kSynthRate);
}

}  // namespace

void Delay::setBuffer(int16_t* buf, uint32_t len) {
  buf_ = len > 1 ? buf : nullptr;
  len_ = buf_ ? len : 0;
  clear();
}

void Delay::clear() {
  if (buf_) memset(buf_, 0, sizeof(int16_t) * len_);
  w_ = 0;
  lp_ = 0;
}

MT_HOT void Delay::process(const float* send, float* mix, int n, uint32_t delay, uint8_t fb, uint8_t tone,
                           uint8_t level) {
  if (!buf_) return;
  if (delay < 1) delay = 1;
  if (delay > len_ - 1) delay = len_ - 1;
  const float a = toneCoef(tone);
  const float g = (fb > 127 ? 127 : fb) * (kFbMax / 127.f);
  const float lv = (level > 127 ? 127 : level) * (1.f / 127.f);
  uint32_t w = w_;
  uint32_t r = w >= delay ? w - delay : w + len_ - delay;
  float lp = lp_;
  for (int i = 0; i < n; ++i) {
    lp += (buf_[r] * (1.f / kScale) - lp) * a;
    mix[i] += lp * lv;
    float x = (send[i] + lp * g) * kScale;
    x = x > 32767.f ? 32767.f : (x < -32768.f ? -32768.f : x);
    buf_[w] = static_cast<int16_t>(lrintf(x));
    if (++w == len_) w = 0;
    if (++r == len_) r = 0;
  }
  // Denormals: a long silent tail would crawl through them on the S3's FPU.
  lp_ = fabsf(lp) < 1e-9f ? 0.f : lp;
  w_ = w;
}

}  // namespace mt
