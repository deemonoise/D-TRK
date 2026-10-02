#include <unity.h>
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_step_is_six_bytes_and_empty_by_default() {
  TEST_ASSERT_EQUAL(6, sizeof(Step));
  Step s;
  TEST_ASSERT_TRUE(s.isEmpty());
  TEST_ASSERT_FALSE(s.hasNote());
  s.note = 60;
  TEST_ASSERT_TRUE(s.hasNote());
  s.note = kNoteOff;
  TEST_ASSERT_FALSE(s.hasNote());
}

void test_find_fx() {
  Step s;
  s.fx[1] = {Fx::RAT, 4};
  TEST_ASSERT_NULL(s.find(Fx::PRB));
  TEST_ASSERT_NOT_NULL(s.find(Fx::RAT));
  TEST_ASSERT_EQUAL(4, s.find(Fx::RAT)->val);
}

void test_ticks_per_step() {
  TEST_ASSERT_EQUAL(96, ticksPerStep(Resolution::Quarter));
  TEST_ASSERT_EQUAL(24, ticksPerStep(Resolution::Sixteenth));
  TEST_ASSERT_EQUAL(12, ticksPerStep(Resolution::ThirtySecond));
  TEST_ASSERT_EQUAL(32, ticksPerStep(Resolution::EighthTriplet));
  TEST_ASSERT_EQUAL(16, ticksPerStep(Resolution::SixteenthTriplet));
}

void test_gate_percent_encoding() {
  TEST_ASSERT_EQUAL(1, gatePercent(1));
  TEST_ASSERT_EQUAL(100, gatePercent(100));
  TEST_ASSERT_EQUAL(107, gatePercent(101));
  TEST_ASSERT_EQUAL(800, gatePercent(200));
  TEST_ASSERT_EQUAL(800, gatePercent(255));
}

void test_project_reset_defaults() {
  Project* p = new Project();
  TEST_ASSERT_EQUAL(120, p->bpm);
  for (int i = 0; i < kTracks; ++i) TEST_ASSERT_EQUAL(i, p->tracks[i].channel);
  TEST_ASSERT_EQUAL_STRING("TRK1", p->tracks[0].name);
  TEST_ASSERT_EQUAL(16, p->patterns[3].length);
  TEST_ASSERT_TRUE(p->patterns[3].isEmpty());
  delete p;
}

void test_project_reset_clears_song_mode() {
  Project* p = new Project();
  TEST_ASSERT_FALSE(p->songMode);
  p->songMode = true;
  p->reset();
  TEST_ASSERT_FALSE(p->songMode);
  delete p;
}

void test_track_audible_mute_solo() {
  Project* p = new Project();
  TEST_ASSERT_TRUE(p->trackAudible(0));
  p->tracks[0].mute = true;
  TEST_ASSERT_FALSE(p->trackAudible(0));
  p->tracks[2].solo = true;
  TEST_ASSERT_TRUE(p->trackAudible(2));
  TEST_ASSERT_FALSE(p->trackAudible(1));
  delete p;
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_step_is_six_bytes_and_empty_by_default);
  RUN_TEST(test_find_fx);
  RUN_TEST(test_ticks_per_step);
  RUN_TEST(test_gate_percent_encoding);
  RUN_TEST(test_project_reset_defaults);
  RUN_TEST(test_project_reset_clears_song_mode);
  RUN_TEST(test_track_audible_mute_solo);
  return UNITY_END();
}
