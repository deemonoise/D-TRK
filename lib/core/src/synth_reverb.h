#pragma once
#include <stdint.h>
#include "hot.h"

namespace mt {

// Reverb, Freeverb-lite: 4 parallel damped feedback combs, then 2 series allpasses (mono). Stereo:
// the first allpass feeds two second ones, L = the mono one, R = one 23 samples longer (Freeverb
// spread). The float buffer (kBufLen) is owned by the caller (firmware: PSRAM); without one the reverb is silent.
class Reverb {
 public:
  static constexpr int kCombs = 4, kAllpasses = 2;
  static constexpr int kCombLen[kCombs] = {1116, 1188, 1277, 1356};
  static constexpr int kApLen[kAllpasses] = {556, 441};
  static constexpr int kApRLen = 441 + 23;  // R's second allpass
  static constexpr int kBufLen = 1116 + 1188 + 1277 + 1356 + 556 + 441 + kApRLen;  // 6398 floats

  // buf: len floats, nullptr or too short = off. Clears it.
  void setBuffer(float* buf, int len);
  void clear();
  // mixL[i] += the reverb of in[] x level / 127 x 0.5 (the mono path), mixR[i] += its spread side.
  // size: comb read delays and feedback, damp: high loss in the combs, level: return; 0..127
  // (Project::rvb*). Changes ramp for 5 ms; level 0 then leaves the mix untouched and clears
  // the tail. Comb rings have fixed capacity; a size change crossfades two read taps, so old
  // samples are always overwritten and expanding the size cannot expose stale ring contents.
  // Silent input with both tails below -100 dB for
  // kQuietIdle samples: idle (no work, mix untouched) until the input is non-zero.
  // freeze (PERF FREEZE): the combs loop the tail without loss (feedback 1, no damping) and ignore
  // the input; it never goes idle meanwhile. Off again, the tail decays as usual.
  // wetOut (optional, n floats): the mono path before the level (the shimmer's feedback source);
  // left untouched when the reverb does not run (the caller zeroes it).
  void process(const float* in, float* mixL, float* mixR, int n, uint8_t size, uint8_t damp, uint8_t level,
               bool freeze, float* wetOut = nullptr);

  bool muted() const { return wet_.value == 0.f && wet_.target == 0.f; }

 private:
  struct Ramp {
    float value = 0, target = 0, step = 0;
    int left = 0;
    void snap(float x) { value = target = x; step = 0; left = 0; }
    void set(float x);
    MT_INLINE float next() {
      if (left > 0) { value += step; if (--left == 0) value = target; }
      return value;
    }
  };
  void setSize(uint8_t size, bool snap);
  void clearTail();
  float* buf_ = nullptr;
  float* comb_[kCombs] = {};
  float* ap_[kAllpasses] = {};
  float* apR_ = nullptr;  // R's second allpass
  int combIdx_[kCombs] = {}, apIdx_[kAllpasses] = {}, apRIdx_ = 0;
  int combLen_[kCombs] = {}, nextLen_[kCombs] = {}, combCapacity_[kCombs] = {};
  uint8_t size_ = 255, nextSize_ = 255;
  int sizeLeft_ = 0;
  float sizeMix_ = 0;
  Ramp wet_, feedback_, damping_;
  bool initialized_ = false;
  float store_[kCombs] = {};  // damping state
  bool idle_ = true;          // the lines hold (next to) nothing and no input came since
  int quiet_ = 0;             // samples of silent input and inaudible output in a row
  static constexpr int kQuietIdle = 4096;  // > the longest path through the lines (1356 + 556 + 464)
};

}  // namespace mt
