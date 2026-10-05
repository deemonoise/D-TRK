#pragma once
#include <stdint.h>
#include "synth_osc.h"

namespace mt {

// Plays a mono int16 buffer once at kSynthRate (rate <= kSynthRate: linear interpolation),
// added on top of the synth output. The buffer must stay valid until stop() or the end.
class OneShot {
 public:
  void start(const int16_t* d, uint32_t frames, uint32_t rate);
  void stop() { d_ = nullptr; }
  bool playing() const { return d_ != nullptr; }
  // Adds the next n samples times gain to out, saturating.
  void mix(int16_t* out, int n, float gain);

 private:
  const int16_t* d_ = nullptr;
  uint32_t frames_ = 0;
  uint64_t pos_ = 0, step_ = 0;  // 32.32 frames
};

}  // namespace mt
