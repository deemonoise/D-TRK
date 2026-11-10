#pragma once
#include <stdint.h>
#include "hot.h"

namespace mt {
// Allocation-free, loop-side preview. Storage belongs to preview_stream and is shared with WAV IO.
class WtAudition {
 public:
  void bind(int16_t* active, int16_t* stage) { wave_ = active; stage_ = stage; }
  MT_CONTROL void stop();
  // A first half invalidates any previous upload and stops its sound. Half 1 only commits its token.
  MT_CONTROL bool part(uint16_t token, unsigned half, const int16_t* pts, uint32_t now, float* scratch);
  MT_CONTROL void expire(uint32_t now);
  MT_CONTROL unsigned render(int16_t* out, unsigned n);
  bool playing() const { return remaining_ != 0; }
  bool staging() const { return staged_; }
 private:
  int16_t* wave_ = nullptr;
  int16_t* stage_ = nullptr;
  uint16_t token_ = 0;
  uint32_t at_ = 0, remaining_ = 0, phase_ = 0, produced_ = 0;
  bool staged_ = false;
};
// Keep harmonics 1..32 (safe well below C4 Nyquist); drop DC, preserve relative waveform level.
MT_CONTROL void wtAuditionPrepare(const int16_t* in, int16_t* out, float* scratch);
}
