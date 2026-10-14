#pragma once
#include <stdint.h>

namespace mt {

// FILE -> SYSTEM "Samples": where samples play from (Status::psram 0 off, 1 on, 2 failed; fill %) and the
// voices that ran out of data since the synth board came up.
void streamLine(char* out, int n, uint8_t psram, uint8_t fill, uint32_t underruns);

}  // namespace mt
