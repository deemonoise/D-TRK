#pragma once
#include <stdint.h>

namespace mt {

// Groove templates (Pattern::groove): per step of a 16-step cycle, a timing shift in % of a step and
// a velocity in % of the step's. Groove 0 is OFF: the pattern's swing applies instead.
struct Groove {
  const char* name;  // up to 9 characters
  int8_t shift[16];  // -50..50 % of a step
  uint8_t vel[16];   // % of the step velocity
};

int grooveCount();
const Groove& grooveAt(int i);  // out of range = OFF

}  // namespace mt
