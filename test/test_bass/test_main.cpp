#include <string.h>
#include <unity.h>
#include "bass_gen.h"
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static bool samePattern(const BassPattern& a, const BassPattern& b) {
  if (a.len != b.len) return false;
  for (int i = 0; i < a.len; ++i)
    if (memcmp(&a.steps[i], &b.steps[i], sizeof(ArpStep)) != 0 || a.role[i] != b.role[i] || a.oct[i] != b.oct[i])
      return false;
  return true;
}

static BassPattern pat(const char* t) {
  BassPattern p;
  TEST_ASSERT_TRUE(parseBassPattern(t, p));
  return p;
}

void test_parse_format_round_trip() {
  BassPattern p = pat("X5^ xa xw~ xr . - x7v:90:40");
  TEST_ASSERT_EQUAL(7, p.len);
  TEST_ASSERT_EQUAL(ArpAcc::Accent, p.steps[0].acc);
  TEST_ASSERT_EQUAL(BassRole::Fifth, p.role[0]);
  TEST_ASSERT_EQUAL(1, p.oct[0]);
  TEST_ASSERT_EQUAL(BassRole::Approach, p.role[1]);
  TEST_ASSERT_EQUAL(BassRole::Walk, p.role[2]);
  TEST_ASSERT_TRUE(p.steps[2].slide);
  TEST_ASSERT_EQUAL(BassRole::Repeat, p.role[3]);
  TEST_ASSERT_EQUAL(ArpKind::Rest, p.steps[4].kind);
  TEST_ASSERT_EQUAL(ArpKind::Tie, p.steps[5].kind);
  TEST_ASSERT_EQUAL(BassRole::Seventh, p.role[6]);
  TEST_ASSERT_EQUAL(-1, p.oct[6]);
  TEST_ASSERT_EQUAL(90, p.steps[6].velPct);
  TEST_ASSERT_EQUAL(40, p.steps[6].gatePct);
  char buf[kBassTextMax];
  TEST_ASSERT_TRUE(formatBassPattern(p, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("X5^ xa x~w xr . - x7v:90:40", buf);  // canonical: '~' before the role
  BassPattern q = pat(buf);
  TEST_ASSERT_TRUE(samePattern(p, q));
  TEST_ASSERT_FALSE(formatBassPattern(p, buf, 4));
  // Last role / octave wins; ARP-only tokens parse.
  BassPattern r = pat("x35 x^v xp Xs~ o xl");
  TEST_ASSERT_EQUAL(6, r.len);
  TEST_ASSERT_EQUAL(BassRole::Fifth, r.role[0]);
  TEST_ASSERT_EQUAL(-1, r.oct[1]);
  TEST_ASSERT_EQUAL(BassRole::Root, r.role[2]);
  TEST_ASSERT_EQUAL(ArpLen::Short, r.steps[3].len);
  TEST_ASSERT_FALSE(parseBassPattern("  ", r));
}

void test_factory_patterns_parse() {
  TEST_ASSERT_TRUE(bassFactoryCount() >= 48);
  for (int i = 0; i < bassFactoryCount(); ++i) {
    BassPattern p;
    TEST_ASSERT_NOT_NULL(bassFactoryName(i));
    TEST_ASSERT_TRUE(strlen(bassFactoryName(i)) <= 10);
    TEST_ASSERT_TRUE_MESSAGE(parseBassPattern(bassFactoryText(i), p), bassFactoryName(i));
    TEST_ASSERT_EQUAL_MESSAGE(16, p.len, bassFactoryName(i));
  }
  TEST_ASSERT_NULL(bassFactoryName(bassFactoryCount()));
  TEST_ASSERT_NULL(bassFactoryText(-1));
  TEST_ASSERT_EQUAL_STRING("PROGRESSION", bassSourceName(BassSource::Progression));
}

static Pattern S, D;

static void reset() {
  S.clear();
  S.length = 16;
  D = S;
}

static BassSpec spec() { return BassSpec{}; }

void test_root_source_chord_tones() {
  reset();
  BassSpec b = spec();  // root 36, octave 2
  TEST_ASSERT_TRUE(applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x x3 x5 x7 x^ xv"), 0, ScaleType::Major, nullptr, 0));
  const int want[] = {36, 40, 43, 47, 48, 24};
  for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL(want[i], D.steps[0][i].note);
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::CHD));
  // The root is folded into the octave.
  reset();
  b.root = 62;  // D4 -> D2
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x"), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(38, D.steps[0][0].note);
}

void test_progression_approach() {
  reset();
  BassSpec b = spec();
  b.source = BassSource::Progression;
  const ChordSeg segs[] = {{60, kChordTriad, 0, 8}, {65, kChordTriad, 8, 8}};  // C, F (folded to octave 2)
  TEST_ASSERT_TRUE(
      applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x . . . . . . xa"), 0, ScaleType::Major, segs, 2));
  TEST_ASSERT_EQUAL(36, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(40, D.steps[0][7].note);   // F 41 - 1
  TEST_ASSERT_EQUAL(41, D.steps[0][8].note);
  TEST_ASSERT_EQUAL(35, D.steps[0][15].note);  // wraps: C 36 - 1
  TEST_ASSERT_FALSE(applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x"), 0, ScaleType::Major, nullptr, 0));
}

void test_walk_toward_next_root() {
  reset();
  BassSpec b = spec();
  b.source = BassSource::Progression;
  const ChordSeg segs[] = {{36, kChordTriad, 0, 8}, {41, kChordTriad, 8, 8}};
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x . . . . . . xw"), 0, ScaleType::Major, segs, 2);
  TEST_ASSERT_EQUAL(38, D.steps[0][7].note);  // moveDegrees(36, +1)
  TEST_ASSERT_EQUAL(40, D.steps[0][15].note);  // from F 41 one degree down toward the first root C
}

void test_selection_follows_and_off() {
  reset();
  S.steps[0][0].note = 48;  // C3 + triad
  S.steps[0][0].fx[0] = FxSlot{Fx::CHD, kChordTriad};
  S.steps[0][8].note = 45;  // A2
  S.steps[0][12].note = kNoteOff;
  D = S;
  BassSpec b = spec();
  b.source = BassSource::Selection;
  b.dest = 1;
  TEST_ASSERT_TRUE(applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x . . . x3 . . xa x - - - x - - -"), 0,
                             ScaleType::Major, nullptr, 0));
  TEST_ASSERT_EQUAL(36, D.steps[1][0].note);
  TEST_ASSERT_EQUAL(40, D.steps[1][4].note);
  TEST_ASSERT_EQUAL(44, D.steps[1][7].note);  // A2 45 - 1
  TEST_ASSERT_EQUAL(45, D.steps[1][8].note);
  TEST_ASSERT_NOT_NULL(D.steps[1][8].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteOff, D.steps[1][12].note);  // released while tied
  for (int i = 13; i < 16; ++i) TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][i].note);
  reset();
  S.steps[0][0].note = 48;
  S.steps[0][12].note = kNoteOff;
  D = S;
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x . . ."), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(36, D.steps[1][8].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][12].note);  // not tied: nothing
}

void test_rate_rotate_swing_slide_tie() {
  reset();
  BassSpec b = spec();
  b.rate = 2;
  b.swing = 40;
  b.slide = 20;
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x x3 x5~ -"), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(36, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(40, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(100, D.steps[0][0].find(Fx::GAT)->val);  // gate 50 x rate 2
  TEST_ASSERT_EQUAL(20, fxSigned(D.steps[0][2].find(Fx::NDG)->val));  // odd step: swing / 2
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::NDG));
  TEST_ASSERT_EQUAL(43, D.steps[0][4].note);
  TEST_ASSERT_EQUAL(20, D.steps[0][4].find(Fx::SLD)->val);
  TEST_ASSERT_EQUAL(gateValue(210), D.steps[0][2].find(Fx::GAT)->val);  // held into the slide
  TEST_ASSERT_NOT_NULL(D.steps[0][4].find(Fx::TIE));
  TEST_ASSERT_EQUAL(36, D.steps[0][8].note);
  // The last tie runs to the range end: a long gate instead.
  TEST_ASSERT_NULL(D.steps[0][12].find(Fx::TIE));
  TEST_ASSERT_EQUAL(gateValue(400), D.steps[0][12].find(Fx::GAT)->val);
  reset();
  b = spec();
  b.rotate = 1;
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x x3"), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(40, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(36, D.steps[0][1].note);
}

void test_mutate_seeded() {
  const BassPattern p = pat("x x3 x5 x7 x x3 x5 x7 x x3 x5 x7 x x3 x5 x7");
  BassSpec b = spec();
  reset();
  applyBass(S, D, makeSel(0, 0, 0, 15), b, p, 0, ScaleType::Major, nullptr, 0);
  const Pattern plain = D;
  b.seed = 99;  // no mutate: the seed changes nothing
  reset();
  applyBass(S, D, makeSel(0, 0, 0, 15), b, p, 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(0, memcmp(plain.steps[0], D.steps[0], sizeof(D.steps[0])));
  b.mutate = 60;
  reset();
  applyBass(S, D, makeSel(0, 0, 0, 15), b, p, 0, ScaleType::Major, nullptr, 0);
  const Pattern first = D;
  reset();
  applyBass(S, D, makeSel(0, 0, 0, 15), b, p, 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(0, memcmp(first.steps[0], D.steps[0], sizeof(D.steps[0])));
  TEST_ASSERT_TRUE(memcmp(plain.steps[0], D.steps[0], sizeof(D.steps[0])) != 0);
  for (int i = 0; i < 16; ++i) {
    const int n = D.steps[0][i].note;
    TEST_ASSERT_TRUE(n == kNoteEmpty || n == 36 || n == 40 || n == 43 || n == 47 || n == 48);
  }
}

void test_capture_round_trip() {
  reset();
  BassSpec b = spec();
  const BassPattern p = pat("X x3 . x5^ - . xv~ x7 o . x5s xl x3v . X5 x");
  applyBass(S, D, makeSel(0, 0, 0, 15), b, p, 0, ScaleType::Major, nullptr, 0);
  BassPattern c;
  TEST_ASSERT_TRUE(captureBass(D, 0, 0, 15, b, 0, ScaleType::Major, nullptr, 0, nullptr, nullptr, c));
  TEST_ASSERT_EQUAL(16, c.len);
  for (int i = 0; i < 16; ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].kind, c.steps[i].kind, "kind");
    if (p.steps[i].kind != ArpKind::Note) continue;
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].acc, c.steps[i].acc, "acc");
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].len, c.steps[i].len, "len");
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].slide, c.steps[i].slide, "slide");
    TEST_ASSERT_EQUAL_MESSAGE(p.role[i], c.role[i], "role");
    TEST_ASSERT_EQUAL_MESSAGE(p.oct[i], c.oct[i], "oct");
  }
  // Approach / repeat / nearest against a progression.
  reset();
  b.source = BassSource::Progression;
  const ChordSeg segs[] = {{36, kChordTriad, 0, 8}, {41, kChordTriad, 8, 8}};
  D.steps[0][0].note = 38;  // D: no chord tone of C -> nearest (root C, 2 below; E 2 above: root first)
  D.steps[0][1].note = 38;  // same again -> repeat
  D.steps[0][7].note = 40;  // E is C's third (pitch class first)
  D.steps[0][8].note = 40;  // under F (next root wraps to C 36): F's 7th? E = moveDegrees(41, 6) -> 7th
  D.steps[0][15].note = 35;  // B: C - 1 = approach (B is no tone of F: F A C E)
  TEST_ASSERT_TRUE(captureBass(D, 0, 0, 15, b, 0, ScaleType::Major, segs, 2, nullptr, nullptr, c));
  TEST_ASSERT_EQUAL(BassRole::Root, c.role[0]);
  TEST_ASSERT_EQUAL(BassRole::Repeat, c.role[1]);
  TEST_ASSERT_EQUAL(BassRole::Third, c.role[7]);
  TEST_ASSERT_EQUAL(BassRole::Seventh, c.role[8]);
  TEST_ASSERT_EQUAL(BassRole::Approach, c.role[15]);
  TEST_ASSERT_FALSE(captureBass(S, 0, 0, 15, b, 0, ScaleType::Major, segs, 2, nullptr, nullptr, c));
}

void test_mutate_redraws_roles() {
  reset();
  BassSpec b = spec();
  b.mutate = 60;
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x7 x7 x7 x7 x7 x7 x7 x7 x7 x7 x7 x7 x7 x7 x7 x7"), 0,
            ScaleType::Major, nullptr, 0);
  bool seventh = false, other = false;
  for (int i = 0; i < 16; ++i) {
    const int n = D.steps[0][i].note;
    if (n == 47) seventh = true;
    if (n == 36 || n == 40 || n == 43 || n == 48) other = true;
  }
  TEST_ASSERT_TRUE(seventh);
  TEST_ASSERT_TRUE(other);
}

void test_walk_clamps_at_next_root() {
  reset();
  BassSpec b = spec();
  b.source = BassSource::Progression;
  const ChordSeg segs[] = {{36, kChordTriad, 0, 8}, {39, kChordMaj, 8, 8}};  // C, Eb (off the C major scale)
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x . . . . . xw xw"), 0, ScaleType::Major, segs, 2);
  TEST_ASSERT_EQUAL(38, D.steps[0][6].note);
  TEST_ASSERT_EQUAL(39, D.steps[0][7].note);  // D 38 + one degree = E 40 passes Eb: clamped
}

void test_selection_next_root_past_range() {
  reset();
  S.steps[0][0].note = 48;   // C
  S.steps[0][12].note = 53;  // F, after the range
  D = S;
  BassSpec b = spec();
  b.source = BassSource::Selection;
  b.dest = 1;
  applyBass(S, D, makeSel(0, 0, 0, 7), b, pat("x . . . . . . xa"), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(36, D.steps[1][0].note);
  TEST_ASSERT_EQUAL(40, D.steps[1][7].note);  // F 41 - 1
}

void test_selection_same_chord_is_no_change() {
  reset();
  S.steps[0][0].note = 48;
  S.steps[0][8].note = 48;  // C again: no change
  S.steps[0][12].note = 53;
  D = S;
  BassSpec b = spec();
  b.source = BassSource::Selection;
  b.dest = 1;
  applyBass(S, D, makeSel(0, 0, 0, 15), b, pat("x . . . . . . xa"), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(40, D.steps[1][7].note);   // the next different chord F, not C again
  TEST_ASSERT_EQUAL(35, D.steps[1][15].note);  // after F: wraps to C
}

void test_selection_skips_drum_tracks() {
  reset();
  S.steps[0][0].note = 33;  // drum track: never a source
  S.steps[1][0].note = 48;
  D = S;
  bool drum[kTracks] = {};
  drum[0] = true;
  BassSpec b = spec();
  b.source = BassSource::Selection;
  b.dest = 2;
  const Sel sel = makeSel(0, 0, 1, 15);
  TEST_ASSERT_TRUE(applyBass(S, D, sel, b, pat("x"), 0, ScaleType::Major, nullptr, 0, drum));
  TEST_ASSERT_EQUAL(36, D.steps[2][0].note);
  BassPattern c;
  TEST_ASSERT_TRUE(captureBass(D, 2, 0, 15, b, 0, ScaleType::Major, nullptr, 0, &S, &sel, c, drum));
  TEST_ASSERT_EQUAL(BassRole::Root, c.role[0]);
  TEST_ASSERT_EQUAL(0, c.oct[0]);
}

void test_short_ranges() {
  reset();
  BassSpec b = spec();
  applyBass(S, D, makeSel(0, 0, 0, 0), b, pat("xa x"), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(35, D.steps[0][0].note);  // next root wraps to the range's own chord
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  reset();
  b.rate = 4;
  applyBass(S, D, makeSel(0, 0, 0, 2), b, pat("x -"), 0, ScaleType::Major, nullptr, 0);
  TEST_ASSERT_EQUAL(36, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][4].note);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_format_round_trip);
  RUN_TEST(test_factory_patterns_parse);
  RUN_TEST(test_root_source_chord_tones);
  RUN_TEST(test_progression_approach);
  RUN_TEST(test_walk_toward_next_root);
  RUN_TEST(test_selection_follows_and_off);
  RUN_TEST(test_rate_rotate_swing_slide_tie);
  RUN_TEST(test_mutate_seeded);
  RUN_TEST(test_capture_round_trip);
  RUN_TEST(test_mutate_redraws_roles);
  RUN_TEST(test_walk_clamps_at_next_root);
  RUN_TEST(test_selection_next_root_past_range);
  RUN_TEST(test_selection_same_chord_is_no_change);
  RUN_TEST(test_selection_skips_drum_tracks);
  RUN_TEST(test_short_ranges);
  return UNITY_END();
}
