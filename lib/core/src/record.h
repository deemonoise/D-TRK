#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {
struct RecordMode { bool notes, motion; };
inline RecordMode recordModeToggle(RecordMode current, bool motionOnly) {
  const bool selected = motionOnly ? (!current.notes && current.motion) : (current.notes && current.motion);
  return selected ? RecordMode{false, false} : RecordMode{!motionOnly, true};
}


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

// Signed percent offset from the nearest step of a recorded note.
inline int8_t recordNudge(uint8_t phase) {
  const int offset = phase < 128 ? phase : static_cast<int>(phase) - 256;
  return static_cast<int8_t>(offset * 100 / 256);
}

inline bool recordTiming(Step& step, uint8_t phase) {
  const int8_t value = recordNudge(phase);
  int slot = -1;
  for (int i = 0; i < kFxSlots; ++i) if (step.fx[i].cmd == Fx::NDG) { slot = i; break; }
  if (slot < 0) for (int i = 0; i < kFxSlots; ++i) if (step.fx[i].cmd == Fx::None) { slot = i; break; }
  if (value && slot < 0) return false;
  for (auto& f : step.fx) if (f.cmd == Fx::NDG) f = FxSlot{};
  if (value) step.fx[slot] = {Fx::NDG, static_cast<uint8_t>(value)};
  return true;
}

}  // namespace mt
