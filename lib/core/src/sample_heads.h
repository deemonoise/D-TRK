#pragma once
#include <stdint.h>
#include "model.h"
#include "sample_source.h"

namespace mt {

// Heads: runs of a store sample kept in the flash bank (entry "h" + crc), so notes start without
// waiting for the card. The entry is an index (kHeadIndexFrames int16: count, 0, then frame, len,
// offset as uint32 per head) followed by the heads' frames.
constexpr uint32_t kHeadStartMs = 100;  // a note start, a forward loop's start
constexpr uint32_t kHeadSliceMs = 25;   // a slice start
constexpr int kHeadIndexFrames = 256;   // 512 bytes

struct HeadPoint {
  uint32_t frame = 0, len = 0;
};

// Runs of crc (frames long at rate) worth a head: per instrument of m playing it, its start and the
// start of a forward loop (played backwards: the run ending there), then its slice starts; frame 0
// for KIT sampler lanes. Taken in that order up to max, then sorted and merged. Returns the count.
int headPoints(const SynthModel& m, uint32_t crc, uint32_t frames, uint32_t rate, HeadPoint* out, int max);
void headKey(uint32_t crc, char out[kSampleNameMax + 1]);  // "h" + 8 hex
// The index for heads p; returns the entry's length in frames (index included).
uint32_t headLayout(const HeadPoint* p, int n, int16_t index[kHeadIndexFrames]);
// Heads of an entry (pointers into d); false if the index does not fit the entry.
bool headParse(const int16_t* d, uint32_t entryFrames, SampleHead* out, int& n);
// The entry holds exactly the runs p.
bool headSame(const int16_t* d, uint32_t entryFrames, const HeadPoint* p, int n);

}  // namespace mt
