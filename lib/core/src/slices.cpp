#include "slices.h"

namespace mt {

uint32_t fracToFrame(uint16_t f, uint32_t len) {
  return static_cast<uint32_t>(static_cast<uint64_t>(f) * len / 0xFFFF);
}

uint16_t frameToFrac(uint32_t frame, uint32_t len) {
  if (!len) return 0;
  const uint64_t f = (static_cast<uint64_t>(frame) * 0xFFFF + len / 2) / len;
  return static_cast<uint16_t>(f > 0xFFFF ? 0xFFFF : f);
}

int sliceInsert(Instrument& m, uint16_t pos) {
  const int n = m.sliceCount;
  if (n >= kMaxSlices) return -1;
  int i = 0;
  while (i < n && m.slices[i] < pos) ++i;
  if (i < n && m.slices[i] == pos) return -1;
  for (int k = n; k > i; --k) m.slices[k] = m.slices[k - 1];
  m.slices[i] = pos;
  m.sliceCount = static_cast<uint8_t>(n + 1);
  return i;
}

void sliceRemove(Instrument& m, int i) {
  const int n = m.sliceCount < kMaxSlices ? m.sliceCount : kMaxSlices;
  if (i < 0 || i >= n) return;
  for (int k = i; k + 1 < n; ++k) m.slices[k] = m.slices[k + 1];
  m.slices[n - 1] = 0;
  m.sliceCount = static_cast<uint8_t>(n - 1);
}

void sliceClear(Instrument& m) {
  for (int k = 0; k < kMaxSlices; ++k) m.slices[k] = 0;
  m.sliceCount = 0;
}

uint16_t sliceMove(Instrument& m, int i, int pos) {
  const int n = m.sliceCount < kMaxSlices ? m.sliceCount : kMaxSlices;
  if (i < 0 || i >= n) return 0;
  const int lo = i > 0 ? m.slices[i - 1] + 1 : 0;
  int hi = i + 1 < n ? m.slices[i + 1] - 1 : 0xFFFF;
  if (hi < lo) hi = lo;  // duplicates (corrupt input): stay at the left neighbour
  if (pos < lo) pos = lo;
  if (pos > hi) pos = hi;
  m.slices[i] = static_cast<uint16_t>(pos);
  return m.slices[i];
}

bool sliceRegion(const Instrument& m, int i, uint32_t len, uint32_t& from, uint32_t& to) {
  const int n = m.sliceCount < kMaxSlices ? m.sliceCount : kMaxSlices;
  if (i < 0 || i >= n || !len) return false;
  from = fracToFrame(m.slices[i], len);
  // End moved left of the last slice: that slice plays to the end of the sample.
  to = i + 1 < n ? fracToFrame(m.slices[i + 1], len) : (m.end > m.slices[i] ? fracToFrame(m.end, len) : len);
  if (from >= len) from = len - 1;
  if (to > len) to = len;
  if (to <= from) to = from + 1;
  return true;
}

void chopEqual(Instrument& m) {
  int n = m.chopN;
  if (n < 2) n = 2;
  if (n > kMaxSlices) n = kMaxSlices;
  const uint32_t a = m.start, b = m.end > m.start ? m.end : m.start;
  sliceClear(m);
  // Strictly ascending: a region shorter than n units gets fewer slices (at least one at Start).
  int c = 0;
  for (int k = 0; k < n; ++k) {
    const uint16_t v = static_cast<uint16_t>(a + (b - a) * k / n);
    if (c == 0 || v > m.slices[c - 1]) m.slices[c++] = v;
  }
  m.sliceCount = static_cast<uint8_t>(c);
}

}  // namespace mt
