#include "synth_reverb.h"
#include "hot.h"

namespace mt {

constexpr int Reverb::kCombLen[Reverb::kCombs];
constexpr int Reverb::kApLen[Reverb::kAllpasses];

void Reverb::setBuffer(float* buf, int len) {
  buf_ = buf && len >= kBufLen ? buf : nullptr;
  if (!buf_) return;
  float* p = buf_;
  for (int k = 0; k < 2; ++k) {
    Chan& c = ch_[k];
    const int spread = k * kStereoSpread;
    for (int i = 0; i < kCombs; ++i) {
      c.comb[i] = p;
      c.combMax[i] = kCombLen[i] + spread;
      p += c.combMax[i];
    }
    for (int a = 0; a < kAllpasses; ++a) {
      c.ap[a] = p;
      c.apLen[a] = kApLen[a] + spread;
      p += c.apLen[a];
    }
  }
  clear();
}

void Reverb::zero() {
  for (int i = 0; i < kBufLen; ++i) buf_[i] = 0;
  for (Chan& c : ch_)
    for (float& s : c.store) s = 0;
}

void Reverb::clear() {
  if (!buf_) return;
  zero();
  for (Chan& c : ch_) {
    for (int i = 0; i < kCombs; ++i) {
      c.combIdx[i] = 0;
      c.combLen[i] = c.combMax[i];
    }
    for (int a = 0; a < kAllpasses; ++a) c.apIdx[a] = 0;
  }
  idle_ = true;
  quiet_ = 0;
}

MT_HOT float Reverb::run(Chan& c, const float* in, float* out, int n, float fb, float d, float wet) {
  float peak = 0;
  for (int i = 0; i < n; ++i) {
    const float x = in[i] * 0.25f;  // 4 combs summed
    float acc = 0;
    for (int k = 0; k < kCombs; ++k) {
      float* line = c.comb[k];
      const float o = line[c.combIdx[k]];
      c.store[k] = o * (1.f - d) + c.store[k] * d;
      line[c.combIdx[k]] = x + c.store[k] * fb;
      if (++c.combIdx[k] >= c.combLen[k]) c.combIdx[k] = 0;
      acc += o;
    }
    for (int a = 0; a < kAllpasses; ++a) {  // Freeverb allpass, g = 0.5
      float* line = c.ap[a];
      const float b = line[c.apIdx[a]];
      line[c.apIdx[a]] = acc + b * 0.5f;
      acc = b - acc * 0.5f;
      if (++c.apIdx[a] >= c.apLen[a]) c.apIdx[a] = 0;
    }
    out[i] += acc * wet;
    const float m = acc < 0 ? -acc : acc;
    if (m > peak) peak = m;
  }
  return peak;
}

MT_HOT void Reverb::process(const float* in, float* mixL, float* mixR, int n, uint8_t size, uint8_t damp,
                            uint8_t level) {
  if (!buf_ || level == 0) return;
  bool input = false;
  for (int i = 0; i < n && !input; ++i) input = in[i] != 0.f;
  if (idle_ && !input) return;
  idle_ = false;
  const float sz = (size > 127 ? 127 : size) * (1.f / 127.f);
  const float fb = 0.70f + 0.28f * sz;
  const float d = (damp > 127 ? 127 : damp) * (0.4f / 127.f);
  const float wet = (level > 127 ? 127 : level) * (0.5f / 127.f);
  // A size change moves the loop end (no glide); the write index wraps into the new length.
  for (Chan& c : ch_)
    for (int k = 0; k < kCombs; ++k) {
      const int len = static_cast<int>(c.combMax[k] * (0.5f + 0.5f * sz));
      c.combLen[k] = len < 1 ? 1 : len;
      if (c.combIdx[k] >= c.combLen[k]) c.combIdx[k] = 0;
    }
  const float pl = run(ch_[0], in, mixL, n, fb, d, wet);
  const float pr = run(ch_[1], in, mixR, n, fb, d, wet);
  const float peak = pl > pr ? pl : pr;
  quiet_ = !input && peak < 1e-5f ? quiet_ + n : 0;
  if (quiet_ >= kQuietIdle) {  // the tail has died out: drop the remains, rest until the next input
    zero();
    idle_ = true;
    quiet_ = 0;
  }
}

}  // namespace mt
