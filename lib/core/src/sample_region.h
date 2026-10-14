#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Where a SAMPLE note plays: the region, the loop and the start position. Shared by the voice start
// (Synth::startSample) and the prefetch hint (Synth::prefetch), so both agree on the first frame.
struct SampleRegion {
  uint32_t from = 0, to = 1;        // played region [from, to)
  uint32_t loopLo = 0, loopHi = 1;  // loop (LoopMode::Off: the region)
  int64_t pos = 0;                  // start position, 32.32
  int8_t dir = 1;                   // -1 = reverse
  uint8_t loopMode = 0;             // LoopMode
};

// slice < 0 (or past sliceCount): the instrument's start / end / loop. ofs: OFS, /256 of the region.
SampleRegion sampleRegion(const Instrument& m, int slice, uint32_t len, uint8_t ofs);

}  // namespace mt
