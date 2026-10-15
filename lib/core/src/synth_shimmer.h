#pragma once
#include <stdint.h>
#include "synth_fxutil.h"

namespace mt {

// Delay-line pitch shifter: an int16 line (caller-owned, >= kLen samples, firmware: next to the
// reverb buffer), two read taps sweeping at speed `ratio` through a kWindow window, 180 deg apart,
// with complementary triangular gains (sum 1), then a one-pole LP (~6 kHz) so stacked octaves do not
// turn to hiss. Intervals: +12 (x2), +7 (x1.4983), +24 (x4). Without a buffer the output is silent.
class PitchShift {
 public:
  static constexpr uint32_t kLen = 2048;   // 64 ms, 4 KB
  static constexpr float kWindow = 1280.f;  // 40 ms crossfade window
  static constexpr float kScale = 8192.f;
  enum Interval : uint8_t { Oct = 0, Fifth = 1, TwoOct = 2, kIntervals };
  static float ratio(uint8_t interval);

  void setBuffer(int16_t* buf, uint32_t len);  // len < kLen = off. Clears.
  void clear();
  // Phase-continuous, no click. Per block: an unchanged interval returns inline.
  void setInterval(uint8_t interval) {
    if (interval != interval_) change(interval);
  }
  // out[i] = the shifted in[i] (overwrites). in and out may alias.
  void process(const float* in, float* out, int n);

 private:
  // The taps' delays in Q20 fixed point (samples x 2^20), in kLo .. kLo + kW: one integer step per
  // sample (no float -> int per read), the index and the fraction come from shifts and a mask.
  static constexpr int kFrac = 20;
  static constexpr int32_t kLo = 2 << kFrac;                                    // 2 samples
  static constexpr int32_t kW = static_cast<int32_t>(kWindow) << kFrac;          // the window
  int16_t* buf_ = nullptr;
  uint32_t len_ = 0, w_ = 0;
  int32_t dA_ = kLo + kW, dB_ = kLo + kW / 2;  // tap delays; B is half a window from A
  void change(uint8_t interval);
  int32_t step_ = 1 << kFrac;                  // (ratio - 1) samples per sample, Q20
  uint8_t interval_ = 0xFF;                    // step_ is for it (0xFF: the default octave)
  float lp_ = 0;
};

// Shimmer: the shifter in the reverb's feedback. The caller keeps the last block's reverb wet (mono,
// before the return level) and passes it as prevWet; rvbIn = send + softSat(shift(prevWet) x g),
// g = amount / 127 x kLoop x 2 sqrt(1 - fb^2) (fb = the reverb's feedback; max 0.5, ramped per
// block): the reverb's RMS gain is about 0.5 / sqrt(1 - fb^2) (4 combs x 0.25 in), so the loop
// gain stays near kLoop at every size. A one-pole HP (kHpHz) before the gain: DC and sub are not
// shifted, and the combs' DC gain (1 / (1 - fb)) would make them ring up. Each pass climbs one
// interval. amount 0 (settled):
// rvbIn = send (bit-exact). FREEZE: nothing is added (the frozen tail holds), the shifter keeps
// running so un-freezing does not click.
class Shimmer {
 public:
  void setBuffer(int16_t* buf, uint32_t len) { ps_.setBuffer(buf, len); }
  void setInterval(uint8_t interval) { ps_.setInterval(interval); }
  void clear() {
    ps_.clear();
    g_ = 0;
  }
  // send, prevWet: n samples; rvbIn: n samples out (may alias send).
  // size: the reverb's size (Project::rvbSize), for its feedback 0.70 + 0.28 x size / 127.
  void process(const float* send, const float* prevWet, float* rvbIn, int n, uint8_t amount, uint8_t size,
               bool freeze);
  bool active() const { return g_ != 0.f; }

 private:
  PitchShift ps_;
  float g_ = 0, hp_ = 0;
  void retune(uint8_t amount, uint8_t size);
  float gT_ = 0;                        // the loop gain for (amt_, size_)
  uint8_t amt_ = 0, size_ = 0xFF;       // 0xFF: not computed yet
  static constexpr int kMaxN = 128;

 public:
  static constexpr float kLoop = 1.0f;
  static constexpr float kHpHz = 200.f;
};

}  // namespace mt
