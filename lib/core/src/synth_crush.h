#pragma once
#include <math.h>
#include <stdint.h>
#include "hot.h"

namespace mt {

// Lo-fi per voice: bit depth and sample-rate reduction (fx BIT / SRR), after the drive, before the
// filter. bits 0 = off .. 127 = 2 bits; rate 0 = off .. 127 = holds each sample ~64 samples.
struct Crush {
  // Per control update: the unchanged case inline, a change out of line (synth_crush.cpp,
  // hot too: an LFO on BIT / SRR moves it every control period).
  MT_INLINE void set(uint8_t bits, uint8_t rate) {
    bits = bits > 127 ? 127 : bits;
    rate = rate > 127 ? 127 : rate;
    if (bits == bits_ && rate == rate_) return;
    change(bits, rate);
  }
  void change(uint8_t bits, uint8_t rate);
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
    if (bits_) {  // floor(x / step + 0.5) x step, no floorf (a flash call per sample on the ESP32)
      const float y = x * rstep_ + 0.5f;  // step_ is a power of two: x * rstep_ == x / step_
      int i = static_cast<int>(y);
      if (static_cast<float>(i) > y) --i;
      x = static_cast<float>(i) * step_;
    }
    return x;
  }

 private:
  uint8_t bits_ = 0, rate_ = 0;
  float step_ = 0, rstep_ = 0, inc_ = 1, phase_ = 1, held_ = 0;
};

}  // namespace mt
