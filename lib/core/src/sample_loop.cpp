#include "sample_loop.h"

namespace mt {
bool sampleLoopEdge(int64_t& pos, int64_t& step, int64_t lo, int64_t hi, uint8_t mode,
                    bool& looped) {
  if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
    const int64_t span = hi - lo;
    if (span <= 0) return false;
    if (step >= 0) {
      pos -= span;
      if (pos >= hi) pos = lo + (pos - lo) % span;
    } else {
      pos += span;
      if (pos < lo) pos = hi - 1 - (hi - 1 - pos) % span;
    }
  } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
    const int64_t top = hi - (int64_t(1) << 32);
    const int64_t length = top - lo;
    if (length <= 0) {
      pos = lo;
      if (step < 0) step = -step;
    } else {
      pos = step >= 0 ? 2 * top - pos : 2 * lo - pos;
      step = -step;
      if (pos < lo || pos > top) {
        const int64_t period = 2 * length;
        int64_t phase = (pos - lo) % period;
        if (phase < 0) phase += period;
        if (phase >= length) {
          pos = lo + period - phase;
          step = -step;
        } else {
          pos = lo + phase;
        }
      }
      // At a turning point the next sample moves into the loop, never repeats the end.
      if ((pos == lo && step < 0) || (pos == top && step > 0)) step = -step;
    }
  } else {
    return false;
  }
  looped = true;
  return true;
}
}  // namespace mt
