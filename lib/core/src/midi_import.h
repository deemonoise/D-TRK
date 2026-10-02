#pragma once
#include <stdint.h>
#include "model.h"
#include "smf.h"

namespace mt {

enum class MonoMode : uint8_t { Highest, Lowest, First };

struct ImportMap {
  int8_t target[kSmfMaxSources];     // source -> track 0..7 or -1 (default -1)
  int8_t transpose[kSmfMaxSources];  // semitones, +-24 (default 0)
  uint16_t offsetBars = 0;           // skip N bars (4/4)
  Resolution quant = Resolution::Sixteenth;
  uint8_t patternLen = 16;    // length of written patterns (clamped kMinSteps..kMaxSteps)
  uint8_t firstPattern = 0;   // first pattern to write
  MonoMode mono = MonoMode::Highest;
  bool useSourceChannel = false;  // CHN fx when the source channel != track channel
  bool useTempo = true;
  bool keepMicrotiming = false;   // off-grid offset -> NDG

  ImportMap() {
    for (int i = 0; i < kSmfMaxSources; ++i) {
      target[i] = -1;
      transpose[i] = 0;
    }
  }
};

struct ImportResult {
  uint8_t patternsWritten;
  uint16_t bpm;           // project bpm after import
  uint32_t notesDropped;  // past P16, mono-reduced, out of 0..127 after transpose
};

// Writes notes (sorted by tick, as parseSmf returns them) into p. Patterns
// firstPattern..firstPattern+patternsWritten-1 get their target tracks cleared, length =
// patternLen and res = quant; other tracks and patterns are untouched. Notes of unmapped
// sources or before the offset are ignored (not counted as dropped); a note less than half a
// step before the offset rounds to step 0.
// Pattern count covers the last placed note and, once any note is placed, the trailing silence
// up to info.lastTick (rounded to the nearest step), clamped to the patterns left up to P16.
// Velocity equal to the track's defVel is stored as kVelDefault.
// Gate: the note length becomes the nearest GAT value (1..800 %); longer notes are clamped to
// 800 %. GAT is written only when it differs from the track's defGate by more than 10
// absolute percentage points (e.g. defGate 50 % -> 40..60 % gets no GAT).
ImportResult importSmf(const SmfInfo& info, const SmfNote* notes, uint32_t n, const ImportMap& m,
                       Project& p);

// Nearest GAT value (1..200, see gatePercent) for a note of `len` file ticks on a grid of
// q / kPpqn ticks per step. Ties pick the lower value.
uint8_t nearestGate(uint32_t len, int64_t q);

}  // namespace mt
