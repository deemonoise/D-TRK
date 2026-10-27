#pragma once
#include <math.h>
#include <stdint.h>
#include "fastmath.h"
#include "hot.h"
#include "synth_osc.h"

namespace mt {

// State-variable filter (Simper, trapezoidal integration): stable under fast cutoff changes.
// set() at control rate (fastTan), process() per sample.
struct Svf {
  enum class Mode : uint8_t { Lp, Bp, Hp };

  // hz is clamped to 20 Hz .. 0.45 x the sample rate, q to >= 0.5.
  // Unchanged settings skip the fastTan (Synth sets every control tick).
  void set(Mode m, float hz, float q);
  void reset() { ic1_ = ic2_ = 0; }
  // Still ringing with no input: state above -80 dB of full scale (Synth keeps the voice for it).
  MT_INLINE bool ringing() const {
    constexpr float kOff = 1e-4f;
    return ic1_ > kOff || ic1_ < -kOff || ic2_ > kOff || ic2_ < -kOff;
  }
  // out[i] += process(in[i]), in = nullptr for silence (a ringing tail). The state stays in
  // registers over the block (process() reloads it through this after every out store).
  MT_HOT MT_INLINE void processAdd(const float* __restrict in, float* __restrict out, int n) {
    float ic1 = ic1_, ic2 = ic2_;
    const float a1 = a1_, a2 = a2_, a3 = a3_, k = k_;
    const Mode m = mode_;
    for (int i = 0; i < n; ++i) {
      const float v0 = in ? in[i] : 0.f;
      const float v3 = v0 - ic2;
      const float v1 = a1 * ic1 + a2 * v3;
      const float v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = 2.f * v1 - ic1;
      ic2 = 2.f * v2 - ic2;
      out[i] += m == Mode::Lp ? v2 : m == Mode::Bp ? v1 : v0 - k * v1 - v2;
    }
    ic1_ = ic1;
    ic2_ = ic2;
  }
  // A loop's copy of the state and coefficients (run(), bp() / hp() per sample, store()): the
  // mode picked by the caller, the state in registers. Same arithmetic as process(), bit for bit.
  struct Run {
    float ic1, ic2, a1, a2, a3, k;
    MT_INLINE void step(float v0, float& v1, float& v2) {
      const float v3 = v0 - ic2;
      v1 = a1 * ic1 + a2 * v3;
      v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = 2.f * v1 - ic1;
      ic2 = 2.f * v2 - ic2;
    }
    MT_INLINE float bp(float v0) {
      float v1, v2;
      step(v0, v1, v2);
      return v1;
    }
    MT_INLINE float hp(float v0) {
      float v1, v2;
      step(v0, v1, v2);
      return v0 - k * v1 - v2;
    }
  };
  MT_INLINE Run run() const { return {ic1_, ic2_, a1_, a2_, a3_, k_}; }
  MT_INLINE void store(const Run& r) {
    ic1_ = r.ic1;
    ic2_ = r.ic2;
  }
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
  float hz_ = -1, q_ = 0;  // last set() arguments, before clamping
};

}  // namespace mt
