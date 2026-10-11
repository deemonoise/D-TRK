#pragma once
#include <math.h>
#include <stdint.h>

namespace mt {

// Master compressor: peak detector (1 ms attack, 20..1000 ms release), 4:1 above a threshold set by
// amt (-6 .. -30 dB), makeup half the threshold drop, halved. The gain is computed every kHold
// samples and held. Stereo-linked: key = max(|l|, |r|) plus max(|scL|, |scR|) x scDepth / 127 x 4
// (the sidechain bus), one gain on both sides. amt 0 = bypass (bit-exact).
class Compressor {
 public:
  static constexpr int kHold = 8;
  static constexpr float kRate = 32000.f;
  void reset() {
    env_ = 0;
    gain_ = 1;
  }
  // scL, scR: both set or both nullptr (no sidechain). l and r must not alias.
  void process(float* l, float* r, const float* scL, const float* scR, int n, uint8_t amt, uint8_t rel,
               uint8_t scDepth);
  float gainDb() const { return 6.02f * log2f(gain_); }  // current gain incl. makeup (tests)

 private:
  float env_ = 0, gain_ = 1;
  int hold_ = kHold - 1;  // the first sample computes a gain
  uint8_t amt_ = 0, rel_ = 0;
  float thrDb_ = -6, makeup_ = 1, ca_ = 0, cr_ = 0;
};

}  // namespace mt
