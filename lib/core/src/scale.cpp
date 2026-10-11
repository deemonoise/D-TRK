#include "scale.h"
#include <stdio.h>
#include <string.h>

namespace mt {
namespace {

constexpr uint16_t kMasks[] = {
    0x0FFF,  // Chromatic
    0x0AB5,  // Major            0 2 4 5 7 9 11
    0x05AD,  // Minor            0 2 3 5 7 8 10
    0x06AD,  // Dorian           0 2 3 5 7 9 10
    0x05AB,  // Phrygian         0 1 3 5 7 8 10
    0x0AD5,  // Lydian           0 2 4 6 7 9 11
    0x06B5,  // Mixolydian       0 2 4 5 7 9 10
    0x056B,  // Locrian          0 1 3 5 6 8 10
    0x09AD,  // Harmonic minor   0 2 3 5 7 8 11
    0x0AAD,  // Melodic minor    0 2 3 5 7 9 11
    0x0295,  // Pentatonic major 0 2 4 7 9
    0x04A9,  // Pentatonic minor 0 3 5 7 10
    0x04E9,  // Blues            0 3 5 6 7 10
};
constexpr const char* kScaleNames[] = {"Chrom", "Major", "Minor", "Dorian", "Phryg", "Lydian", "Mixo",
                                       "Locr", "HarmMin", "MelMin", "PentMaj", "PentMin", "Blues"};
constexpr const char* kChordNames[] = {
    "tri", "7th", "su2", "su4", "6th", "ad9", "pwr", "oct", "9th", "7s4", "a11", "MAJ",
    "MIN", "DIM", "AUG", "SU2", "SU4", "MA7", "MI7", "DOM", "HDM", "DM7", "MMA", "MA6",
    "MI6", "MA9", "MI9", "DO9", "7S4", "AD9", "MD9", "D7#", "D7b", "D13", "M11", "QRT",
};
static_assert(sizeof(kChordNames) / sizeof(kChordNames[0]) == kChordTypes, "chord names");
// Scale degrees of each degree-based chord (tri..ad9, 9th..a11), -1 terminates.
constexpr int8_t kChordDegrees[][4] = {
    {0, 2, 4, -1}, {0, 2, 4, 6}, {0, 1, 4, -1}, {0, 3, 4, -1}, {0, 2, 4, 5}, {0, 2, 4, 8},
};
constexpr int8_t kChordDegrees2[][4] = {{0, 2, 6, 8}, {0, 3, 4, 6}, {0, 2, 4, 10}};
static_assert(sizeof(kChordDegrees2) / sizeof(kChordDegrees2[0]) == kChordMaj - kChordNinth, "diatonic chords");
// Semitones of each fixed chord (MAJ..QRT), -1 terminates.
constexpr int8_t kChordSemis[][4] = {
    {0, 4, 7, -1}, {0, 3, 7, -1},  {0, 3, 6, -1},  {0, 4, 8, -1},  {0, 2, 7, -1},  {0, 5, 7, -1},  {0, 4, 7, 11},
    {0, 3, 7, 10}, {0, 4, 7, 10},  {0, 3, 6, 10},  {0, 3, 6, 9},   {0, 3, 7, 11},  {0, 4, 7, 9},   {0, 3, 7, 9},
    {0, 4, 11, 14}, {0, 3, 10, 14}, {0, 4, 10, 14}, {0, 5, 7, 10}, {0, 4, 7, 14}, {0, 3, 7, 14}, {0, 4, 10, 15},
    {0, 4, 10, 13}, {0, 4, 10, 21}, {0, 3, 10, 17}, {0, 5, 10, 15},
};
static_assert(sizeof(kChordSemis) / sizeof(kChordSemis[0]) == kChordTypes - kChordMaj, "fixed chords");

int clampNote(int n) { return n < 0 ? 0 : (n > 127 ? 127 : n); }

}  // namespace

uint16_t scaleMask(ScaleType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < static_cast<uint8_t>(ScaleType::Count) ? kMasks[i] : kMasks[0];
}

int scaleDegrees(ScaleType t) { return __builtin_popcount(scaleMask(t)); }

const char* scaleName(ScaleType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < static_cast<uint8_t>(ScaleType::Count) ? kScaleNames[i] : kScaleNames[0];
}

const char* chordName(uint8_t chord) { return chord < kChordValues ? kChordNames[chordType(chord)] : "???"; }

void chordLabel(uint8_t chord, char out[5]) {
  const int inv = chord < kChordValues ? chordInv(chord) : 0;
  snprintf(out, 5, "%s%c", chordName(chord), inv ? '0' + inv : ' ');
}

int chordTypeByName(const char* s, int len) {
  if (!s || len != 3) return -1;
  for (int i = 0; i < kChordTypes; ++i)
    if (!strncmp(s, kChordNames[i], 3)) return i;
  return -1;
}

bool inScale(int note, uint8_t root, ScaleType t) {
  const int pc = ((note - root) % 12 + 12) % 12;
  return (scaleMask(t) >> pc) & 1;
}

int moveDegrees(int note, int degrees, uint8_t root, ScaleType t) {
  int n = clampNote(note);
  if (!inScale(n, root, t)) {
    while (n > 0 && !inScale(n, root, t)) --n;
    if (degrees < 0) ++degrees;  // stepping onto the lower scale note already moved one degree down
  }
  const int dir = degrees > 0 ? 1 : -1;
  for (int k = degrees > 0 ? degrees : -degrees; k > 0; --k) {
    int m = n + dir;
    while (m >= 0 && m <= 127 && !inScale(m, root, t)) m += dir;
    if (m < 0 || m > 127) break;
    n = m;
  }
  return n;
}

int degreeNote(int button, uint8_t root, ScaleType t, int base) {
  const uint16_t mask = t == ScaleType::Chromatic ? scaleMask(ScaleType::Major) : scaleMask(t);
  int deg[12];
  int k = 0;
  for (int i = 0; i < 12; ++i)
    if ((mask >> i) & 1) deg[k++] = i;
  if (button < 0) button = 0;
  int n = base + root + deg[button % k] + 12 * (button / k);
  while (n > 127) n -= 12;
  return n < 0 ? 0 : n;
}

int chordNotes(uint8_t note, uint8_t chord, uint8_t root, ScaleType t, uint8_t out[4]) {
  int count = 0;
  auto add = [&](int n) {
    if (n >= 0 && n <= 127 && count < 4) out[count++] = static_cast<uint8_t>(n);
  };
  if (chord >= kChordValues) {
    add(note);
    return count;
  }
  const uint8_t type = chordType(chord);
  if (type == kChordPower || type == kChordOctave) {
    add(note);
    add(note + (type == kChordPower ? 7 : 12));
  } else if (type >= kChordMaj) {
    for (int8_t d : kChordSemis[type - kChordMaj])
      if (d >= 0) add(note + d);
  } else {
    // Chromatic has no harmony of its own: build the chord as if the note were a major tonic.
    const bool chromatic = t == ScaleType::Chromatic;
    const uint8_t r = chromatic ? note % 12 : root;
    const ScaleType s = chromatic ? ScaleType::Major : t;
    const int8_t* deg = type < kChordPower ? kChordDegrees[type] : kChordDegrees2[type - kChordNinth];
    for (int i = 0; i < 4; ++i) {
      const int8_t d = deg[i];
      if (d < 0) break;
      if (d == 0) add(note);
      else {
        const int n = moveDegrees(note, d, r, s);
        if (n > note) add(n);  // moveDegrees clamps at 127: ignore notes that didn't move up
      }
    }
  }
  // Inversion: the lowest note goes up an octave (dropped above 127 or when already in the chord),
  // the rest stay sorted.
  for (int k = chordInv(chord); k > 0 && count > 0; --k) {
    const int up = out[0] + 12;
    for (int i = 1; i < count; ++i) out[i - 1] = out[i];
    --count;
    bool dup = false;
    for (int i = 0; i < count; ++i) dup = dup || out[i] == up;
    int i = count;
    if (up <= 127 && !dup) {
      while (i > 0 && out[i - 1] > up) {
        out[i] = out[i - 1];
        --i;
      }
      out[i] = static_cast<uint8_t>(up);
      ++count;
    }
  }
  return count;
}

}  // namespace mt
