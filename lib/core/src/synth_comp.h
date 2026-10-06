#pragma once
#include <math.h>
#include <stdint.h>
#include "hot.h"

namespace mt {

// Master compressor: peak detector (1 ms attack, 20..1000 ms release), 4:1 above a threshold set by
// amt (-6 .. -30 dB), makeup half the threshold drop, halved. The gain is computed every kHold
// samples and held. Key = |mix| plus |sc| x scDepth / 127 x 4 (the sidechain bus, nullptr = none).
// amt 0 = bypass (bit-exact).
class Compressor {
 public:
  static constexpr int kHold = 8;
  static constexpr float kRate = 32000.f;
  void reset() {
    env_ = 0;
    gain_ = 1;
  }
  MT_HOT void process(float* mix, const float* sc, int n, uint8_t amt, uint8_t rel, uint8_t scDepth) {
    if (amt == 0) return;
    if (amt != amt_ || rel != rel_) {  // constants per setting, not per block
      amt_ = amt;
      rel_ = rel;
      const float a = amt > 127 ? 127 : amt;
      thrDb_ = -6.f - 24.f * a / 127.f;
      makeup_ = exp2f(-thrDb_ * 0.25f / 6.02f);
      const float relMs = 20.f * powf(50.f, (rel > 127 ? 127 : rel) / 127.f);
      ca_ = 1.f - expf(-1.f / (0.001f * kRate));
      cr_ = 1.f - expf(-1.f / (relMs * 0.001f * kRate));
    }
    const float scW = (scDepth > 127 ? 127 : scDepth) * (4.f / 127.f);
    for (int i = 0; i < n; ++i) {
      float key = fabsf(mix[i]);
      if (sc) key += fabsf(sc[i]) * scW;
      env_ += (key > env_ ? ca_ : cr_) * (key - env_);
      if (++hold_ >= kHold) {
        hold_ = 0;
        if (env_ < 1e-20f) env_ = 0;  // silence: no denormal tail
        const float envDb = env_ > 1e-6f ? 6.02f * log2f(env_) : -120.f;
        const float over = envDb - thrDb_;
        gain_ = (over > 0 ? exp2f(-over * 0.75f / 6.02f) : 1.f) * makeup_;
      }
      mix[i] *= gain_;
    }
  }
  float gainDb() const { return 6.02f * log2f(gain_); }  // current gain incl. makeup (tests)

 private:
  float env_ = 0, gain_ = 1;
  int hold_ = kHold - 1;  // the first sample computes a gain
  uint8_t amt_ = 0, rel_ = 0;
  float thrDb_ = -6, makeup_ = 1, ca_ = 0, cr_ = 0;
};

}  // namespace mt
