#pragma once
#include <math.h>
#include <stdint.h>

namespace mt {

// tanh waveshaper: y = tanh(g x) / tanh(g), g = 1..8 from drive 1..127; drive 0 = bypass (bit-exact).
// A 257-point tanh table over [-8, 8] with linear interpolation: no transcendental per sample.
class Drive {
 public:
  static constexpr int kTab = 257;
  void set(uint8_t drive) {  // per control update
    if (drive == drive_) return;
    drive_ = drive;
    g_ = drive ? 1.f + (drive > 127 ? 127 : drive) * (7.f / 127.f) : 0.f;
    norm_ = drive ? 1.f / tanhf(g_) : 1.f;
  }
  bool on() const { return g_ != 0.f; }
  float process(float x) const {
    if (!on()) return x;
    float u = x * g_;
    u = u < -8.f ? -8.f : (u > 8.f ? 8.f : u);
    const float p = (u + 8.f) * 16.f;  // table position 0..256
    int i = static_cast<int>(p);
    if (i > kTab - 2) i = kTab - 2;
    const float f = p - i;
    const float* t = table();
    return (t[i] + (t[i + 1] - t[i]) * f) * norm_;
  }
  // x[i] = process(x[i]) over a block, drive on.
  void processBlock(float* __restrict x, int n) const {
    const float* __restrict t = table();
    const float g = g_, norm = norm_;
    for (int i = 0; i < n; ++i) {
      float u = x[i] * g;
      u = u < -8.f ? -8.f : (u > 8.f ? 8.f : u);
      const float p = (u + 8.f) * 16.f;
      int k = static_cast<int>(p);
      if (k > kTab - 2) k = kTab - 2;
      const float f = p - k;
      x[i] = (t[k] + (t[k + 1] - t[k]) * f) * norm;
    }
  }
  // Builds the table (257 tanhf, once): call outside the audio path to keep the first block clean.
  static void init() { table(); }

 private:
  static const float* table() {
    static float t[kTab];
    static bool ready = false;
    if (!ready) {
      for (int i = 0; i < kTab; ++i) t[i] = tanhf(-8.f + i * (1.f / 16.f));
      ready = true;
    }
    return t;
  }
  uint8_t drive_ = 0;
  float g_ = 0, norm_ = 1;
};

}  // namespace mt
