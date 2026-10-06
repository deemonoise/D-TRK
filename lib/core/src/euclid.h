#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Bjorklund distribution: hits <= steps <= 128 (both clamped). out[i] = true for a hit.
// rotation shifts right (negative = left), modulo steps.
void euclid(int hits, int steps, int rotation, bool* out);

}  // namespace mt
