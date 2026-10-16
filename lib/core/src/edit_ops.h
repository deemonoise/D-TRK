#pragma once
#include <stdint.h>
#include "model.h"
#include "scale.h"
#include "shift_gen.h"

namespace mt {

struct Sel {
  uint8_t t0, t1;  // tracks, inclusive, t0 <= t1
  uint8_t s0, s1;  // steps, inclusive, s0 <= s1
};
Sel makeSel(int trackA, int stepA, int trackB, int stepB);  // normalizes order and clamps

struct Clipboard {
  uint8_t tracks = 0, steps = 0;  // 0 = empty
  Step data[kTracks][kMaxSteps];
};

void copySel(const Pattern& p, const Sel& s, Clipboard& cb);
// Pastes with the clipboard's top-left at (track, step); clipped to kTracks and p.length.
void pasteAt(Pattern& p, const Clipboard& cb, int track, int step);
void clearSel(Pattern& p, const Sel& s);
// Notes only (OFF and empty steps untouched). degrees=true moves by scale degrees.
// drumTracks[t] (optional): track t is a drum track, left as is (its note is a velocity).
void transposeSel(Pattern& p, const Sel& s, int amount, bool degrees, uint8_t root, ScaleType t,
                  const bool* drumTracks = nullptr);

// Capture gen: writes track tr's generator loop (c, from state st, not changed) into p, one pass of the
// track's length starting at step fromStep (wrapping), with Flip 0 (FLP locks ignored): the loop as it plays
// now, without mutations. PITCH: the note steps get the generated notes. GATE: steps with a gate get the
// note, the others lose theirs; OFF stays. Velocities and fx stay.
void captureGen(Pattern& p, int tr, const GenCfg& c, const GenState& st, int fromStep, uint8_t root, ScaleType t);

// Song slots: inserts slot (pat, mute) at `at` (clamped to 0..songLen), false when full; deletes slot
// `at` (nothing past the end). A freed slot is zeroed.
bool songInsert(Project& p, int at, uint8_t pat, uint16_t mute);
void songDelete(Project& p, int at);
// Bit t set: track t of pattern pat has a non-empty step within the pattern length.
uint16_t patternTrackMask(const Project& p, int pat);

}  // namespace mt
