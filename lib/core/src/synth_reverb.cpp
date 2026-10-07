#include "synth_reverb.h"
#include "hot.h"

namespace mt {

constexpr int Reverb::kCombLen[Reverb::kCombs];
constexpr int Reverb::kApLen[Reverb::kAllpasses];

void Reverb::setBuffer(float* buf, int len) {
  buf_ = buf && len >= kBufLen ? buf : nullptr;
  if (!buf_) return;
  float* p = buf_;
  for (int c = 0; c < kCombs; ++c) {
    comb_[c] = p;
    p += kCombLen[c];
  }
  for (int a = 0; a < kAllpasses; ++a) {
    ap_[a] = p;
    p += kApLen[a];
  }
  clear();
}

void Reverb::clear() {
  if (!buf_) return;
  for (int i = 0; i < kBufLen; ++i) buf_[i] = 0;
  for (int c = 0; c < kCombs; ++c) {
    combIdx_[c] = 0;
    store_[c] = 0;
    combLen_[c] = kCombLen[c];
  }
  for (int a = 0; a < kAllpasses; ++a) apIdx_[a] = 0;
  idle_ = true;
  quiet_ = 0;
}

MT_HOT void Reverb::process(const float* in, float* mix, int n, uint8_t size, uint8_t damp, uint8_t level) {
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
  for (int c = 0; c < kCombs; ++c) {
    const int len = static_cast<int>(kCombLen[c] * (0.5f + 0.5f * sz));
    combLen_[c] = len < 1 ? 1 : len;
    if (combIdx_[c] >= combLen_[c]) combIdx_[c] = 0;
  }
  float peak = 0;
  for (int i = 0; i < n; ++i) {
    const float x = in[i] * 0.25f;  // 4 combs summed
    float acc = 0;
    for (int c = 0; c < kCombs; ++c) {
      float* line = comb_[c];
      const float out = line[combIdx_[c]];
      store_[c] = out * (1.f - d) + store_[c] * d;
      line[combIdx_[c]] = x + store_[c] * fb;
      if (++combIdx_[c] >= combLen_[c]) combIdx_[c] = 0;
      acc += out;
    }
    for (int a = 0; a < kAllpasses; ++a) {  // Freeverb allpass, g = 0.5
      float* line = ap_[a];
      const float b = line[apIdx_[a]];
      line[apIdx_[a]] = acc + b * 0.5f;
      acc = b - acc * 0.5f;
      if (++apIdx_[a] >= kApLen[a]) apIdx_[a] = 0;
    }
    mix[i] += acc * wet;
    const float a = acc < 0 ? -acc : acc;
    if (a > peak) peak = a;
  }
  quiet_ = !input && peak < 1e-5f ? quiet_ + n : 0;
  if (quiet_ >= kQuietIdle) {  // the tail has died out: drop the remains, rest until the next input
    for (int i = 0; i < kBufLen; ++i) buf_[i] = 0;
    for (float& s : store_) s = 0;
    idle_ = true;
    quiet_ = 0;
  }
}

}  // namespace mt
