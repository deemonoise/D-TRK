#include <string.h>
#include <initializer_list>
#include <unity.h>
#include "chord_gen.h"
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Pattern gPat;  // too big for the stack

static ChordSpec spec4() {
  ChordSpec s;
  s.length = 4;
  return s;
}

void test_parse_tokens() {
  Progression p;
  TEST_ASSERT_TRUE(parseProgression("2MI7 5DOM 1MA7*2", p));
  TEST_ASSERT_EQUAL(3, p.n);
  TEST_ASSERT_FALSE(p.steps);
  TEST_ASSERT_EQUAL(1, p.c[0].degree);
  TEST_ASSERT_EQUAL(4, p.c[1].degree);
  TEST_ASSERT_EQUAL(0, p.c[2].degree);
  TEST_ASSERT_EQUAL(kChordMin7, p.c[0].type);
  TEST_ASSERT_EQUAL(kChordDom7, p.c[1].type);
  TEST_ASSERT_EQUAL(kChordMaj7, p.c[2].type);
  TEST_ASSERT_EQUAL(1, p.c[0].len);
  TEST_ASSERT_EQUAL(1, p.c[1].len);
  TEST_ASSERT_EQUAL(2, p.c[2].len);

  TEST_ASSERT_TRUE(parseProgression("b7 #4 1 ?? 9", p));  // unknown tokens skipped
  TEST_ASSERT_EQUAL(3, p.n);
  TEST_ASSERT_EQUAL(6, p.c[0].degree);
  TEST_ASSERT_EQUAL(-1, p.c[0].shift);
  TEST_ASSERT_EQUAL(3, p.c[1].degree);
  TEST_ASSERT_EQUAL(1, p.c[1].shift);
  TEST_ASSERT_TRUE(p.c[0].roman);
  TEST_ASSERT_TRUE(p.c[1].roman);
  TEST_ASSERT_EQUAL(0, p.c[2].shift);
  TEST_ASSERT_FALSE(p.c[2].roman);
  TEST_ASSERT_EQUAL(-1, p.c[2].type);
  // 'n': natural Roman numeral; '*N' digits are capped before the clamp.
  TEST_ASSERT_TRUE(parseProgression("n6MA7 1*99999999999", p));
  TEST_ASSERT_EQUAL(2, p.n);
  TEST_ASSERT_TRUE(p.c[0].roman);
  TEST_ASSERT_EQUAL(0, p.c[0].shift);
  TEST_ASSERT_EQUAL(5, p.c[0].degree);
  TEST_ASSERT_EQUAL(kChordMaj7, p.c[0].type);
  TEST_ASSERT_EQUAL(128, p.c[1].len);
  TEST_ASSERT_FALSE(parseProgression("  x ", p));
}

void test_steps_and_format() {
  Progression p, q;
  TEST_ASSERT_TRUE(parseProgression("@steps 1*8 4*8", p));
  TEST_ASSERT_TRUE(p.steps);
  TEST_ASSERT_EQUAL(2, p.n);
  TEST_ASSERT_EQUAL(8, p.c[1].len);
  char buf[kProgTextMax];
  TEST_ASSERT_TRUE(formatProgression(p, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("@steps 1*8 4*8", buf);
  TEST_ASSERT_TRUE(parseProgression("2MI7  5DOM,1MA7*2 b7 #4 n6MA7", q));
  TEST_ASSERT_TRUE(formatProgression(q, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("2MI7 5DOM 1MA7*2 b7 #4 n6MA7", buf);
  TEST_ASSERT_FALSE(formatProgression(q, buf, 8));
}

void test_factory_parses() {
  TEST_ASSERT_TRUE(progFactoryCount() >= 30);
  for (int i = 0; i < progFactoryCount(); ++i) {
    Progression p;
    TEST_ASSERT_TRUE(strlen(progFactoryName(i)) <= 10);
    TEST_ASSERT_TRUE_MESSAGE(parseProgression(progFactoryText(i), p), progFactoryName(i));
    TEST_ASSERT_TRUE(p.n > 0);
  }
  TEST_ASSERT_NULL(progFactoryName(progFactoryCount()));
  TEST_ASSERT_EQUAL(16, chordRhythmCount());
  for (int i = 0; i < chordRhythmCount(); ++i) {
    ArpPattern r;
    TEST_ASSERT_TRUE(strlen(chordRhythmName(i)) <= 10);
    TEST_ASSERT_TRUE_MESSAGE(parseArpPattern(chordRhythmText(i), r), chordRhythmName(i));
    TEST_ASSERT_EQUAL_MESSAGE(16, r.len, chordRhythmName(i));
  }
  TEST_ASSERT_NULL(chordRhythmText(-1));
}

void test_layout_length() {
  Progression p;
  parseProgression("1 5 6 4", p);
  ChordSeg seg[kProgMax];
  ChordSpec s = spec4();
  s.voicing = ChordVoicing::Root;  // CLOSE may move roots an octave down (test_voicing_close)
  TEST_ASSERT_EQUAL(4, layoutChords(p, s, 16, 0, ScaleType::Major, seg));
  const uint8_t roots[] = {60, 67, 69, 65};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(roots[i], seg[i].root);
    TEST_ASSERT_EQUAL(4 * i, seg[i].start);
    TEST_ASSERT_EQUAL(4, seg[i].steps);
    TEST_ASSERT_EQUAL(kChordTriad, seg[i].chd);
  }
  // A too-short range cuts the progression, the last kept chord ends at the range end.
  TEST_ASSERT_EQUAL(2, layoutChords(p, spec4(), 6, 0, ScaleType::Major, seg));
  TEST_ASSERT_EQUAL(2, seg[1].steps);
  // Flavor of tokens without a type.
  s.flavor = ChordFlavor::Seventh;
  layoutChords(p, s, 16, 0, ScaleType::Major, seg);
  TEST_ASSERT_EQUAL(kChordSeventh, chordType(seg[0].chd));
  // Mutate 100: every chord swaps to its relative (I-vi, V-iii, vi-I, IV-ii).
  s.mutate = 100;
  TEST_ASSERT_EQUAL(4, layoutChords(p, s, 16, 0, ScaleType::Major, seg));
  const uint8_t rel[] = {69, 64, 60, 62};
  for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(rel[i], seg[i].root);
  // Minor-ish scales: i-III, iv-VI, v-VII. Roman tokens are neither swapped nor given a flavor.
  parseProgression("1 4 5 b7", p);
  TEST_ASSERT_EQUAL(4, layoutChords(p, s, 16, 0, ScaleType::Minor, seg));
  const uint8_t relMin[] = {63, 68, 70, 70};  // Eb Ab Bb, b7 stays Bb
  for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(relMin[i], seg[i].root);
  TEST_ASSERT_EQUAL(kChordMaj, seg[3].chd);
}

void test_layout_auto() {
  Progression p;
  parseProgression("1 4 5", p);
  ChordSpec s;  // length 0 = AUTO
  ChordSeg seg[kProgMax];
  TEST_ASSERT_EQUAL(3, layoutChords(p, s, 16, 0, ScaleType::Major, seg));
  TEST_ASSERT_EQUAL(0, seg[0].start);
  TEST_ASSERT_EQUAL(5, seg[0].steps);
  TEST_ASSERT_EQUAL(5, seg[1].start);
  TEST_ASSERT_EQUAL(5, seg[1].steps);
  TEST_ASSERT_EQUAL(10, seg[2].start);
  TEST_ASSERT_EQUAL(6, seg[2].steps);
  // '@steps': lengths are steps whatever Length says.
  parseProgression("@steps 1*3 4*2", p);
  s.length = 4;
  TEST_ASSERT_EQUAL(2, layoutChords(p, s, 16, 0, ScaleType::Major, seg));
  TEST_ASSERT_EQUAL(3, seg[1].start);
  TEST_ASSERT_EQUAL(13, seg[1].steps);
}

void test_roman_roots() {
  Progression p;
  ChordSeg seg[kProgMax];
  ChordSpec s = spec4();
  s.voicing = ChordVoicing::Root;
  parseProgression("b7 #4", p);
  layoutChords(p, s, 16, 0, ScaleType::Major, seg);
  TEST_ASSERT_EQUAL(70, seg[0].root);
  TEST_ASSERT_EQUAL(chordValue(kChordMaj, 0), seg[0].chd);
  TEST_ASSERT_EQUAL(66, seg[1].root);
  parseProgression("b6 6", p);
  layoutChords(p, s, 16, 0, ScaleType::Minor, seg);
  TEST_ASSERT_EQUAL(68, seg[0].root);
  TEST_ASSERT_EQUAL(kChordMaj, seg[0].chd);
  TEST_ASSERT_EQUAL(68, seg[1].root);
  TEST_ASSERT_EQUAL(kChordTriad, seg[1].chd);
  // Folded into the octave window: root A, degree 7 (G#) = 48 + 9 + 11 = 68, outside [48, 59].
  parseProgression("7", p);
  s.octave = 3;
  layoutChords(p, s, 16, 9, ScaleType::Major, seg);
  TEST_ASSERT_EQUAL(56, seg[0].root);  // G#3 in [48, 59]
}

void test_voicing_close() {
  // After C E G (60 64 67) the F triad drops an octave: root 53 inv 2 = C F A (60 65 69), distance 3.
  Progression p;
  parseProgression("1 4", p);
  ChordSeg seg[kProgMax];
  layoutChords(p, spec4(), 16, 0, ScaleType::Major, seg);
  TEST_ASSERT_EQUAL(60, seg[0].root);
  TEST_ASSERT_EQUAL(chordValue(kChordTriad, 0), seg[0].chd);
  TEST_ASSERT_EQUAL(53, seg[1].root);
  TEST_ASSERT_EQUAL(chordValue(kChordTriad, 2), seg[1].chd);
  // After F A C (65 69 72): C at 60 inv 1 = E G C (64 67 72); 48 is below base - 7 = 53.
  parseProgression("4 1", p);
  layoutChords(p, spec4(), 16, 0, ScaleType::Major, seg);
  TEST_ASSERT_EQUAL(65, seg[0].root);
  TEST_ASSERT_EQUAL(0, chordInv(seg[0].chd));
  TEST_ASSERT_EQUAL(60, seg[1].root);
  TEST_ASSERT_EQUAL(chordValue(kChordTriad, 1), seg[1].chd);
  // ROOT voicing: inv 0 at the original octave.
  ChordSpec s = spec4();
  s.voicing = ChordVoicing::Root;
  parseProgression("1 4", p);
  layoutChords(p, s, 16, 0, ScaleType::Major, seg);
  TEST_ASSERT_EQUAL(65, seg[1].root);
  TEST_ASSERT_EQUAL(chordValue(kChordTriad, 0), seg[1].chd);
}

void test_random_progression() {
  Progression a, b;
  randomProgression(6, 7, ScaleType::Major, a);
  randomProgression(6, 7, ScaleType::Major, b);
  TEST_ASSERT_EQUAL(6, a.n);
  TEST_ASSERT_EQUAL(0, memcmp(&a, &b, sizeof(a)));
  bool differs = false;
  for (uint32_t seed = 1; seed < 50; ++seed) {
    for (ScaleType sc : {ScaleType::Major, ScaleType::Minor}) {
      Progression r;
      randomProgression(8, seed, sc, r);
      TEST_ASSERT_EQUAL(8, r.n);
      TEST_ASSERT_EQUAL(0, r.c[0].degree);
      const int last = r.c[7].degree;
      if (sc == ScaleType::Major) TEST_ASSERT_TRUE(last == 4 || last == 3);
      else TEST_ASSERT_TRUE(last == 4 || last == 3 || last == 6);
      for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL(0, r.c[i].shift);
        TEST_ASSERT_EQUAL(-1, r.c[i].type);
        if (sc == ScaleType::Major) TEST_ASSERT_TRUE(r.c[i].degree <= 5);
        if (i) TEST_ASSERT_NOT_EQUAL(r.c[i - 1].degree, r.c[i].degree);
      }
    }
    Progression r;
    randomProgression(8, seed, ScaleType::Major, r);
    differs = differs || memcmp(&r, &a, sizeof(r)) != 0;
  }
  TEST_ASSERT_TRUE(differs);
  // RANDOM in the spec replaces the given progression.
  ChordSpec s = spec4();
  s.random = true;
  s.chords = 3;
  ChordSeg seg[kProgMax];
  TEST_ASSERT_EQUAL(3, layoutChords(a, s, 16, 0, ScaleType::Major, seg));
}

void test_apply_hold() {
  gPat = Pattern{};
  Progression p;
  parseProgression("1 4", p);
  ArpPattern r;
  parseArpPattern(chordRhythmText(0), r);  // HOLD
  ChordSpec s;
  s.length = 8;
  s.dest = 2;
  s.voicing = ChordVoicing::Root;
  TEST_ASSERT_TRUE(applyChords(gPat, Sel{0, 0, 0, 15}, s, p, r, 0, ScaleType::Major));
  const Step* t = gPat.steps[2];
  TEST_ASSERT_EQUAL(60, t[0].note);
  TEST_ASSERT_EQUAL(kChordTriad, t[0].find(Fx::CHD)->val);
  TEST_ASSERT_NOT_NULL(t[0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteEmpty, t[7].note);  // no OFF: the next chord follows
  TEST_ASSERT_EQUAL(65, t[8].note);
  TEST_ASSERT_EQUAL(kChordTriad, t[8].find(Fx::CHD)->val);
  TEST_ASSERT_NULL(t[8].find(Fx::TIE));
  TEST_ASSERT_EQUAL(gateValue(800), t[8].find(Fx::GAT)->val);
  for (int i = 9; i < 16; ++i) TEST_ASSERT_EQUAL(kNoteEmpty, t[i].note);
}

void test_apply_hold_long_chord() {
  // HOLD over one 32-step chord: no re-hit when the 16-step rhythm wraps.
  gPat = Pattern{};
  gPat.length = 64;
  Progression p;
  parseProgression("1 4", p);
  ArpPattern r;
  parseArpPattern(chordRhythmText(0), r);
  ChordSpec s;
  s.length = 32;
  s.voicing = ChordVoicing::Root;
  TEST_ASSERT_TRUE(applyChords(gPat, Sel{0, 0, 0, 63}, s, p, r, 0, ScaleType::Major));
  const Step* t = gPat.steps[0];
  TEST_ASSERT_EQUAL(60, t[0].note);
  TEST_ASSERT_NOT_NULL(t[0].find(Fx::TIE));  // held until the next chord replaces it
  for (int i = 1; i < 32; ++i) TEST_ASSERT_EQUAL_MESSAGE(kNoteEmpty, t[i].note, "re-hit inside the chord");
  TEST_ASSERT_EQUAL(65, t[32].note);
  TEST_ASSERT_NULL(t[32].find(Fx::TIE));  // tie to the range end: a long GAT
  TEST_ASSERT_EQUAL(gateValue(800), t[32].find(Fx::GAT)->val);
  for (int i = 33; i < 64; ++i) TEST_ASSERT_EQUAL(kNoteEmpty, t[i].note);
  // Other rhythms keep wrapping: HALF hits at 0 and 8, then again at 16 and 24.
  gPat = Pattern{};
  gPat.length = 32;
  parseArpPattern(chordRhythmText(1), r);  // HALF
  parseProgression("1", p);
  TEST_ASSERT_TRUE(applyChords(gPat, Sel{0, 0, 0, 31}, s, p, r, 0, ScaleType::Major));
  for (int i : {0, 8, 16, 24}) TEST_ASSERT_EQUAL(60, t[i].note);
}

void test_apply_quarter_and_off() {
  gPat = Pattern{};
  Progression p;
  parseProgression("1", p);
  ArpPattern r;
  parseArpPattern(chordRhythmText(2), r);  // QUARTER
  ChordSpec s;
  TEST_ASSERT_TRUE(applyChords(gPat, Sel{0, 0, 0, 15}, s, p, r, 0, ScaleType::Major));
  const Step* t = gPat.steps[0];
  for (int i = 0; i < 16; ++i) {
    if (i % 4 == 0) {
      TEST_ASSERT_EQUAL(60, t[i].note);
      TEST_ASSERT_EQUAL((60 + 110) / 2, t[i].vel);
      TEST_ASSERT_EQUAL(gateValue(50), t[i].find(Fx::GAT)->val);
    } else {
      TEST_ASSERT_EQUAL(kNoteEmpty, t[i].note);
    }
  }
  // A rest after a tie puts OFF; '^' lifts the chord an octave.
  gPat = Pattern{};
  parseArpPattern("x - . x^", r);
  TEST_ASSERT_TRUE(applyChords(gPat, Sel{0, 0, 0, 3}, s, p, r, 0, ScaleType::Major));
  TEST_ASSERT_NOT_NULL(t[0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteOff, t[2].note);
  TEST_ASSERT_EQUAL(72, t[3].note);
}

void test_apply_keeps_fx_and_drums() {
  gPat = Pattern{};
  gPat.steps[1][4].fx[0] = FxSlot{Fx::FLT, 33};
  gPat.steps[1][5].fx[0] = FxSlot{Fx::RAT, 3};  // a generator fx: cleared
  Progression p;
  parseProgression("1 5", p);
  ArpPattern r;
  parseArpPattern(chordRhythmText(2), r);
  ChordSpec s;
  s.dest = 1;
  TEST_ASSERT_TRUE(applyChords(gPat, Sel{0, 1, 0, 15}, s, p, r, 0, ScaleType::Major));
  TEST_ASSERT_EQUAL(33, gPat.steps[1][4].find(Fx::FLT)->val);
  TEST_ASSERT_NULL(gPat.steps[1][5].find(Fx::RAT));
  bool drums[kTracks] = {};
  drums[3] = true;
  s.dest = 3;
  TEST_ASSERT_FALSE(applyChords(gPat, Sel{0, 3, 0, 15}, s, p, r, 0, ScaleType::Major, drums));
  for (int i = 0; i < 16; ++i) TEST_ASSERT_TRUE(gPat.steps[3][i].isEmpty());
}

void test_capture_round_trip() {
  gPat = Pattern{};
  Progression p, q;
  parseProgression("1 4 5 1", p);
  ArpPattern r;
  parseArpPattern(chordRhythmText(0), r);
  ChordSpec s = spec4();
  TEST_ASSERT_TRUE(applyChords(gPat, Sel{0, 0, 0, 15}, s, p, r, 0, ScaleType::Major));
  TEST_ASSERT_TRUE(captureChords(gPat, 0, 0, 15, 0, ScaleType::Major, q));
  TEST_ASSERT_TRUE(q.steps);
  TEST_ASSERT_EQUAL(4, q.n);
  const int deg[] = {0, 3, 4, 0};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(deg[i], q.c[i].degree);
    TEST_ASSERT_EQUAL(0, q.c[i].shift);
    TEST_ASSERT_EQUAL(kChordTriad, q.c[i].type);
    TEST_ASSERT_EQUAL(4, q.c[i].len);
  }
  // Out of the scale: Roman numerals (flats first); a note without CHD gets MAJ / MIN by the scale.
  gPat = Pattern{};
  gPat.steps[0][2].note = 70;  // Bb in C major
  gPat.steps[0][6].note = 62;  // D: the scale's third above is F, minor
  TEST_ASSERT_TRUE(captureChords(gPat, 0, 0, 7, 0, ScaleType::Major, q));
  TEST_ASSERT_EQUAL(2, q.n);
  TEST_ASSERT_EQUAL(6, q.c[0].degree);
  TEST_ASSERT_EQUAL(-1, q.c[0].shift);
  TEST_ASSERT_EQUAL(kChordMaj, q.c[0].type);
  TEST_ASSERT_EQUAL(4, q.c[0].len);
  TEST_ASSERT_EQUAL(1, q.c[1].degree);
  TEST_ASSERT_EQUAL(kChordMin, q.c[1].type);
  TEST_ASSERT_EQUAL(2, q.c[1].len);
  char buf[kProgTextMax];
  formatProgression(q, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("@steps b7MAJ*4 2MIN*2", buf);
  TEST_ASSERT_FALSE(captureChords(gPat, 0, 8, 15, 0, ScaleType::Major, q));
  // An out-of-scale natural: A in C minor is n6 (not #5 / b7), and lays out back on A.
  gPat = Pattern{};
  gPat.steps[0][0].note = 69;
  TEST_ASSERT_TRUE(captureChords(gPat, 0, 0, 3, 0, ScaleType::Minor, q));
  TEST_ASSERT_TRUE(q.c[0].roman);
  TEST_ASSERT_EQUAL(0, q.c[0].shift);
  TEST_ASSERT_EQUAL(5, q.c[0].degree);
  formatProgression(q, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("@steps n6MAJ*4", buf);
  ChordSeg seg[kProgMax];
  TEST_ASSERT_EQUAL(1, layoutChords(q, ChordSpec{}, 4, 0, ScaleType::Minor, seg));
  TEST_ASSERT_EQUAL(69 % 12, seg[0].root % 12);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_tokens);
  RUN_TEST(test_steps_and_format);
  RUN_TEST(test_factory_parses);
  RUN_TEST(test_layout_length);
  RUN_TEST(test_layout_auto);
  RUN_TEST(test_roman_roots);
  RUN_TEST(test_voicing_close);
  RUN_TEST(test_random_progression);
  RUN_TEST(test_apply_hold);
  RUN_TEST(test_apply_hold_long_chord);
  RUN_TEST(test_apply_quarter_and_off);
  RUN_TEST(test_apply_keeps_fx_and_drums);
  RUN_TEST(test_capture_round_trip);
  return UNITY_END();
}
