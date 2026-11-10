#pragma once
#include <stdint.h>
#include "hot.h"
#include "model.h"

namespace mt {
// The reverse initial suffix is ordinary file data until the first wrap. Thereafter the
// upper interpolation neighbour of a forward loop is its first frame in either direction.
MT_INLINE uint32_t sampleLoopNext(uint32_t frame, uint32_t last, uint32_t lo, uint32_t hi,
                                  uint8_t mode, bool looped, int64_t step) {
  if (frame + 1 == hi && (looped || step >= 0)) {
    if (mode == static_cast<uint8_t>(LoopMode::Forward)) return lo;
    if (mode == static_cast<uint8_t>(LoopMode::PingPong)) return frame;
  }
  return frame < last ? frame + 1 : last;
}

MT_INLINE bool sampleLoopPast(int64_t pos, int64_t step, int64_t lo, int64_t hi, uint8_t mode) {
  if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
    const int64_t top = hi - (int64_t(1) << 32);
    return step >= 0 ? pos >= top : pos <= lo;
  }
  return step >= 0 ? pos >= hi : pos < lo;
}
// Rare loop crossing, shared by RAM and streamed playback. Returns false for a one-shot end.
MT_HOT bool sampleLoopEdge(int64_t& pos, int64_t& step, int64_t lo, int64_t hi, uint8_t mode,
                           bool& looped);
}  // namespace mt
