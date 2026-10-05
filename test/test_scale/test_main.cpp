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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_masks);
  RUN_TEST(test_in_scale_with_root);
  RUN_TEST(test_move_degrees);
  RUN_TEST(test_chords);
  RUN_TEST(test_names);
  RUN_TEST(test_degree_note);
  RUN_TEST(test_scale_degrees);
  return UNITY_END();
}
