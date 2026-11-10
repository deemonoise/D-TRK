#pragma once
#include "onset.h"
#include "hot.h"
// Offline sample processing stays in flash: IRAM is reserved for live audio and its state.
namespace mt {
// Periodicity confidence is a heuristic 0..1000, not a probability. bpm10=0 means unknown.
struct TempoEstimate {
  uint16_t bpm10 = 0, confidence = 0, halfBpm10 = 0, doubleBpm10 = 0;
  bool ambiguous = false;
};
// Bounded analysis of <=128 chronological onset candidates, relative to the selection.
// Main tempo 40..240 BPM; half/double alternatives 20..480 BPM. Never changes a project.
// Fewer than four usable attacks or less than one second between them returns unknown.
TempoEstimate estimateTempo(const Onset* onsets, int count, uint32_t rate, uint32_t frames);
}  // namespace mt
