#pragma once
#include <stdint.h>

namespace mt {

// Mono send delay. The int16 line is owned by the caller (firmware: PSRAM); without one the delay is
// silent. A one-pole LP (TONE) sits on the line's output, so the first echo and every repeat get
// darker; the repeats are fed back through it.
class Delay {
 public:
  static constexpr float kScale = 8192.f;  // line units per 1.0: +-4.0 before it saturates

  // buf: len samples, nullptr = off. Clears it.
  void setBuffer(int16_t* buf, uint32_t len);
  void clear();
  // mix[i] += the return; the line takes send[i] + return x feedback. delay: samples, clamped to
  // 1..len-1. fb, tone, level: 0..127 (Project::dly*). tone 127 = no filter.
  void process(const float* send, float* mix, int n, uint32_t delay, uint8_t fb, uint8_t tone, uint8_t level);

 private:
  int16_t* buf_ = nullptr;
  uint32_t len_ = 0;
  uint32_t w_ = 0;  // write position
  float lp_ = 0;
};

}  // namespace mt
