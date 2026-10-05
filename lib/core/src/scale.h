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

uint16_t scaleMask(ScaleType t);
int scaleDegrees(ScaleType t);  // notes per octave  // bit i set = semitone i above the root is in the scale
const char* scaleName(ScaleType t);
const char* chordName(uint8_t chord);  // 3 chars
bool inScale(int note, uint8_t root, ScaleType t);
// Moves by scale degrees. An out-of-scale start counts its nearest lower scale note as one
// degree down. Result is clamped to 0..127.
int moveDegrees(int note, int degrees, uint8_t root, ScaleType t);
// Note of track button 0..7: buttons 1-7 play degrees I-VII of the scale from the tonic at
// base + root (base = C of the octave), button 8 plays I an octave up. Scales shorter than 7
// notes continue into the next octave; Chromatic plays major degrees. Above 127 drops octaves.
int degreeNote(int button, uint8_t root, ScaleType t, int base);
// Fills up to 4 notes (root first), drops notes above 127. Returns the count.
int chordNotes(uint8_t note, uint8_t chord, uint8_t root, ScaleType t, uint8_t out[4]);

}  // namespace mt
