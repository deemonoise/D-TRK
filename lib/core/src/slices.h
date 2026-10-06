#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Fraction of the sample (/0xFFFF) <-> frame, len = sample frames.
uint32_t fracToFrame(uint16_t f, uint32_t len);
uint16_t frameToFrac(uint32_t frame, uint32_t len);
// Sorted insert; index, or -1 when full or the position is taken.
int sliceInsert(Instrument& m, uint16_t pos);
void sliceRemove(Instrument& m, int i);  // out of range: no-op
void sliceClear(Instrument& m);
// Moves slice i, clamped strictly between its neighbours (0 / 0xFFFF at the ends); new position.
uint16_t sliceMove(Instrument& m, int i, int pos);
// Frames [from, to) of slice i in a sample of len frames; false if i >= sliceCount.
bool sliceRegion(const Instrument& m, int i, uint32_t len, uint32_t& from, uint32_t& to);
// chopN (clamped 2..kMaxSlices) equal slices of Start..End.
void chopEqual(Instrument& m);

}  // namespace mt
