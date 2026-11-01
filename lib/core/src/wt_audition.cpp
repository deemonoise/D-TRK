#include "wt_audition.h"
#include <math.h>
#include <string.h>
#include "wt_mip.h"
namespace mt {
void wtAuditionPrepare(const int16_t* in, int16_t* out, float* scratch) {
  float* re = scratch; float* im = scratch + kWtHarm;
  wtAnalyze(in, 256, re, im);
  for (int n = 0; n < 256; ++n) {
    float v = 0;
    for (int h = 1; h <= 32; ++h) {
      const float a = 6.28318530718f * (h * n) / 256.f;
      v += re[h - 1] * cosf(a) + im[h - 1] * sinf(a);
    }
    out[n] = static_cast<int16_t>(v < -32767 ? -32767 : v > 32767 ? 32767 : lrintf(v));
  }
}
void WtAudition::stop() { staged_ = false; remaining_ = 0; }
void WtAudition::expire(uint32_t now) {
  if (staged_ && now - at_ > 1000) staged_ = false;
}
bool WtAudition::part(uint16_t token, unsigned half, const int16_t* pts, uint32_t now, float* scratch) {
  expire(now);
  if (!wave_ || !stage_ || !pts || half > 1) return false;
  if (!half) {
    stop(); token_ = token; at_ = now; staged_ = true;
    memcpy(stage_, pts, 128 * sizeof(int16_t));
    return true;
  }
  if (!staged_ || token != token_) { staged_ = false; return false; }
  memcpy(stage_ + 128, pts, 128 * sizeof(int16_t));
  wtAuditionPrepare(stage_, wave_, scratch);
  staged_ = false; remaining_ = 26460; produced_ = phase_ = 0;  // 600 ms at 44.1kHz
  return true;
}
unsigned WtAudition::render(int16_t* out, unsigned n) {
  if (n > remaining_) n = remaining_;
  for (unsigned i = 0; i < n; ++i) {
    const unsigned index = phase_ >> 24;
    const float f = (phase_ & 0xFFFFFF) * (1.f / 16777216.f);
    float v = wave_[index] + (wave_[(index + 1) & 255] - wave_[index]) * f;
    float gain = 1;
    if (produced_ < 220) gain = produced_ / 220.f;
    if (remaining_ < 441) gain = remaining_ / 441.f;
    out[i] = static_cast<int16_t>(v * gain);
    phase_ += 25480119u;  // C4 261.625565Hz / 44100 * 2^32
    ++produced_; --remaining_;
  }
  return n;
}
}
