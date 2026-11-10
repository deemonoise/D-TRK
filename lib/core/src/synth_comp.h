#pragma once
#include <math.h>
#include <stdint.h>
#include "hot.h"

namespace mt {

// Stereo-linked master compressor: soft knee, no makeup, and smoothly interpolated gain.
// Amount moves ratio 1:1..4:1 and threshold 0..-18 dB; attack .5..50 ms, release 20..1000 ms.
// External SC detects only the key, depth scales its reduction in dB, and the dry key stays dry.
class Compressor {
 public:
  static constexpr int kHold = 8;
  static constexpr uint8_t kDefaultAttack = 83;  // approximately 10 ms
  MT_INLINE void reset() {
    // Explicit stores: aggregate assignment can copy a reset template from flash in IRAM code.
    envMix_ = 0;
    envSc_ = 0;
    rawGain_ = 1;
    intervalTarget_ = 1;
    gainStep_ = 0;
    gain_ = 1;
    wet_ = 0;
    scBlend_ = 0;
    depth_ = 0;
    controlLeft_ = 0;
    parameterLeft_ = 0;
    amt_ = 255;
    rel_ = 255;
    attack_ = 255;
    thrDb_ = 0;
    slope_ = 0;
    ca_ = 0;
    cr_ = 0;
  }
  // l/r contain the FULL mix, including the dry key in scL/scR. The key buffers must not alias
  // l/r; both are set or both nullptr. l/r must not alias. OFF fades to exact bypass in 5 ms;
  // a freshly constructed/reset compressor bypasses immediately.
  void process(float* l, float* r, const float* scL, const float* scR, int n, uint8_t amt, uint8_t rel,
               uint8_t scDepth, uint8_t attack = kDefaultAttack);
  float gainDb() const { return 6.0206f * log2f(gain_); }  // applied program gain, excluding dry key

 private:
  float envMix_ = 0, envSc_ = 0;
  float rawGain_ = 1, intervalTarget_ = 1, gainStep_ = 0, gain_ = 1;
  float wet_ = 0, scBlend_ = 0, depth_ = 0;
  int controlLeft_ = 0, parameterLeft_ = 0;
  uint8_t amt_ = 255, rel_ = 255, attack_ = 255;
  float thrDb_ = 0, slope_ = 0, ca_ = 0, cr_ = 0;
};

}  // namespace mt
