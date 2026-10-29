#pragma once
#include "edit_ops.h"
namespace mt {
struct QuantizeSpec { Resolution grid = Resolution::Sixteenth; uint8_t strength = 100; };
enum class QuantizeResult { Ok, Unchanged, Collision, NoSlot, Invalid };
// scratch must be a distinct Pattern. Atomic: p is unchanged on any failure.
QuantizeResult quantizeNotes(Pattern& p, Pattern& scratch, const Sel& sel, QuantizeSpec spec);
void clearNotesSel(Pattern& p, const Sel& sel);
void clearAutomationSel(Pattern& p, const Sel& sel);
}
