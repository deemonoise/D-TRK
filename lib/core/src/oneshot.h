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
  void mix(int16_t* out, int n, float gain) { mixInto(out, n, gain, 1); }
  // Same into interleaved stereo lr[2 * n]: each sample to both L and R.
  void mixStereo(int16_t* lr, int n, float gain) { mixInto(lr, n, gain, 2); }

 private:
  // stride 1: mono, 2: interleaved L R (the sample goes to out[k] and out[k + 1]).
  void mixInto(int16_t* out, int n, float gain, int stride);

  const int16_t* d_ = nullptr;
  uint32_t frames_ = 0;
  uint64_t pos_ = 0, step_ = 0;  // 32.32 frames
};

}  // namespace mt
