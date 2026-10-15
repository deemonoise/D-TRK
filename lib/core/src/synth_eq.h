#pragma once
#include <stdint.h>
#include "synth_fxutil.h"

namespace mt {

// Master EQ: low shelf (120 Hz) + high shelf (5 kHz), stereo, -12..+12 dB each.
// y = x + gL * lp(x) + gH * (x - lp'(x)) with TPT one-poles: the low shelf is exact at DC and
// leaves Nyquist alone, the high shelf is exact at Nyquist and leaves DC alone. A cut is the exact
// inverse of the same boost (its corner moves by the gain), so +-12 dB are mirror images. Opposite
// settings make a tilt. low = high = 64 with the gains settled: skipped (bit-exact). Gain changes ramp
// linearly over the block (no zipper / click).
class MasterEq {
 public:
  static constexpr float kLowHz = 120.f, kHighHz = 5000.f;
  // 0..127 -> -12..+12 dB, 64 = 0 dB.
  static float db(uint8_t v);
  void reset();
  // In place. l and r must not alias.
  void process(float* l, float* r, int n, uint8_t low, uint8_t high);

 private:
  void retarget(uint8_t low, uint8_t high);
  template <bool kLo, bool kHi>
  void run(float* l, float* r, int n);
  OnePoleTpt lo_[2], hi_[2];
  float gL_ = 0, gH_ = 0;  // current linear gain - 1
  float cL_, cH_;          // current TPT coefficients (corners)
  uint8_t low_ = 64, high_ = 64;
  float tgtL_ = 0, tgtH_ = 0, tcL_, tcH_;

 public:
  MasterEq() { reset(); }
};

}  // namespace mt
