#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

struct Onset {
  uint32_t pos;       // frame where the attack starts
  uint16_t strength;  // 1..1000, 1000 = the strongest found
};
constexpr int kMaxOnsets = 128;

// Transients of mono int16 data, ascending by pos; returns the count (<= max). Energy in windows
// of 256 frames, hop 128; onset = rise of log energy >= 3 dB above a -60 dBFS gate; peaks closer
// than 40 ms keep the stronger; position refined back to the first 32-frame block reaching half
// the peak level of the window. More than max candidates keep the strongest.
int detectOnsets(const int16_t* d, uint32_t frames, uint32_t rate, Onset* out, int max);
// Nearest onset to frame pos, -1 if n == 0.
int nearestOnset(const Onset* o, int n, uint32_t pos);
// First onset strictly after (dir > 0) or before (dir < 0) pos, -1 if none.
int stepOnset(const Onset* o, int n, uint32_t pos, int dir);
// TRANS chop of Start..End: a slice at Start, plus up to kMaxSlices - 1 strongest onsets inside
// with strength >= (100 - chopThresh) * 10, skipping those within 10 ms of Start.
void chopTransients(Instrument& m, const Onset* o, int n, uint32_t len, uint32_t rate);

}  // namespace mt
