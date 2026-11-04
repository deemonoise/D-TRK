#pragma once
#include "hot.h"
#include "synth_osc.h"
namespace mt {
// A finite 5 ms ramp, shared by MAIN and Phones. Cold/reset starts at the requested value.
class GainRamp {
 public:
  static constexpr int kSamples = (kSynthRate * 5 + 999) / 1000;
  MT_INLINE void reset() { initialized_ = false; left_ = 0; }
  MT_INLINE void set(float target) {
    if (!initialized_) { current_ = target_ = target; left_ = 0; initialized_ = true; }
    else if (target != target_) {
      target_ = target;
      left_ = kSamples;
      step_ = (target_ - current_) * (1.f / kSamples);
    }
  }
  MT_INLINE float next() {
    if (left_ && --left_ == 0) current_ = target_;
    else if (left_) current_ += step_;
    return current_;
  }
  float current() const { return current_; }
  bool moving() const { return left_ != 0; }
 private:
  float current_ = 1, target_ = 1, step_ = 0;
  int left_ = 0;
  bool initialized_ = false;
};
}  // namespace mt
