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

// The store's peak overview (/mt-store/<crc>.pk): min / max (>> 8) of every kPeakBlock frames.
constexpr uint32_t kPeakBlock = 256;
// Pairs of d (n frames, the last block may be shorter) into out; returns the pair count.
int peakOverview(const int16_t* d, uint32_t n, int8_t* out);

// A sample on the card: overview pairs and frames, read on demand.
struct PeakReader {
  virtual bool pairs(uint32_t first, uint32_t n, int8_t* out) = 0;    // pairs [first, first + n)
  virtual bool frames(uint32_t first, uint32_t n, int16_t* out) = 0;  // frames [first, first + n)
};
// wavePeaks for a sample behind r: a column of kPeakBlock frames or more takes the overview pairs of
// the blocks it touches, a narrower one reads its frames (exact). False on a read error.
bool wavePeaksRead(PeakReader& r, uint32_t frames, uint32_t col0, uint32_t span, uint32_t width, int cols,
                   int8_t* mn, int8_t* mx);

}  // namespace mt
