#pragma once
#include <stdint.h>

namespace mt {

enum class ScaleType : uint8_t {
  Chromatic, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian,
  HarmonicMinor, MelodicMinor, PentatonicMajor, PentatonicMinor, Blues, Count
};

// CHD values.
enum : uint8_t {
  kChordTriad, kChordSeventh, kChordSus2, kChordSus4, kChordSixth, kChordAdd9,
  kChordPower, kChordOctave, kChordCount
};

uint16_t scaleMask(ScaleType t);  // bit i set = semitone i above the root is in the scale
const char* scaleName(ScaleType t);
const char* chordName(uint8_t chord);  // 3 chars
bool inScale(int note, uint8_t root, ScaleType t);
// Moves by scale degrees. An out-of-scale start counts its nearest lower scale note as one
// degree down. Result is clamped to 0..127.
int moveDegrees(int note, int degrees, uint8_t root, ScaleType t);
// Fills up to 4 notes (root first), drops notes above 127. Returns the count.
int chordNotes(uint8_t note, uint8_t chord, uint8_t root, ScaleType t, uint8_t out[4]);

}  // namespace mt
