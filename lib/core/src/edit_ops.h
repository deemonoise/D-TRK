#pragma once
#include <stdint.h>
#include "model.h"
#include "scale.h"

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
void transposeSel(Pattern& p, const Sel& s, int amount, bool degrees, uint8_t root, ScaleType t);

}  // namespace mt
