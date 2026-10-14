#include "sample_region.h"
#include "slices.h"

namespace mt {

SampleRegion sampleRegion(const Instrument& m, int slice, uint32_t len, uint8_t ofs) {
  SampleRegion g;
  uint32_t from, to, lf;
  // A slice plays [from, to) of its own once: the loop is ignored.
  const bool sliced = slice >= 0 && sliceRegion(m, slice, len, from, to);
  if (!sliced) {
    from = static_cast<uint32_t>(static_cast<uint64_t>(m.start) * len / 0xFFFF);
    to = static_cast<uint32_t>(static_cast<uint64_t>(m.end) * len / 0xFFFF);
    if (from >= len) from = len - 1;
    if (to > len) to = len;
    if (to <= from) to = from + 1;
  }
  const uint32_t span = to - from;
  if (sliced) {
    lf = from;
  } else {
    lf = from + static_cast<uint32_t>(static_cast<uint64_t>(m.loopStart) * span / 0xFFFF);
    if (lf >= to) lf = to - 1;
  }
  g.from = from;
  g.to = to;
  g.loopMode = sliced ? static_cast<uint8_t>(LoopMode::Off)
                      : (m.loop < static_cast<uint8_t>(LoopMode::Count) ? m.loop : 0);
  const uint32_t o = static_cast<uint32_t>(static_cast<uint64_t>(ofs) * span / 256);
  if (m.reverse) {
    // Reverse plays the region mirrored: the loop is [from, to - (lf - from)) walked downwards.
    g.dir = -1;
    g.loopLo = from;
    g.loopHi = to - (lf - from);
    g.pos = static_cast<int64_t>(to - 1 - o) << 32;
  } else {
    g.dir = 1;
    g.loopLo = lf;
    g.loopHi = to;
    g.pos = static_cast<int64_t>(from + o) << 32;
  }
  if (g.loopMode == static_cast<uint8_t>(LoopMode::Off)) {
    g.loopLo = from;
    g.loopHi = to;
  }
  return g;
}

}  // namespace mt
