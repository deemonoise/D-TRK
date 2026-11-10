#pragma once
#include "hot.h"
namespace mt {
// Unity below 0.8 FS. A C1 quadratic knee reaches 1 FS at input 1.2; odd, monotonic, bounded.
// The historical small-signal +3.52 dB belongs to MAIN's linear gain, not to this protection.
MT_INLINE float masterLimit(float x) {
  const float a = x < 0 ? -x : x;
  if (a <= .8f) return x;
  const float t = (a - .8f) * 2.5f;
  const float y = t >= 1.f ? 1.f : .8f + .4f * t * (1.f - .5f * t);
  return x < 0 ? -y : y;
}
}  // namespace mt
