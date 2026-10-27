#pragma once
#include <stdint.h>
#include "model.h"
#include "scale.h"

namespace mt {

// GRID's piano roll (src/ui/grid_screen.*): note lengths through the GAT value, the bars as drawn,
// the keys by the scale, the KIT lanes and the view following the cursor.

// A step's GAT value: its GAT, else the track's defGate.
uint8_t rollGateVal(const Step& s, uint8_t defGate);
// Writes GAT value v into the step: equal to defGate removes GAT (the track's Def gate plays), else
// the GAT slot, else the first free one. NoSlot: all slots taken, the step untouched.
enum class RollLen : uint8_t { Ok, NoSlot };
RollLen rollSetGate(Step& s, uint8_t v, uint8_t defGate);

// The bar of step i in hundredths of a step (GAT 300 % = 300, 50 % = 50; no GAT = defGate), cut by
// the next note or OFF (the track is mono) and by count (the end of the track); 0 without a note.
int rollSpan(const Step* steps, int count, int i, uint8_t defGate);
// CHD on a note step: the chord's other notes (not the step's own) into out; the count, 0 without CHD.
int rollChordExtra(const Step& s, uint8_t root, ScaleType t, uint8_t out[4]);

// A key by the project scale: the root (any scale), in / out of the scale, Chromatic for the rest.
enum class RollKey : uint8_t { Root, In, Out, Chromatic };
RollKey rollKey(int note, uint8_t root, ScaleType t);

// The key column as a vertical piano, a semitone per row: a black key's row is its bar on the left
// and, right of it, its white neighbours meeting at the row's middle (upper = note + 1 above, lower =
// note - 1 below); a white key's row is all its own (upper = lower = note). lineBelow: C and F, the
// white keys right above a white one, end with a full-width line.
struct RollKeyRow {
  bool black;
  bool lineBelow;
  int upper, lower;
};
RollKeyRow rollKeyRow(int note);

// KIT: flips lane's bit; an empty (or OFF) step becomes a note step at the track's velocity (note 0).
// True when the lane is now on.
bool rollToggleLane(Step& s, int lane);

// The first visible of total items (rows, notes) after the least scroll that shows cur, visible at once.
int rollFollow(int first, int cur, int visible, int total);
// The page of page columns holding cur: its first column.
inline int rollPage(int cur, int page) { return cur < 0 ? 0 : cur / page * page; }

// RGB565 a..b by t (0 = a, 256 = b): velocity brightness, greyed keys, the root stripe.
uint16_t rollMix565(uint16_t a, uint16_t b, int t);

}  // namespace mt
