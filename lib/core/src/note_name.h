#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Tracker-style name, 60 = "C-4". Octave -1 is shown as 'm'. out must hold 4 chars.
inline void noteName(uint8_t note, char out[4]) {
  if (note == kNoteEmpty || note > kNoteOff) { out[0] = out[1] = out[2] = '-'; out[3] = 0; return; }
  if (note == kNoteOff) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return; }
  static const char kNames[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
  int pc = note % 12;
  int oct = note / 12 - 1;
  out[0] = kNames[pc * 2];
  out[1] = kNames[pc * 2 + 1];
  out[2] = oct < 0 ? 'm' : char('0' + oct);
  out[3] = 0;
}

}  // namespace mt
