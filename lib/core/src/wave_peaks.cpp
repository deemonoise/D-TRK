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

int peakOverview(const int16_t* d, uint32_t n, int8_t* out) {
  int k = 0;
  for (uint32_t a = 0; a < n; a += kPeakBlock, ++k) {
    const uint32_t b = a + kPeakBlock < n ? a + kPeakBlock : n;
    int lo = 32767, hi = -32768;
    for (uint32_t i = a; i < b; ++i) {
      lo = d[i] < lo ? d[i] : lo;
      hi = d[i] > hi ? d[i] : hi;
    }
    out[2 * k] = static_cast<int8_t>(lo >> 8);
    out[2 * k + 1] = static_cast<int8_t>(hi >> 8);
  }
  return k;
}

// min / max of >> 8 equals >> 8 of min / max (the shift is monotonic): merging block pairs is exact.
bool wavePeaksRead(PeakReader& r, uint32_t frames, uint32_t col0, uint32_t span, uint32_t width, int cols,
                   int8_t* mn, int8_t* mx) {
  if (!width) width = 1;
  auto gridFrame = [&](uint64_t g) { return static_cast<uint32_t>(g * span / width); };
  int8_t pk[2 * 128];
  int16_t fr[kPeakBlock];
  for (int x = 0; x < cols; ++x) {
    const uint32_t a = gridFrame(static_cast<uint64_t>(col0) + x);
    uint32_t b = gridFrame(static_cast<uint64_t>(col0) + x + 1);
    if (b <= a) b = a + 1;
    if (a >= frames) {
      mn[x] = 1;
      mx[x] = 0;
      continue;
    }
    if (b > frames) b = frames;
    int lo = 127, hi = -128;
    if (b - a >= kPeakBlock) {
      const uint32_t p0 = a / kPeakBlock, p1 = (b + kPeakBlock - 1) / kPeakBlock;
      for (uint32_t p = p0; p < p1; p += 128) {
        const uint32_t n = p1 - p < 128 ? p1 - p : 128;
        if (!r.pairs(p, n, pk)) return false;
        for (uint32_t i = 0; i < n; ++i) {
          lo = pk[2 * i] < lo ? pk[2 * i] : lo;
          hi = pk[2 * i + 1] > hi ? pk[2 * i + 1] : hi;
        }
      }
    } else {
      // Narrow: every frame, like wavePeaks with stride 1.
      if (!r.frames(a, b - a, fr)) return false;
      int l = 32767, h = -32768;
      for (uint32_t i = 0; i < b - a; ++i) {
        l = fr[i] < l ? fr[i] : l;
        h = fr[i] > h ? fr[i] : h;
      }
      lo = l >> 8;
      hi = h >> 8;
    }
    mn[x] = static_cast<int8_t>(lo);
    mx[x] = static_cast<int8_t>(hi);
  }
  return true;
}

}  // namespace mt
