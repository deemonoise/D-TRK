#include "synth_reverb.h"
#include "synth_osc.h"

namespace mt {

constexpr int Reverb::kCombLen[Reverb::kCombs];
constexpr int Reverb::kApLen[Reverb::kAllpasses];
namespace {
constexpr int kRampSamples = (kSynthRate * 5 + 999) / 1000;
constexpr float kRampStep = 1.f / kRampSamples;

// Schroeder allpass, g=.5: |H|=1. The delayed state is v, not the output.
MT_INLINE float allpass(float* line, int& idx, int len, float x) {
  const float b = line[idx];
  const float v = x + b * .5f;
  line[idx] = v;
  if (++idx >= len) idx = 0;
  return b - v * .5f;
}
MT_INLINE float readComb(const float* line, int write, int delay, int capacity) {
  const int read = write >= delay ? write - delay : write + capacity - delay;
  return line[read];
}
}  // namespace

MT_HOT void Reverb::Ramp::set(float x) {
  if (x == target) return;
  target = x;
  step = (target - value) * kRampStep;
  left = kRampSamples;
}

void Reverb::setBuffer(float* buf, int len) {
  buf_ = buf && len >= kBufLen ? buf : nullptr;
  if (!buf_) { clear(); return; }
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

MT_HOT void Reverb::clearTail() {
  if (buf_) for (int i = 0; i < kBufLen; ++i) buf_[i] = 0;
  for (int c = 0; c < kCombs; ++c) combIdx_[c] = 0, store_[c] = 0;
  for (int a = 0; a < kAllpasses; ++a) apIdx_[a] = 0;
  apRIdx_ = 0;
  idle_ = true;
  quiet_ = 0;
}

MT_HOT void Reverb::clear() {
  clearTail();
  for (int c = 0; c < kCombs; ++c)
    combCapacity_[c] = combLen_[c] = nextLen_[c] = kCombLen[c];
  size_ = nextSize_ = 255;
  sizeLeft_ = 0;
  sizeMix_ = 0;
  wet_.snap(0); feedback_.snap(0); damping_.snap(0);
  initialized_ = false;
}

MT_HOT void Reverb::setSize(uint8_t size, bool snap) {
  const float scale = .5f + .5f * size * (1.f / 127.f);
  for (int c = 0; c < kCombs; ++c) {
    nextLen_[c] = static_cast<int>(combCapacity_[c] * scale);
    if (snap) combLen_[c] = nextLen_[c];
  }
  nextSize_ = size;
  if (snap) size_ = size;
  sizeMix_ = 0;
  sizeLeft_ = snap ? 0 : kRampSamples;
}

MT_HOT void Reverb::process(const float* in, float* mixL, float* mixR, int n, uint8_t size, uint8_t damp,
                            uint8_t level, bool freeze, float* wetOut) {
  if (!buf_ || n <= 0) return;
  if (size > 127) size = 127;
  if (damp > 127) damp = 127;
  if (level > 127) level = 127;
  const float fb = freeze ? 1.f : .70f + .28f * size * (1.f / 127.f);
  const float d = freeze ? 0.f : damp * (.4f / 127.f);
  const float wet = level * (.5f / 127.f);
  if (!initialized_) {
    wet_.snap(wet); feedback_.snap(fb); damping_.snap(d);
    setSize(size, true);
    initialized_ = true;
  } else {
    wet_.set(wet); feedback_.set(fb); damping_.set(d);
    if (!freeze && sizeLeft_ == 0 && size != size_) setSize(size, false);
  }
  if (muted()) return;
  for (int i = 0; i < n; ++i) {
    const float lv = wet_.next(), f = feedback_.next(), dc = damping_.next();
    if (muted()) { clear(); return; }  // no old tail can return after a completed mute
    const float x = freeze ? 0.f : in[i] * .25f;
    const bool moving = sizeLeft_ > 0;
    if (moving) sizeMix_ = sizeLeft_ == 1 ? 1.f : sizeMix_ + kRampStep;
    if (!idle_ || x != 0.f) {
      idle_ = false;
      float acc = 0;
      for (int c = 0; c < kCombs; ++c) {
        float* line = comb_[c];
        const int write = combIdx_[c], cap = combCapacity_[c];
        float out = readComb(line, write, combLen_[c], cap);
        if (moving) {
          const float to = readComb(line, write, nextLen_[c], cap);
          out += (to - out) * sizeMix_;
        }
        store_[c] = out * (1.f - dc) + store_[c] * dc;
        line[write] = x + store_[c] * f;
        if (++combIdx_[c] >= cap) combIdx_[c] = 0;
        acc += out;
      }
      acc = allpass(ap_[0], apIdx_[0], kApLen[0], acc);
      const float l = allpass(ap_[1], apIdx_[1], kApLen[1], acc);
      const float r = allpass(apR_, apRIdx_, kApRLen, acc);
      mixL[i] += l * lv;
      mixR[i] += r * lv;
      if (wetOut) wetOut[i] = l;
      const float al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
      quiet_ = !freeze && x == 0.f && al < 1e-5f && ar < 1e-5f ? quiet_ + 1 : 0;
      if (quiet_ >= kQuietIdle) clearTail();
    }
    if (moving && --sizeLeft_ == 0) {
      for (int c = 0; c < kCombs; ++c) combLen_[c] = nextLen_[c];
      size_ = nextSize_;
      sizeMix_ = 0;
      // A retarget while fading takes the newest requested size after landing. At most two
      // 5 ms transitions, and no jump from abruptly replacing an in-flight read mixture.
      if (!freeze && size != size_) setSize(size, false);
    }
  }
}

}  // namespace mt
