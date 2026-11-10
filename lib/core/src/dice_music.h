#pragma once
#include "model.h"

namespace mt { namespace dice_detail {
// Deterministic per-bar foreground/support budget. Kept source events retain their articulation;
// a tie whose terminating attack is deleted receives a bounded gate instead.
void limitAttacks(Step* row, int len, Resolution resolution, int perBar, bool support);
// Arrangement edits retain the source events and repair only affected articulation.
void thinPhrase(Step* row, int len, int budget);
// source stores retained original events; kNoteOff tags deleted original onsets, empty permits Busy additions.
void retainAttacks(Step* row, int len, const Step* source);
void endPhrase(Step* row, int len, int cutoff);
}}  // namespace mt::dice_detail
