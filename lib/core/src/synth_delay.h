#pragma once
#include <stdint.h>

namespace mt {

// Stereo send delay: two independent lines (L, R), same settings, no cross-feed. The int16 lines are
// owned by the caller (one buffer of 2 x len); without one the delay is silent. A one-pole LP (TONE)
// sits on each line's output, so the first echo and every repeat get darker; the repeats are fed
// back through it.
class Delay {
 public:
  static constexpr float kScale = 8192.f;  // line units per 1.0: +-4.0 before it saturates

  // buf: 2 x len samples (L line, then R line), nullptr = off. Clears it.
  void setBuffer(int16_t* buf, uint32_t len);
  void clear();
  // mixL/R[i] += the return of each line; a line takes send[i] + return x feedback. delay: samples,
  // clamped to 1..len-1. fb, tone, level: 0..127 (Project::dly*). tone 127 = no filter.
  void process(const float* sendL, const float* sendR, float* mixL, float* mixR, int n, uint32_t delay,
               uint8_t fb, uint8_t tone, uint8_t level);

 private:
  int16_t* buf_ = nullptr;
  uint32_t len_ = 0;  // per line
  uint32_t w_ = 0;    // write position
  float lp_[2] = {};
};

}  // namespace mt
