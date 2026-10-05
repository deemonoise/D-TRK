#pragma once
#include <math.h>
#include <stdint.h>
#include "hot.h"
#include "synth_osc.h"

namespace mt {

// State-variable filter (Simper, trapezoidal integration): stable under fast cutoff changes.
// set() at control rate (tanf), process() per sample.
struct Svf {
  enum class Mode : uint8_t { Lp, Bp, Hp };

  // hz is clamped to 20 Hz .. 0.45 x the sample rate, q to >= 0.5.
  void set(Mode m, float hz, float q) {
    mode_ = m;
    const float maxHz = kSynthRate * 0.45f;
    hz = hz < 20.f ? 20.f : (hz > maxHz ? maxHz : hz);
    q = q < 0.5f ? 0.5f : q;
    const float g = tanf(3.14159265f * hz / kSynthRate);
    k_ = 1.f / q;
    a1_ = 1.f / (1.f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
  }
  void reset() { ic1_ = ic2_ = 0; }
  MT_HOT MT_INLINE float process(float v0) {
    const float v3 = v0 - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.f * v1 - ic1_;
    ic2_ = 2.f * v2 - ic2_;
    switch (mode_) {
      case Mode::Lp: return v2;
      case Mode::Bp: return v1;
      default: return v0 - k_ * v1 - v2;
    }
  }

 private:
  Mode mode_ = Mode::Lp;
  float ic1_ = 0, ic2_ = 0;
  float a1_ = 0, a2_ = 0, a3_ = 0, k_ = 2;
};

}  // namespace mt
