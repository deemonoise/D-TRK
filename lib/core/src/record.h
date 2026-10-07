#pragma once
#include <stdint.h>

namespace mt {

// Step a live-recorded note lands on: the heard step pos, or the next one past its half (phase
// 0..255 inside the step), wrapped to len.
inline int recordStepFor(int pos, uint8_t phase, int len) {
  if (len <= 0) return 0;
  return (phase < 128 ? pos : pos + 1) % len;
}

// Position at now inside a step that started at start and lasts len us: 0 .. 255.
inline uint8_t stepPhase256(uint64_t now, uint64_t start, uint32_t len) {
  if (now <= start || len == 0) return 0;
  const uint64_t ph = (now - start) * 256 / len;
  return static_cast<uint8_t>(ph > 255 ? 255 : ph);
}

}  // namespace mt
