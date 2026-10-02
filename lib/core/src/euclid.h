#pragma once
#include <stdint.h>
#include "model.h"
#include "scale.h"

namespace mt {

// Bjorklund distribution: hits <= steps <= 128 (both clamped). out[i] = true for a hit.
// rotation shifts right (negative = left), modulo steps.
void euclid(int hits, int steps, int rotation, bool* out);

enum class EuclidFill : uint8_t { Root, Up, Down, UpDown, Random, Count };

struct EuclidParams {
  uint8_t hits = 4, length = 16;  // length <= pattern length, repeats over the pattern
  int8_t rotation = 0;
  EuclidFill fill = EuclidFill::Root;
  uint8_t baseNote = 36;  // fill base
  uint8_t range = 1;      // octaves (1..4) for Up/Down/UpDown/Random
  uint8_t vel = 100;
  uint8_t accentVel = 127;
  uint8_t accentEvery = 0;  // 0 = none; N = hit 0, N, 2N... (counted over the whole pattern)
  uint32_t seed = 1;
  bool merge = false;  // true: only write into empty steps; false: replace the track (within pattern length)
};

// Writes track `track` of p; notes follow the scale root/scale.
void applyEuclid(Pattern& p, int track, const EuclidParams& e, uint8_t root, ScaleType scale);

}  // namespace mt
