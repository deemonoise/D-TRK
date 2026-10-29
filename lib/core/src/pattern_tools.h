#pragma once
#include "edit_ops.h"
namespace mt {
enum class StepPart { All, Notes, Automation };
enum class ToolResult { Changed, Unchanged, Invalid, NoRoom, NoSlot };
enum class VelocityMode { Set, Add, Ramp, Humanize };
struct VelocitySpec {
  VelocityMode mode = VelocityMode::Set;
  int amount = 100, end = 127;
  uint32_t seed = 1;
};
// Scratch must be distinct; failures leave p untouched. Notes = note, velocity and KIT masks;
// Automation = supported parameter/LFO locks; every other FX remains at its destination.
ToolResult rotateSteps(Pattern& p, Pattern& scratch, const Sel& scope, int amount, StepPart part);
ToolResult pastePart(Pattern& p, Pattern& scratch, const Clipboard& clip, int track, int step, StepPart part);
// Copies immediately after scope, without growing or clipping. Clipboard is not involved.
ToolResult duplicateSteps(Pattern& p, const Sel& scope);
// All tracks; explicit polymeter lengths/speeds stay unchanged, inherited length doubles.
ToolResult doublePattern(Pattern& p);
// Only note hits. KIT velocity lives in note; lane masks and FX are preserved.
ToolResult changeVelocity(Pattern& p, const Sel& scope, VelocitySpec spec,
                          const uint8_t* defaultVelocity, const bool* drumTracks);
}
