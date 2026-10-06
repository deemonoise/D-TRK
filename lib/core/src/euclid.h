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
  // Drum track: the lane written (its bit in Step::vel), -1 = melodic. Fill, base, range and seed
  // are unused; merge keeps the lane's other hits, replace clears them first; other lanes stay.
  int8_t lane = -1;
};

// Writes track `track` of p; notes follow the scale root/scale. Drum track (e.lane >= 0): sets the
// lane's bit on the hits (an empty step becomes a note step at the track's velocity, 0); an accent
// sets the step velocity to accentVel where it is still the track's.
void applyEuclid(Pattern& p, int track, const EuclidParams& e, uint8_t root, ScaleType scale);

}  // namespace mt
