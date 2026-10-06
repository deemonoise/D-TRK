#pragma once
#include <stdint.h>

namespace mt {

// Mono reverb, Freeverb-lite: 4 parallel damped feedback combs, then 2 series allpasses. The float
// buffer (kBufLen) is owned by the caller (firmware: PSRAM); without one the reverb is silent.
class Reverb {
 public:
  static constexpr int kCombs = 4, kAllpasses = 2;
  static constexpr int kCombLen[kCombs] = {1116, 1188, 1277, 1356};
  static constexpr int kApLen[kAllpasses] = {556, 441};
  static constexpr int kBufLen = 1116 + 1188 + 1277 + 1356 + 556 + 441;  // 5934 floats

  // buf: len floats, nullptr or too short = off. Clears it.
  void setBuffer(float* buf, int len);
  void clear();
  // mix[i] += the reverb of in[] x level / 127 x 0.5. size: comb lengths and feedback, damp: high
  // loss in the combs, level: return; 0..127 (Project::rvb*). level 0 leaves mix untouched. Silent
  // input with the tail below -100 dB for kQuietIdle samples: idle (no work, mix untouched) until the
  // input is non-zero.
  void process(const float* in, float* mix, int n, uint8_t size, uint8_t damp, uint8_t level);

 private:
  float* buf_ = nullptr;
  float* comb_[kCombs] = {};
  float* ap_[kAllpasses] = {};
  int combIdx_[kCombs] = {}, apIdx_[kAllpasses] = {};
  int combLen_[kCombs] = {};  // current lengths (size)
  float store_[kCombs] = {};  // damping state
  bool idle_ = true;          // the lines hold (next to) nothing and no input came since
  int quiet_ = 0;             // samples of silent input and inaudible output in a row
  static constexpr int kQuietIdle = 4096;  // > the longest path through the lines (1356 + 556 + 441)
};

}  // namespace mt
