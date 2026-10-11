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
  apR_ = p;
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
  apRIdx_ = 0;
  idle_ = true;
  quiet_ = 0;
}

namespace {
// Freeverb allpass, g = 0.5.
MT_INLINE float allpass(float* line, int& idx, int len, float x) {
  const float b = line[idx];
  line[idx] = x + b * 0.5f;
  if (++idx >= len) idx = 0;
  return b - x * 0.5f;
}
}  // namespace

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
    acc = allpass(ap_[0], apIdx_[0], kApLen[0], acc);
    const float l = allpass(ap_[1], apIdx_[1], kApLen[1], acc);
    mixL[i] += l * wet;
    const float al = l < 0 ? -l : l;
    if (al > peak) peak = al;
    const float r = allpass(apR_, apRIdx_, kApRLen, acc);
    mixR[i] += r * wet;
    const float ar = r < 0 ? -r : r;
    if (ar > peak) peak = ar;
  }
  quiet_ = !input && peak < 1e-5f ? quiet_ + n : 0;
  if (quiet_ >= kQuietIdle) {  // the tail has died out: drop the remains, rest until the next input
    for (int i = 0; i < kBufLen; ++i) buf_[i] = 0;
    for (float& s : store_) s = 0;
    apRIdx_ = 0;
    idle_ = true;
    quiet_ = 0;
  }
}

}  // namespace mt
