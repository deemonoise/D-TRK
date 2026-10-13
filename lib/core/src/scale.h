#pragma once
#include <stdint.h>

namespace mt {

enum class ScaleType : uint8_t {
  Chromatic, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian,
  HarmonicMinor, MelodicMinor, PentatonicMajor, PentatonicMinor, Blues, Count
};

// CHD types. Lowercase names are built from scale degrees, uppercase ones are fixed semitones.
enum : uint8_t {
  kChordTriad, kChordSeventh, kChordSus2, kChordSus4, kChordSixth, kChordAdd9,
  kChordPower, kChordOctave,
  kChordNinth, kChordSeventhSus4, kChordAdd11,                       // diatonic
  kChordMaj, kChordMin, kChordDim, kChordAug, kChordSus2Fix, kChordSus4Fix,
  kChordMaj7, kChordMin7, kChordDom7, kChordHalfDim, kChordDim7, kChordMinMaj7,
  kChordMaj6, kChordMin6, kChordMaj9, kChordMin9, kChordDom9, kChordDom7Sus4,
  kChordMajAdd9, kChordMinAdd9, kChordDom7Sharp9, kChordDom7Flat9, kChordDom13,
  kChordMin11, kChordQuartal,
  kChordTypes
};
// CHD value: inv * kChordTypes + type, inv 0..3 (inv k: the k lowest notes up an octave).
constexpr int kChordInversions = 4;
constexpr int kChordValues = kChordTypes * kChordInversions;  // CHD range 0 .. kChordValues - 1
// 0xFF is Synth::kNoArpChord ("no ARP chord"), so a CHD value must never reach it.
static_assert(kChordValues < 0xFF, "CHD values must stay below kNoArpChord");
inline uint8_t chordType(uint8_t v) { return v % kChordTypes; }
inline uint8_t chordInv(uint8_t v) { return v / kChordTypes; }
inline uint8_t chordValue(uint8_t type, uint8_t inv) { return static_cast<uint8_t>(inv * kChordTypes + type); }

uint16_t scaleMask(ScaleType t);
int scaleDegrees(ScaleType t);  // notes per octave  // bit i set = semitone i above the root is in the scale
const char* scaleName(ScaleType t);
const char* chordName(uint8_t chord);         // the type's 3 chars ("tri", "MA7"); "???" out of range
void chordLabel(uint8_t chord, char out[5]);  // name + inversion digit: "MA7 ", "tri2"; "??? " out of range
// Name -> type (exact, case-sensitive: "tri" vs "MAJ"); -1 when unknown.
int chordTypeByName(const char* s, int len);
bool inScale(int note, uint8_t root, ScaleType t);
// Moves by scale degrees. An out-of-scale start counts its nearest lower scale note as one
// degree down. Result is clamped to 0..127.
int moveDegrees(int note, int degrees, uint8_t root, ScaleType t);
// Note of track button 0..7: buttons 1-7 play degrees I-VII of the scale from the tonic at
// base + root (base = C of the octave), button 8 plays I an octave up. Scales shorter than 7
// notes continue into the next octave; Chromatic plays major degrees. Above 127 drops octaves.
int degreeNote(int button, uint8_t root, ScaleType t, int base);
// Fills up to 4 notes sorted ascending (the note is the chord root; an inversion lifts the lowest notes an
// octave), drops notes above 127. Returns the count.
int chordNotes(uint8_t note, uint8_t chord, uint8_t root, ScaleType t, uint8_t out[4]);

}  // namespace mt
