#include "stream_ring.h"

namespace mt {

int LinearResampler::push(const int16_t* in, int n, int16_t* out) {
  if (n <= 0) return 0;
  int k = 0;
  // Output k_ needs input frames i and i + 1, i = k_ * in_ / out_; input inBase_ - 1 is prev_.
  for (;;) {
    const uint64_t num = k_ * in_;
    const uint64_t i = num / out_;
    if (i + 1 >= inBase_ + n) break;
    const uint32_t frac = static_cast<uint32_t>(num % out_);
    const int a = i + 1 == inBase_ ? prev_ : in[i - inBase_];
    const int b = in[i + 1 - inBase_];
    out[k++] = static_cast<int16_t>(a + static_cast<int64_t>(b - a) * frac / out_);
    ++k_;
  }
  prev_ = in[n - 1];
  inBase_ += n;
  return k;
}

void StreamRing::reset() {
  w_.store(0);
  r_.store(0);
  done_.store(false);
  underruns_.store(0);
}

void StreamRing::write(const int16_t* d, uint32_t n) {
  uint32_t w = w_.load();
  for (uint32_t i = 0; i < n; ++i, ++w) buf_[w % kLen] = d[i];
  w_.store(w);
}

void StreamRing::mix(int16_t* l, int16_t* rt, int n, float gain) {
  if (!active_.load()) return;
  uint32_t r = r_.load();
  const uint32_t have = w_.load() - r;
  const int m = have < static_cast<uint32_t>(n) ? static_cast<int>(have) : n;
  const int32_t g = static_cast<int32_t>(gain * 32768.f);
  auto add = [](int16_t& o, int32_t x) {
    const int32_t v = o + x;
    o = static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
  };
  for (int i = 0; i < m; ++i, ++r) {
    const int32_t x = (buf_[r % kLen] * g) >> 15;
    add(l[i], x);
    if (rt) add(rt[i], x);
  }
  r_.store(r);
  if (m < n) {
    if (done_.load()) active_.store(false);
    else underruns_.fetch_add(1);
  }
}

}  // namespace mt
