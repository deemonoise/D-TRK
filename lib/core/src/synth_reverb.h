#pragma once
#include <stdint.h>

namespace mt {

// Stereo reverb, Freeverb-lite: per channel 4 parallel damped feedback combs, then 2 series
// allpasses, fed the same mono input; the right channel's lines are kStereoSpread samples longer
// (Freeverb's spread), so L and R decorrelate. The float buffer (kBufLen) is owned by the caller;
// without one the reverb is silent.
class Reverb {
 public:
  static constexpr int kCombs = 4, kAllpasses = 2;
  static constexpr int kCombLen[kCombs] = {1116, 1188, 1277, 1356};
  static constexpr int kApLen[kAllpasses] = {556, 441};
  static constexpr int kStereoSpread = 23;
  static constexpr int kChanLen = 1116 + 1188 + 1277 + 1356 + 556 + 441;  // 5934 floats, L
  static constexpr int kBufLen = 2 * kChanLen + (kCombs + kAllpasses) * kStereoSpread;  // 12006 floats

  // buf: len floats, nullptr or too short = off. Clears it.
  void setBuffer(float* buf, int len);
  void clear();
  // mixL/R[i] += the reverb of in[] x level / 127 x 0.5. size: comb lengths and feedback, damp: high
  // loss in the combs, level: return; 0..127 (Project::rvb*). level 0 leaves the mix untouched.
  // Silent input with the tail below -100 dB for kQuietIdle samples: idle (no work, mix untouched)
  // until the input is non-zero.
  void process(const float* in, float* mixL, float* mixR, int n, uint8_t size, uint8_t damp, uint8_t level);

 private:
  struct Chan {
    float* comb[kCombs] = {};
    float* ap[kAllpasses] = {};
    int combIdx[kCombs] = {}, apIdx[kAllpasses] = {};
    int combMax[kCombs] = {}, apLen[kAllpasses] = {};  // full lengths (this channel)
    int combLen[kCombs] = {};                          // current lengths (size)
    float store[kCombs] = {};                          // damping state
  };
  // One channel's block: out[i] += reverb x wet; returns the peak of the reverb.
  static float run(Chan& c, const float* in, float* out, int n, float fb, float d, float wet);
  void zero();

  float* buf_ = nullptr;
  Chan ch_[2];
  bool idle_ = true;  // the lines hold (next to) nothing and no input came since
  int quiet_ = 0;     // samples of silent input and inaudible output in a row
  static constexpr int kQuietIdle = 4096;  // > the longest path through the lines (1379 + 579 + 464)
};

}  // namespace mt
