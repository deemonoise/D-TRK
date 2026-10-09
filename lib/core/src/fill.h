#pragma once
#include <stdint.h>
#include "edit_ops.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Which steps of the range get a value. Positions count from the first step of the range.
enum class FillWhere : uint8_t { Every, Euclid, Random, Count };
// The field written.
enum class FillTarget : uint8_t { Note, Vel, Fx, Count };
// The value: one for every step, a ramp from -> to over the hits, or random in from..to.
enum class FillValue : uint8_t { Const, Ramp, Random, Count };
// Overwrite: every hit. Empty: only where the field is empty (no note / default velocity / free
// slot). Notes: only steps holding a note.
enum class FillMode : uint8_t { Overwrite, Empty, Notes, Count };

struct FillSpec {
  FillWhere where = FillWhere::Every;
  uint8_t every = 4, offset = 0;                // Every: offset, offset + every, ...
  uint8_t hits = 4, length = 16;                // Euclid: hits of length, repeated over the range
  int8_t rotation = 0;
  uint8_t density = 50;                         // Random: % of the steps
  FillTarget target = FillTarget::Note;
  uint8_t slot = 0;                             // Fx: the slot written
  Fx cmd = Fx::None;                            // Fx: the command (None = the slot is cleared)
  FillValue value = FillValue::Const;
  // Note: MIDI notes (ramp / random stay in the scale); Vel: 1..127; Fx: raw values of cmd.
  uint8_t from = 60, to = 72;
  FillMode mode = FillMode::Overwrite;
  uint8_t lane = 0;                             // drum track, Note: the lane set on the hits
  uint32_t seed = 1;
  bool arp = false;                             // FILL dialog shows the arp generator (ArpSpec) instead
};

// Hit mask of the spec over n positions (n <= kMaxSteps). Random draws from rng state `seed`.
void fillHits(const FillSpec& f, int n, bool* out);

// Fills sel (steps clamped to the pattern length). drumTracks[t] (optional): track t is a drum
// track; there Note sets the lane bit (an empty step becomes a hit at the track velocity) and Vel
// writes the step velocity (its note field). Vel never creates a note. Non-hits stay as they are.
void applyFill(Pattern& p, const Sel& sel, const FillSpec& f, uint8_t root, ScaleType scale,
               const bool* drumTracks = nullptr);

}  // namespace mt
