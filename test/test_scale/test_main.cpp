#include <unity.h>
#include "scale.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_masks() {
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, scaleMask(ScaleType::Chromatic));
  TEST_ASSERT_EQUAL_HEX16(0x0AB5, scaleMask(ScaleType::Major));  // 0 2 4 5 7 9 11
  TEST_ASSERT_EQUAL_HEX16(0x05AD, scaleMask(ScaleType::Minor));  // 0 2 3 5 7 8 10
}

void test_in_scale_with_root() {
  // D major: D E F# G A B C#
  TEST_ASSERT_TRUE(inScale(62, 2, ScaleType::Major));
  TEST_ASSERT_TRUE(inScale(66, 2, ScaleType::Major));
  TEST_ASSERT_FALSE(inScale(65, 2, ScaleType::Major));
  TEST_ASSERT_TRUE(inScale(65, 2, ScaleType::Chromatic));
}

void test_move_degrees() {
  // C major
  TEST_ASSERT_EQUAL(62, moveDegrees(60, 1, 0, ScaleType::Major));
  TEST_ASSERT_EQUAL(59, moveDegrees(60, -1, 0, ScaleType::Major));
  TEST_ASSERT_EQUAL(72, moveDegrees(60, 7, 0, ScaleType::Major));
  // out-of-scale start: C# +1 -> D, C# -1 -> C
  TEST_ASSERT_EQUAL(62, moveDegrees(61, 1, 0, ScaleType::Major));
  TEST_ASSERT_EQUAL(60, moveDegrees(61, -1, 0, ScaleType::Major));
  // chromatic = semitones
  TEST_ASSERT_EQUAL(63, moveDegrees(60, 3, 0, ScaleType::Chromatic));
  // clamped to MIDI range
  TEST_ASSERT_EQUAL(127, moveDegrees(126, 5, 0, ScaleType::Chromatic));
  TEST_ASSERT_EQUAL(0, moveDegrees(1, -5, 0, ScaleType::Chromatic));
}

void test_chords() {
  uint8_t n[4];
  // C major triad from C4 in C major
  TEST_ASSERT_EQUAL(3, chordNotes(60, kChordTriad, 0, ScaleType::Major, n));
  TEST_ASSERT_EQUAL(60, n[0]); TEST_ASSERT_EQUAL(64, n[1]); TEST_ASSERT_EQUAL(67, n[2]);
  // D in C major -> D minor triad
  chordNotes(62, kChordTriad, 0, ScaleType::Major, n);
  TEST_ASSERT_EQUAL(65, n[1]); TEST_ASSERT_EQUAL(69, n[2]);
  // seventh: 4 notes
  TEST_ASSERT_EQUAL(4, chordNotes(60, kChordSeventh, 0, ScaleType::Major, n));
  TEST_ASSERT_EQUAL(71, n[3]);
  // power / octave are fixed intervals
  TEST_ASSERT_EQUAL(2, chordNotes(60, kChordPower, 0, ScaleType::Minor, n));
  TEST_ASSERT_EQUAL(67, n[1]);
  chordNotes(60, kChordOctave, 0, ScaleType::Minor, n);
  TEST_ASSERT_EQUAL(72, n[1]);
  // chromatic scale: chords use major intervals relative to the note
  chordNotes(61, kChordTriad, 0, ScaleType::Chromatic, n);
  TEST_ASSERT_EQUAL(65, n[1]); TEST_ASSERT_EQUAL(68, n[2]);
  // notes above 127 are dropped
  TEST_ASSERT_EQUAL(1, chordNotes(126, kChordOctave, 0, ScaleType::Chromatic, n));
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("Major", scaleName(ScaleType::Major));
  TEST_ASSERT_EQUAL_STRING("tri", chordName(kChordTriad));
}

void test_degree_note() {
  const int cmaj[8] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (int b = 0; b < 8; ++b) TEST_ASSERT_EQUAL(cmaj[b], degreeNote(b, 0, ScaleType::Major, 60));
  // Short scales continue into the next octave: C minor pentatonic C Eb F G Bb | C Eb F
  TEST_ASSERT_EQUAL(70, degreeNote(4, 0, ScaleType::PentatonicMinor, 60));
  TEST_ASSERT_EQUAL(72, degreeNote(5, 0, ScaleType::PentatonicMinor, 60));
  TEST_ASSERT_EQUAL(75, degreeNote(6, 0, ScaleType::PentatonicMinor, 60));
  TEST_ASSERT_EQUAL(77, degreeNote(7, 0, ScaleType::PentatonicMinor, 60));
  // Blues (6 notes): button 7 = II + octave
  TEST_ASSERT_EQUAL(75, degreeNote(7, 0, ScaleType::Blues, 60));
  // Root shifts the tonic; Chromatic plays major degrees
  TEST_ASSERT_EQUAL(62, degreeNote(0, 2, ScaleType::Major, 60));
  TEST_ASSERT_EQUAL(66, degreeNote(2, 2, ScaleType::Major, 60));
  TEST_ASSERT_EQUAL(65, degreeNote(3, 0, ScaleType::Chromatic, 60));
  // Above 127: same degree an octave (or more) lower
  TEST_ASSERT_EQUAL(120, degreeNote(7, 0, ScaleType::Major, 120));
  TEST_ASSERT_EQUAL(127, degreeNote(4, 0, ScaleType::Major, 120));
  TEST_ASSERT_EQUAL(119, degreeNote(6, 0, ScaleType::Major, 120));
}

void test_scale_degrees() {
  TEST_ASSERT_EQUAL(12, scaleDegrees(ScaleType::Chromatic));
  TEST_ASSERT_EQUAL(7, scaleDegrees(ScaleType::Major));
  TEST_ASSERT_EQUAL(5, scaleDegrees(ScaleType::PentatonicMinor));
  TEST_ASSERT_EQUAL(6, scaleDegrees(ScaleType::Blues));
}

static void expectNotes(const uint8_t* got, int n, const int* want, int wn) {
  TEST_ASSERT_EQUAL(wn, n);
  for (int i = 0; i < wn; ++i) TEST_ASSERT_EQUAL(want[i], got[i]);
}

void test_new_diatonic_chords() {
  uint8_t n[4];
  const int ninth[] = {60, 64, 71, 74}, sus7[] = {60, 65, 67, 71}, add11[] = {60, 64, 67, 77};
  expectNotes(n, chordNotes(60, kChordNinth, 0, ScaleType::Major, n), ninth, 4);
  expectNotes(n, chordNotes(60, kChordSeventhSus4, 0, ScaleType::Major, n), sus7, 4);
  expectNotes(n, chordNotes(60, kChordAdd11, 0, ScaleType::Major, n), add11, 4);
}

void test_fixed_chords() {
  static const int8_t kWant[][4] = {
      {0, 4, 7, -1},  {0, 3, 7, -1},  {0, 3, 6, -1},  {0, 4, 8, -1},  {0, 2, 7, -1},  {0, 5, 7, -1},
      {0, 4, 7, 11},  {0, 3, 7, 10},  {0, 4, 7, 10},  {0, 3, 6, 10},  {0, 3, 6, 9},   {0, 3, 7, 11},
      {0, 4, 7, 9},   {0, 3, 7, 9},   {0, 4, 11, 14}, {0, 3, 10, 14}, {0, 4, 10, 14}, {0, 5, 7, 10},
      {0, 4, 7, 14},  {0, 3, 7, 14},  {0, 4, 10, 15}, {0, 4, 10, 13}, {0, 4, 10, 21}, {0, 3, 10, 17},
      {0, 5, 10, 15},
  };
  TEST_ASSERT_EQUAL(kChordTypes - kChordMaj, sizeof(kWant) / sizeof(kWant[0]));
  const ScaleType scales[] = {ScaleType::Major, ScaleType::Minor, ScaleType::Chromatic};
  for (int i = 0; i < kChordTypes - kChordMaj; ++i) {
    for (ScaleType sc : scales) {
      uint8_t n[4];
      int want[4], wn = 0;
      for (int8_t d : kWant[i])
        if (d >= 0) want[wn++] = 60 + d;
      expectNotes(n, chordNotes(60, static_cast<uint8_t>(kChordMaj + i), 3, sc, n), want, wn);
    }
  }
}

void test_inversions() {
  uint8_t n[4];
  const int i1[] = {64, 67, 72}, i2[] = {67, 72, 76}, i3[] = {72, 76, 79}, m7[] = {71, 72, 76, 79};
  expectNotes(n, chordNotes(60, chordValue(kChordMaj, 1), 0, ScaleType::Major, n), i1, 3);
  expectNotes(n, chordNotes(60, chordValue(kChordMaj, 2), 0, ScaleType::Major, n), i2, 3);
  expectNotes(n, chordNotes(60, chordValue(kChordMaj, 3), 0, ScaleType::Major, n), i3, 3);
  expectNotes(n, chordNotes(60, chordValue(kChordMaj7, 3), 0, ScaleType::Major, n), m7, 4);
  // diatonic types invert too: C major triad, 1st inversion
  expectNotes(n, chordNotes(60, chordValue(kChordTriad, 1), 0, ScaleType::Major, n), i1, 3);
  // two-note types: pwr inv 1 = G C
  const int p1[] = {67, 72};
  expectNotes(n, chordNotes(60, chordValue(kChordPower, 1), 0, ScaleType::Minor, n), p1, 2);
  const int p2[] = {72, 79};
  expectNotes(n, chordNotes(60, chordValue(kChordPower, 2), 0, ScaleType::Minor, n), p2, 2);
  // a lifted note equal to one already in the chord is dropped: oct inv 1 = C5 alone
  const int o1[] = {72};
  expectNotes(n, chordNotes(60, chordValue(kChordOctave, 1), 0, ScaleType::Minor, n), o1, 1);
  TEST_ASSERT_EQUAL(kChordTypes, chordValue(kChordTriad, 1));
  TEST_ASSERT_EQUAL(kChordMaj7, chordType(chordValue(kChordMaj7, 2)));
  TEST_ASSERT_EQUAL(2, chordInv(chordValue(kChordMaj7, 2)));
}

void test_chords_near_top() {
  uint8_t n[4];
  // 120 + 4 + 7 + 11: 131 dropped
  const int top[] = {120, 124, 127};
  expectNotes(n, chordNotes(120, kChordMaj7, 0, ScaleType::Major, n), top, 3);
  // inversion of the top note is dropped, never wraps
  const int inv[] = {124, 127};
  expectNotes(n, chordNotes(120, chordValue(kChordMaj, 1), 0, ScaleType::Major, n), inv, 2);
  for (int v = 0; v < kChordValues; ++v) {
    const int c = chordNotes(127, static_cast<uint8_t>(v), 0, ScaleType::Minor, n);
    TEST_ASSERT_TRUE(c >= 0 && c <= 4);
    for (int i = 0; i < c; ++i) TEST_ASSERT_TRUE(n[i] <= 127);
    for (int i = 1; i < c; ++i) TEST_ASSERT_TRUE(n[i - 1] <= n[i]);
  }
}

void test_chord_labels() {
  char b[5];
  chordLabel(chordValue(kChordMaj7, 0), b);
  TEST_ASSERT_EQUAL_STRING("MA7 ", b);
  chordLabel(chordValue(kChordMaj7, 2), b);
  TEST_ASSERT_EQUAL_STRING("MA72", b);
  chordLabel(chordValue(kChordTriad, 2), b);
  TEST_ASSERT_EQUAL_STRING("tri2", b);
  chordLabel(200, b);
  TEST_ASSERT_EQUAL_STRING("??? ", b);
  TEST_ASSERT_EQUAL_STRING("QRT", chordName(kChordQuartal));
  TEST_ASSERT_EQUAL_STRING("MA7", chordName(chordValue(kChordMaj7, 1)));
  TEST_ASSERT_EQUAL_STRING("???", chordName(kChordValues));
  TEST_ASSERT_EQUAL(36, kChordTypes);
  TEST_ASSERT_EQUAL(144, kChordValues);
}

void test_chord_type_by_name() {
  TEST_ASSERT_EQUAL(kChordMaj7, chordTypeByName("MA7", 3));
  TEST_ASSERT_EQUAL(kChordTriad, chordTypeByName("tri", 3));
  TEST_ASSERT_EQUAL(kChordMaj, chordTypeByName("MAJ", 3));
  TEST_ASSERT_EQUAL(kChordDom7Flat9, chordTypeByName("D7b", 3));
  TEST_ASSERT_EQUAL(-1, chordTypeByName("xyz", 3));
  TEST_ASSERT_EQUAL(-1, chordTypeByName("TRI", 3));
  TEST_ASSERT_EQUAL(-1, chordTypeByName("MA", 2));
  TEST_ASSERT_EQUAL(-1, chordTypeByName("MA7x", 4));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_masks);
  RUN_TEST(test_in_scale_with_root);
  RUN_TEST(test_move_degrees);
  RUN_TEST(test_chords);
  RUN_TEST(test_names);
  RUN_TEST(test_degree_note);
  RUN_TEST(test_scale_degrees);
  RUN_TEST(test_new_diatonic_chords);
  RUN_TEST(test_fixed_chords);
  RUN_TEST(test_inversions);
  RUN_TEST(test_chords_near_top);
  RUN_TEST(test_chord_labels);
  RUN_TEST(test_chord_type_by_name);
  return UNITY_END();
}
