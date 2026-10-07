#pragma once
#include <math.h>
#include <stdint.h>
#include "hot.h"

namespace mt {

// Lo-fi per voice: bit depth and sample-rate reduction (fx BIT / SRR), after the drive, before the
// filter. bits 0 = off .. 127 = 2 bits; rate 0 = off .. 127 = holds each sample ~64 samples.
struct Crush {
  void set(uint8_t bits, uint8_t rate) {  // per control update
    bits = bits > 127 ? 127 : bits;
    rate = rate > 127 ? 127 : rate;
    if (bits == bits_ && rate == rate_) return;
    bits_ = bits;
    rate_ = rate;
    // 16 bits at 1 down to 2 at 127; the signal is about +-1 at full scale.
    const int b = 16 - (bits_ * 14 + 63) / 127;
    step_ = bits_ ? 2.f / static_cast<float>(1 << (b > 1 ? b - 1 : 0)) : 0.f;
    inc_ = 1.f / (1.f + static_cast<float>(rate_) * rate_ / 256.f);
  }
  bool on() const { return bits_ || rate_; }
  void reset() {
    phase_ = 1.f;
    held_ = 0;
  }
  // x[i] = process(x[i]) over a block.
  MT_HOT MT_INLINE void processBlock(float* __restrict x, int n) {
    Crush c = *this;  // the state in registers
    for (int i = 0; i < n; ++i) x[i] = c.process(x[i]);
    *this = c;
  }
  MT_HOT MT_INLINE float process(float x) {
    if (rate_) {
      phase_ += inc_;
      if (phase_ >= 1.f) {
        phase_ -= 1.f;
        held_ = x;
      }
      x = held_;
    }
    if (bits_) x = floorf(x / step_ + 0.5f) * step_;
    return x;
  }

 private:
  uint8_t bits_ = 0, rate_ = 0;
  float step_ = 0, inc_ = 1, phase_ = 1, held_ = 0;
};

}  // namespace mt
