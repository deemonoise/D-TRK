#include "scale.h"

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
constexpr const char* kChordNames[] = {"tri", "7th", "su2", "su4", "6th", "ad9", "pwr", "oct"};
// Scale degrees of each degree-based chord, -1 terminates.
constexpr int8_t kChordDegrees[][4] = {
    {0, 2, 4, -1}, {0, 2, 4, 6}, {0, 1, 4, -1}, {0, 3, 4, -1}, {0, 2, 4, 5}, {0, 2, 4, 8},
};

int clampNote(int n) { return n < 0 ? 0 : (n > 127 ? 127 : n); }

}  // namespace

uint16_t scaleMask(ScaleType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < static_cast<uint8_t>(ScaleType::Count) ? kMasks[i] : kMasks[0];
}

const char* scaleName(ScaleType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < static_cast<uint8_t>(ScaleType::Count) ? kScaleNames[i] : kScaleNames[0];
}

const char* chordName(uint8_t chord) { return chord < kChordCount ? kChordNames[chord] : "???"; }

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

int chordNotes(uint8_t note, uint8_t chord, uint8_t root, ScaleType t, uint8_t out[4]) {
  int count = 0;
  auto add = [&](int n) {
    if (n >= 0 && n <= 127 && count < 4) out[count++] = static_cast<uint8_t>(n);
  };
  if (chord == kChordPower) {
    add(note);
    add(note + 7);
    return count;
  }
  if (chord == kChordOctave) {
    add(note);
    add(note + 12);
    return count;
  }
  if (chord >= kChordPower) {
    add(note);
    return count;
  }
  // Chromatic has no harmony of its own: build the chord as if the note were a major tonic.
  const bool chromatic = t == ScaleType::Chromatic;
  const uint8_t r = chromatic ? note % 12 : root;
  const ScaleType s = chromatic ? ScaleType::Major : t;
  for (int8_t d : kChordDegrees[chord]) {
    if (d < 0) break;
    if (d == 0) add(note);
    else {
      const int n = moveDegrees(note, d, r, s);
      if (n > note) add(n);  // moveDegrees clamps at 127: ignore notes that didn't move up
    }
  }
  return count;
}

}  // namespace mt
