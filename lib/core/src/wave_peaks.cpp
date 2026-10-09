#include "wave_peaks.h"

namespace mt {

void wavePeaks(const int16_t* d, uint32_t frames, uint32_t col0, uint32_t span, uint32_t width, int cols, int8_t* mn,
               int8_t* mx) {
  if (!width) width = 1;
  auto gridFrame = [&](uint64_t g) { return static_cast<uint32_t>(g * span / width); };
  for (int x = 0; x < cols; ++x) {
    const uint32_t a = gridFrame(static_cast<uint64_t>(col0) + x);
    uint32_t b = gridFrame(static_cast<uint64_t>(col0) + x + 1);
    if (b <= a) b = a + 1;
    if (!d || a >= frames) {
      mn[x] = 1;
      mx[x] = 0;
      continue;
    }
    const uint32_t stride = (b - a) > kPeakProbe ? (b - a) / kPeakProbe : 1;
    int lo = 32767, hi = -32768;
    for (uint32_t k = a; k < b && k < frames; k += stride) {
      const int v = d[k];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    mn[x] = static_cast<int8_t>(lo >> 8);
    mx[x] = static_cast<int8_t>(hi >> 8);
  }
}

}  // namespace mt
