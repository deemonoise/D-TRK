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
// drumTracks[t] (optional): track t is a drum track, left as is (its note is a velocity).
void transposeSel(Pattern& p, const Sel& s, int amount, bool degrees, uint8_t root, ScaleType t,
                  const bool* drumTracks = nullptr);

// Chain rows: inserts pattern `pat` (transpose 0, 1 pass, no scene) at `at` (clamped to
// 0..chainLen), false when the chain is full; deletes row `at` (nothing past the end). Both keep
// chain / chainTr / chainRep / chainScene aligned; a freed row gets the defaults.
bool chainInsert(Project& p, int at, uint8_t pat);
void chainDelete(Project& p, int at);

}  // namespace mt
