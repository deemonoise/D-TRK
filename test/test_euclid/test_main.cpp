#include <unity.h>
#include <string.h>
#include "euclid.h"
#include "model.h"
#include "scale.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Renders euclid(hits, steps, rot) as "x..x" text.
static void pat(int hits, int steps, int rot, char* buf) {
  bool out[128];
  euclid(hits, steps, rot, out);
  for (int i = 0; i < steps; ++i) buf[i] = out[i] ? 'x' : '.';
  buf[steps] = 0;
}

static void expectPat(const char* want, int hits, int steps, int rot = 0) {
  char buf[130];
  pat(hits, steps, rot, buf);
  TEST_ASSERT_EQUAL_STRING(want, buf);
}

void test_references() {
  expectPat("x..x..x.", 3, 8);
  expectPat("x.xx.xx.", 5, 8);
  expectPat("x.x..", 2, 5);
  expectPat("x...x...x...x...", 4, 16);
  expectPat("x..x.x.x..x.x.x.", 7, 16);
  expectPat("x.x.x.x.", 4, 8);
  expectPat("x", 1, 1);
  expectPat("x....", 1, 5);
}

void test_zero_and_full() {
  expectPat("........", 0, 8);
  expectPat("xxxxxxxx", 8, 8);
  expectPat("xxxxx", 9, 5);   // hits clamped to steps
  expectPat(".....", -2, 5);  // negative clamped to 0
}

void test_rotation() {
  expectPat(".x..x..x", 3, 8, 1);
  expectPat(".x..x.x.", 3, 8, -2);  // left by 2
}

void test_rotation_wraps() {
  expectPat("x..x..x.", 3, 8, 8);
  expectPat(".x..x..x", 3, 8, 9);
}

static Pattern P;

static void resetP() {
  P.clear();
  P.length = 16;
}

static EuclidParams baseParams() {
  EuclidParams e;
  e.hits = 2;
  e.length = 5;
  e.baseNote = 36;
  e.vel = 100;
  return e;
}

void test_apply_repeats_length_over_pattern() {
  resetP();
  EuclidParams e = baseParams();  // x.x.. repeated
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  const char* want = "x.x..x.x..x.x..x";
  for (int i = 0; i < 16; ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(want[i] == 'x', P.steps[0][i].hasNote(), "hit mismatch");
    if (want[i] == 'x') {
      TEST_ASSERT_EQUAL(36, P.steps[0][i].note);
      TEST_ASSERT_EQUAL(100, P.steps[0][i].vel);
    }
  }
  // Nothing past the pattern length.
  TEST_ASSERT_TRUE(P.steps[0][16].isEmpty());
  // Other tracks untouched.
  TEST_ASSERT_TRUE(P.steps[1][0].isEmpty());
}

void test_euclid_track_15() {
  resetP();
  EuclidParams e = baseParams();
  e.hits = 4;
  e.length = 16;
  applyEuclid(P, kTracks - 1, e, 0, ScaleType::Chromatic);
  int notes = 0;
  for (int i = 0; i < 16; ++i)
    if (P.steps[kTracks - 1][i].hasNote()) ++notes;
  TEST_ASSERT_EQUAL(4, notes);
  TEST_ASSERT_TRUE(P.steps[kTracks - 1][0].hasNote());
  // Track 14 untouched.
  for (int i = 0; i < 16; ++i) TEST_ASSERT_TRUE(P.steps[kTracks - 2][i].isEmpty());
}

void test_apply_writes_only_within_pattern_length() {
  resetP();
  P.length = 8;
  P.steps[0][10].note = 50;
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  for (int i = 0; i < 8; ++i) TEST_ASSERT_TRUE(P.steps[0][i].hasNote());
  TEST_ASSERT_EQUAL(50, P.steps[0][10].note);
  TEST_ASSERT_TRUE(P.steps[0][8].isEmpty());
}

void test_replace_clears_non_hits() {
  resetP();
  P.steps[0][1].note = 70;
  P.steps[0][1].fx[0] = {Fx::RAT, 3};
  P.steps[0][0].fx[0] = {Fx::PRB, 50};
  EuclidParams e;
  e.hits = 4;
  e.length = 16;
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  TEST_ASSERT_TRUE(P.steps[0][1].isEmpty());
  TEST_ASSERT_EQUAL(36, P.steps[0][0].note);
  TEST_ASSERT_EQUAL(Fx::None, P.steps[0][0].fx[0].cmd);
}

void test_merge_keeps_occupied_steps() {
  resetP();
  P.steps[0][0].note = 70;        // occupied hit position
  P.steps[0][4].fx[0] = {Fx::PRB, 50};  // fx-only step is occupied too
  P.steps[0][2].note = 72;        // non-hit position stays
  EuclidParams e;
  e.hits = 4;
  e.length = 16;
  e.merge = true;
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(70, P.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, P.steps[0][4].note);
  TEST_ASSERT_EQUAL(Fx::PRB, P.steps[0][4].fx[0].cmd);
  TEST_ASSERT_EQUAL(72, P.steps[0][2].note);
  TEST_ASSERT_EQUAL(36, P.steps[0][8].note);
  TEST_ASSERT_EQUAL(36, P.steps[0][12].note);
}

void test_root_snaps_to_scale() {
  resetP();
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  e.baseNote = 61;  // C# in C major -> C
  applyEuclid(P, 0, e, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, P.steps[0][0].note);
  TEST_ASSERT_EQUAL(60, P.steps[0][5].note);
}

void test_up_in_c_major() {
  resetP();
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  e.fill = EuclidFill::Up;
  e.baseNote = 60;
  e.range = 1;
  applyEuclid(P, 0, e, 0, ScaleType::Major);
  const uint8_t want[] = {60, 62, 64, 65, 67, 69, 71, 60, 62};
  for (int i = 0; i < 9; ++i) TEST_ASSERT_EQUAL(want[i], P.steps[0][i].note);
}

void test_up_range_two_octaves() {
  resetP();
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  e.fill = EuclidFill::Up;
  e.baseNote = 60;
  e.range = 2;
  applyEuclid(P, 0, e, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(72, P.steps[0][7].note);
  TEST_ASSERT_EQUAL(83, P.steps[0][13].note);
  TEST_ASSERT_EQUAL(60, P.steps[0][14].note);
}

void test_down_and_updown() {
  resetP();
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  e.fill = EuclidFill::Down;
  e.baseNote = 60;
  applyEuclid(P, 0, e, 0, ScaleType::Major);
  const uint8_t down[] = {60, 59, 57, 55, 53, 52, 50, 60};
  for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(down[i], P.steps[0][i].note);

  e.fill = EuclidFill::UpDown;
  applyEuclid(P, 0, e, 0, ScaleType::Major);
  const uint8_t ud[] = {60, 62, 64, 65, 67, 69, 71, 69, 67, 65, 64, 62, 60, 62};
  for (int i = 0; i < 14; ++i) TEST_ASSERT_EQUAL(ud[i], P.steps[0][i].note);
}

void test_random_is_deterministic_by_seed() {
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  e.fill = EuclidFill::Random;
  e.baseNote = 48;
  e.range = 2;
  e.seed = 1234;
  resetP();
  applyEuclid(P, 0, e, 0, ScaleType::Minor);
  uint8_t a[16];
  for (int i = 0; i < 16; ++i) {
    a[i] = P.steps[0][i].note;
    TEST_ASSERT_TRUE(inScale(a[i], 0, ScaleType::Minor));
    TEST_ASSERT_TRUE(a[i] >= 48 && a[i] < 72);
  }
  resetP();
  applyEuclid(P, 0, e, 0, ScaleType::Minor);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(a[i], P.steps[0][i].note);
  e.seed = 99;
  resetP();
  applyEuclid(P, 0, e, 0, ScaleType::Minor);
  bool differs = false;
  for (int i = 0; i < 16; ++i) differs |= a[i] != P.steps[0][i].note;
  TEST_ASSERT_TRUE(differs);
}

void test_accent_every() {
  resetP();
  EuclidParams e;
  e.hits = 8;
  e.length = 16;  // hits on even steps
  e.vel = 90;
  e.accentVel = 120;
  e.accentEvery = 3;
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  // hit index 0, 3, 6 accented -> steps 0, 6, 12
  const uint8_t want[] = {120, 90, 90, 120, 90, 90, 120, 90};
  for (int k = 0; k < 8; ++k) TEST_ASSERT_EQUAL(want[k], P.steps[0][k * 2].vel);
}

void test_rotation_applies() {
  resetP();
  EuclidParams e;
  e.hits = 3;
  e.length = 8;
  e.rotation = 1;
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  TEST_ASSERT_FALSE(P.steps[0][0].hasNote());
  TEST_ASSERT_TRUE(P.steps[0][1].hasNote());
  TEST_ASSERT_TRUE(P.steps[0][4].hasNote());
  TEST_ASSERT_TRUE(P.steps[0][7].hasNote());
  TEST_ASSERT_TRUE(P.steps[0][9].hasNote());
}

void test_euclid_128_steps() {
  bool out[128];
  euclid(37, 128, 0, out);
  int cnt = 0, last = -1, minGap = 1000, maxGap = 0;
  for (int i = 0; i < 128; ++i)
    if (out[i]) {
      if (last >= 0) {
        const int g = i - last;
        if (g < minGap) minGap = g;
        if (g > maxGap) maxGap = g;
      }
      last = i;
      ++cnt;
    }
  TEST_ASSERT_EQUAL(37, cnt);
  TEST_ASSERT_TRUE(out[0]);
  TEST_ASSERT_EQUAL(3, minGap);
  TEST_ASSERT_EQUAL(4, maxGap);

  euclid(1, 128, 127, out);
  TEST_ASSERT_TRUE(out[127]);
  TEST_ASSERT_FALSE(out[0]);
}

void test_euclid_hits_above_steps() {
  bool out[128];
  euclid(200, 128, 0, out);
  for (int i = 0; i < 128; ++i) TEST_ASSERT_TRUE(out[i]);
  euclid(300, 300, 0, out);  // steps clamped to 128
  for (int i = 0; i < 128; ++i) TEST_ASSERT_TRUE(out[i]);
  expectPat("xxx", 7, 3);
}

void test_base_with_no_lower_scale_note_searches_up() {
  resetP();
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  e.baseNote = 0;  // C, not in D major; nothing below -> C#
  applyEuclid(P, 0, e, 2, ScaleType::Major);
  TEST_ASSERT_EQUAL(1, P.steps[0][0].note);
}

void test_high_base_shifts_down_by_octaves() {
  resetP();
  EuclidParams e;
  e.hits = 16;
  e.length = 16;
  e.fill = EuclidFill::Up;
  e.baseNote = 120;  // 120 + 2 oct > 127 -> base 96
  e.range = 2;
  applyEuclid(P, 0, e, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(96, P.steps[0][0].note);
  TEST_ASSERT_EQUAL(108, P.steps[0][7].note);
  TEST_ASSERT_EQUAL(119, P.steps[0][13].note);
  for (int i = 1; i < 14; ++i) TEST_ASSERT_TRUE(P.steps[0][i].note > P.steps[0][i - 1].note);

  resetP();
  e.fill = EuclidFill::Root;  // root fill keeps the base
  applyEuclid(P, 0, e, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(120, P.steps[0][0].note);
}

void test_lane_writes_only_its_bit() {
  resetP();
  P.steps[0][0].note = 0;
  P.steps[0][0].vel = 0b10;  // lane 2
  P.steps[0][4].note = 90;   // lane 2 at step velocity 90
  P.steps[0][4].vel = 0b10;
  P.steps[0][5].note = 0;    // lane 1 only, not a hit: replace clears it
  P.steps[0][5].vel = 0b01;
  P.steps[0][6].note = 0;    // lane 2 only, not a hit: kept
  P.steps[0][6].vel = 0b10;
  EuclidParams e;
  e.hits = 4;
  e.length = 16;
  e.lane = 0;
  e.accentEvery = 2;
  e.accentVel = 120;
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL_HEX8(0b11, P.steps[0][0].vel);
  TEST_ASSERT_EQUAL(120, P.steps[0][0].note);  // accent (hit 0) on a default-velocity step
  TEST_ASSERT_EQUAL_HEX8(0b11, P.steps[0][4].vel);
  TEST_ASSERT_EQUAL(90, P.steps[0][4].note);   // its own velocity kept
  TEST_ASSERT_EQUAL_HEX8(0b01, P.steps[0][8].vel);
  TEST_ASSERT_EQUAL(120, P.steps[0][8].note);  // hit 2: accent on a new step
  TEST_ASSERT_EQUAL_HEX8(0b01, P.steps[0][12].vel);
  TEST_ASSERT_EQUAL(0, P.steps[0][12].note);   // new step at the track's velocity
  TEST_ASSERT_TRUE(P.steps[0][5].isEmpty());   // its only lane cleared: the step goes
  TEST_ASSERT_EQUAL_HEX8(0b10, P.steps[0][6].vel);
  TEST_ASSERT_TRUE(P.steps[0][1].isEmpty());
}

void test_lane_merge_keeps_other_hits_of_lane() {
  resetP();
  P.steps[0][2].note = 0;
  P.steps[0][2].vel = 0b100;  // lane 3 off the euclid grid
  EuclidParams e;
  e.hits = 4;
  e.length = 16;
  e.lane = 2;
  e.merge = true;
  applyEuclid(P, 0, e, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL_HEX8(0b100, P.steps[0][2].vel);
  TEST_ASSERT_EQUAL_HEX8(0b100, P.steps[0][0].vel);
  TEST_ASSERT_EQUAL_HEX8(0b100, P.steps[0][12].vel);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_references);
  RUN_TEST(test_zero_and_full);
  RUN_TEST(test_rotation);
  RUN_TEST(test_rotation_wraps);
  RUN_TEST(test_apply_repeats_length_over_pattern);
  RUN_TEST(test_apply_writes_only_within_pattern_length);
  RUN_TEST(test_replace_clears_non_hits);
  RUN_TEST(test_merge_keeps_occupied_steps);
  RUN_TEST(test_root_snaps_to_scale);
  RUN_TEST(test_up_in_c_major);
  RUN_TEST(test_up_range_two_octaves);
  RUN_TEST(test_down_and_updown);
  RUN_TEST(test_random_is_deterministic_by_seed);
  RUN_TEST(test_accent_every);
  RUN_TEST(test_euclid_track_15);
  RUN_TEST(test_rotation_applies);
  RUN_TEST(test_euclid_128_steps);
  RUN_TEST(test_euclid_hits_above_steps);
  RUN_TEST(test_base_with_no_lower_scale_note_searches_up);
  RUN_TEST(test_high_base_shifts_down_by_octaves);
  RUN_TEST(test_lane_writes_only_its_bit);
  RUN_TEST(test_lane_merge_keeps_other_hits_of_lane);
  return UNITY_END();
}
