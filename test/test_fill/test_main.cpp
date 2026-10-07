#include <unity.h>
#include "fill.h"
#include "model.h"
#include "scale.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Pattern P;

static void resetP() {
  P.clear();
  P.length = 16;
}

static Sel track0() { return makeSel(0, 0, 0, 15); }

void test_every_with_offset() {
  FillSpec f;
  f.every = 4;
  f.offset = 1;
  bool h[16];
  fillHits(f, 16, h);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(i % 4 == 1, h[i]);
}

void test_euclid_hits_repeat() {
  FillSpec f;
  f.where = FillWhere::Euclid;
  f.hits = 2;
  f.length = 5;  // x.x..
  bool h[16];
  fillHits(f, 16, h);
  const char* want = "x.x..x.x..x.x..x";
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(want[i] == 'x', h[i]);
}

void test_random_density_and_seed() {
  FillSpec f;
  f.where = FillWhere::Random;
  f.density = 0;
  bool h[128], g[128];
  fillHits(f, 128, h);
  for (bool b : h) TEST_ASSERT_FALSE(b);
  f.density = 100;
  fillHits(f, 128, h);
  for (bool b : h) TEST_ASSERT_TRUE(b);
  f.density = 50;
  f.seed = 7;
  fillHits(f, 128, h);
  fillHits(f, 128, g);
  int n = 0;
  for (int i = 0; i < 128; ++i) {
    TEST_ASSERT_EQUAL(h[i], g[i]);
    n += h[i];
  }
  TEST_ASSERT_TRUE(n > 40 && n < 90);
}

void test_const_note_keeps_others() {
  resetP();
  P.steps[0][1].note = 50;
  FillSpec f;
  f.every = 2;
  f.from = 64;
  applyFill(P, track0(), f, 0, ScaleType::Chromatic);
  for (int i = 0; i < 16; i += 2) TEST_ASSERT_EQUAL(64, P.steps[0][i].note);
  TEST_ASSERT_EQUAL(50, P.steps[0][1].note);
  TEST_ASSERT_FALSE(P.steps[0][3].hasNote());
}

void test_ramp_notes_in_scale() {
  resetP();
  FillSpec f;
  f.every = 1;
  f.value = FillValue::Ramp;
  f.from = 60;
  f.to = 72;  // C major: 8 notes over 16 steps
  applyFill(P, makeSel(0, 0, 0, 7), f, 0, ScaleType::Major);
  const uint8_t want[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(want[i], P.steps[0][i].note);
  TEST_ASSERT_FALSE(P.steps[0][8].hasNote());
  // Descending.
  resetP();
  f.from = 72;
  f.to = 60;
  applyFill(P, makeSel(0, 0, 0, 7), f, 0, ScaleType::Major);
  for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(want[7 - i], P.steps[0][i].note);
}

void test_random_notes_in_scale_and_range() {
  resetP();
  FillSpec f;
  f.every = 1;
  f.value = FillValue::Random;
  f.from = 48;
  f.to = 71;
  f.seed = 3;
  applyFill(P, track0(), f, 2, ScaleType::Minor);
  for (int i = 0; i < 16; ++i) {
    const uint8_t n = P.steps[0][i].note;
    TEST_ASSERT_TRUE(n >= 48 && n <= 71);
    TEST_ASSERT_TRUE(inScale(n, 2, ScaleType::Minor));
  }
}

void test_modes_for_notes() {
  resetP();
  P.steps[0][0].note = 40;
  P.steps[0][4].note = kNoteOff;
  FillSpec f;
  f.every = 4;
  f.from = 60;
  f.mode = FillMode::Empty;
  applyFill(P, track0(), f, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(40, P.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteOff, P.steps[0][4].note);
  TEST_ASSERT_EQUAL(60, P.steps[0][8].note);
  resetP();
  P.steps[0][0].note = 40;
  f.mode = FillMode::Notes;
  applyFill(P, track0(), f, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(60, P.steps[0][0].note);
  TEST_ASSERT_FALSE(P.steps[0][8].hasNote());
}

void test_velocity_ramp_needs_notes() {
  resetP();
  for (int i = 0; i < 16; i += 2) P.steps[0][i].note = 60;
  FillSpec f;
  f.every = 1;
  f.target = FillTarget::Vel;
  f.value = FillValue::Ramp;
  f.from = 10;
  f.to = 160;  // clamped to 127
  applyFill(P, track0(), f, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(10, P.steps[0][0].vel);
  TEST_ASSERT_EQUAL(127, P.steps[0][14].vel);
  TEST_ASSERT_EQUAL(kVelDefault, P.steps[0][1].vel);
  TEST_ASSERT_FALSE(P.steps[0][1].hasNote());
  TEST_ASSERT_TRUE(P.steps[0][2].vel > P.steps[0][0].vel);
}

void test_fx_slot_ramp_and_empty_mode() {
  resetP();
  P.steps[0][4].fx[1] = FxSlot{Fx::GAT, 33};
  FillSpec f;
  f.every = 4;
  f.target = FillTarget::Fx;
  f.slot = 1;
  f.cmd = Fx::FLT;
  f.value = FillValue::Ramp;
  f.from = 0;
  f.to = 90;
  f.mode = FillMode::Empty;
  applyFill(P, track0(), f, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(Fx::FLT, P.steps[0][0].fx[1].cmd);
  TEST_ASSERT_EQUAL(0, P.steps[0][0].fx[1].val);
  TEST_ASSERT_EQUAL(Fx::GAT, P.steps[0][4].fx[1].cmd);  // occupied: kept
  TEST_ASSERT_EQUAL(60, P.steps[0][8].fx[1].val);
  TEST_ASSERT_EQUAL(90, P.steps[0][12].fx[1].val);
  TEST_ASSERT_EQUAL(Fx::None, P.steps[0][12].fx[0].cmd);
  // None clears the slot.
  f.cmd = Fx::None;
  f.mode = FillMode::Overwrite;
  applyFill(P, track0(), f, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(Fx::None, P.steps[0][4].fx[1].cmd);
}

void test_fx_signed_ramp() {
  resetP();
  FillSpec f;
  f.every = 1;
  f.target = FillTarget::Fx;
  f.cmd = Fx::PBN;
  f.value = FillValue::Ramp;
  f.from = static_cast<uint8_t>(-50);
  f.to = 50;
  applyFill(P, makeSel(0, 0, 0, 2), f, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(-50, static_cast<int8_t>(P.steps[0][0].fx[0].val));
  TEST_ASSERT_EQUAL(0, static_cast<int8_t>(P.steps[0][1].fx[0].val));
  TEST_ASSERT_EQUAL(50, static_cast<int8_t>(P.steps[0][2].fx[0].val));
}

void test_drum_lane_and_velocity() {
  resetP();
  bool drum[kTracks] = {true};
  P.steps[0][4].note = 90;
  P.steps[0][4].vel = 0x01;
  FillSpec f;
  f.every = 4;
  f.lane = 2;
  applyFill(P, track0(), f, 0, ScaleType::Chromatic, drum);
  TEST_ASSERT_EQUAL(0x04, P.steps[0][0].vel);
  TEST_ASSERT_EQUAL(0, P.steps[0][0].note);  // track velocity
  TEST_ASSERT_EQUAL(0x05, P.steps[0][4].vel);
  TEST_ASSERT_EQUAL(90, P.steps[0][4].note);
  f.target = FillTarget::Vel;
  f.from = 110;
  applyFill(P, track0(), f, 0, ScaleType::Chromatic, drum);
  TEST_ASSERT_EQUAL(110, P.steps[0][0].note);
  TEST_ASSERT_EQUAL(0x04, P.steps[0][0].vel);
}

void test_selection_offsets_and_tracks() {
  resetP();
  FillSpec f;
  f.every = 2;
  f.from = 70;
  applyFill(P, makeSel(1, 3, 2, 8), f, 0, ScaleType::Chromatic);
  for (int t = 1; t <= 2; ++t)
    for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(i >= 3 && i <= 8 && (i - 3) % 2 == 0, P.steps[t][i].hasNote());
  for (int i = 0; i < 16; ++i) TEST_ASSERT_FALSE(P.steps[0][i].hasNote());
}

void test_clamps_to_pattern_length() {
  resetP();
  P.length = 8;
  FillSpec f;
  f.every = 1;
  applyFill(P, makeSel(0, 0, 0, 20), f, 0, ScaleType::Chromatic);
  TEST_ASSERT_TRUE(P.steps[0][7].hasNote());
  TEST_ASSERT_FALSE(P.steps[0][8].hasNote());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_with_offset);
  RUN_TEST(test_euclid_hits_repeat);
  RUN_TEST(test_random_density_and_seed);
  RUN_TEST(test_const_note_keeps_others);
  RUN_TEST(test_ramp_notes_in_scale);
  RUN_TEST(test_random_notes_in_scale_and_range);
  RUN_TEST(test_modes_for_notes);
  RUN_TEST(test_velocity_ramp_needs_notes);
  RUN_TEST(test_fx_slot_ramp_and_empty_mode);
  RUN_TEST(test_fx_signed_ramp);
  RUN_TEST(test_drum_lane_and_velocity);
  RUN_TEST(test_selection_offsets_and_tracks);
  RUN_TEST(test_clamps_to_pattern_length);
  return UNITY_END();
}
