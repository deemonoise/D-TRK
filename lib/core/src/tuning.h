#pragma once
#include "hot.h"
#include "model.h"

namespace mt {

// A project tuning as a key -> pitch map (linear keyboard mapping): key root + j plays degree j mod N of
// period floor(j / N); the root keeps its 12-TET pitch. Pitches are fractional MIDI notes (semitones).
// build() is cold (a table change); map() is a lookup + lerp, for control updates.
class TuneMap {
 public:
  // Not tuningValid (12-TET, a broken table): off, map(x) == x.
  void build(const TuningTable& t, uint8_t scaleRoot);
  bool on() const { return on_; }
  // Fractional key x (glides between keys interpolate between their tuned pitches).
  MT_INLINE float map(float x) const {
    if (!on_) return x;
    int i = static_cast<int>(x);
    if (static_cast<float>(i) > x) --i;  // floor
    const float f = x - static_cast<float>(i);
    if (i >= 0 && i < kKeys - 1) return x + off_[i] + f * (off_[i + 1] - off_[i]);
    const float a = offAt(i);
    return x + a + f * (offAt(i + 1) - a);
  }

 private:
  static constexpr int kKeys = 128;
  float offAt(int k) const;  // any key: off_ shifted by whole periods
  bool on_ = false;
  int n_ = 12;
  float perStep_ = 0;  // period in semitones - n_: what one period shift adds to the offset
  float off_[kKeys] = {};  // tuned pitch - key
};

}  // namespace mt
