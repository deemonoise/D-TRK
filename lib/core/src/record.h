#pragma once
#include <stdint.h>
#include "model.h"
#include "roll.h"

namespace mt {

// Step a live-recorded note lands on: the heard step pos, or the next one past its half (phase
// 0..255 inside the step), wrapped to len.
inline int recordStepFor(int pos, uint8_t phase, int len) {
  if (len <= 0) return 0;
  return (phase < 128 ? pos : pos + 1) % len;
}

// Encode a held duration as GAT (1..800% of a track step), rounded to the closest value.
inline uint8_t recordGate(uint64_t heldUs, uint32_t stepUs) {
  if (!stepUs) return 1;
  const uint64_t pct = (heldUs * 100 + stepUs / 2) / stepUs;
  if (pct <= 100) return static_cast<uint8_t>(pct < 1 ? 1 : pct);
  if (pct >= 800) return 200;
  return static_cast<uint8_t>(100 + (pct - 100 + 3) / 7);
}

// A TIE survives empty steps. End it with an OFF note at a boundary, or OFF ticks
// into an empty step. Preflight the entire path so an occupied step is never partly erased.
enum class RecordHold { Ok, NoSlot, Occupied, Clipped };
inline RecordHold recordHold(Step* steps, int len, int start, uint64_t heldUs,
                             uint32_t stepUs, uint16_t tps, uint8_t defGate) {
  if (!steps || len <= 0 || start < 0 || start >= len || !stepUs || !tps)
    return RecordHold::Occupied;
  if (heldUs <= static_cast<uint64_t>(stepUs) * 8)
    return rollSetGate(steps[start], recordGate(heldUs, stepUs), defGate) == RollLen::NoSlot
               ? RecordHold::NoSlot : RecordHold::Ok;
  // A repeating pattern cannot store multiple different passes through the same step.
  const uint64_t limit = static_cast<uint64_t>(len) * stepUs;
  const bool clipped = heldUs >= limit;
  if (clipped) heldUs = limit - 1;
  const int span = static_cast<int>(heldUs / stepUs);
  for (int i = 1; i <= span; ++i)
    if (!steps[(start + i) % len].isEmpty()) return RecordHold::Occupied;
  Step first = steps[start];
  FxSlot* slot = nullptr;
  for (auto& f : first.fx) {
    if (f.cmd == Fx::GAT || f.cmd == Fx::TIE) f = FxSlot();
    // These override a tie; do not silently change their musical meaning.
    if (f.cmd == Fx::OFF || f.cmd == Fx::ARS || f.cmd == Fx::CHD || f.cmd == Fx::NRN)
      return RecordHold::Occupied;
    if (f.cmd == Fx::None && !slot) slot = &f;
  }
  if (!slot) return RecordHold::NoSlot;
  *slot = {Fx::TIE, 0};
  Step last;
  // Round down to stay inside this step (including a release just before loop end).
  const uint16_t tick = static_cast<uint16_t>((heldUs % stepUs) * tps / stepUs);
  if (!tick) last.note = kNoteOff;
  else if (tick <= 255) last.fx[0] = {Fx::OFF, static_cast<uint8_t>(tick)};
  else {
    // Quarter resolution at 1/4 speed has 384 ticks per step, beyond OFF's byte.
    // Same-pitch continuation consumes the tie without a new attack, then its gate releases it.
    last.note = first.note;
    last.vel = first.vel;
    last.fx[0] = {Fx::GAT, recordGate(heldUs % stepUs, stepUs)};
  }
  steps[start] = first;
  steps[(start + span) % len] = last;
  return clipped ? RecordHold::Clipped : RecordHold::Ok;
}

// Position at now inside a step that started at start and lasts len us: 0 .. 255.
inline uint8_t stepPhase256(uint64_t now, uint64_t start, uint32_t len) {
  if (now <= start || len == 0) return 0;
  const uint64_t ph = (now - start) * 256 / len;
  return static_cast<uint8_t>(ph > 255 ? 255 : ph);
}

}  // namespace mt
