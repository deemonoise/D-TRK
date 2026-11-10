#pragma once
#include "record.h"
namespace ui {
// A KIT step has one shared NDG. Later lanes join its first hit instead of moving
// every previously recorded lane to the timing of the latest button press. A re-hit of an
// existing lane records fresh timing, as an ordinary overdub did before chord recording.
inline bool recordGridTiming(mt::Step& output, const mt::Step& before, bool kit, bool clear, uint8_t phase) {
  const bool addsLane = (output.vel & ~before.vel) != 0;
  if (clear || (kit && before.hasNote() && before.vel && addsLane)) return true;
  return mt::recordTiming(output, phase);
}
}  // namespace ui
