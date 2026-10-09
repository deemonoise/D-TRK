#pragma once
#include <stdint.h>

namespace mt {

// Long columns read at most this many evenly spaced frames (flash reads are not free).
constexpr uint32_t kPeakProbe = 256;

// Min / max (>> 8) of mono int16 data d (frames long) per column of a zoom grid: column c covers
// frames [g * span / width, (g + 1) * span / width) with g = col0 + c (at least one frame). A column
// starting at or past the end gets min > max (1 / 0): no data.
void wavePeaks(const int16_t* d, uint32_t frames, uint32_t col0, uint32_t span, uint32_t width, int cols, int8_t* mn,
               int8_t* mx);

}  // namespace mt
