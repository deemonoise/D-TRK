#include "automation.h"
#include "fx_info.h"
#include "step_expand.h"
#include <string.h>
#include <unity.h>
using namespace mt;
static Step steps[kMaxSteps];
void setUp() {
  for (auto &s : steps)
    s = Step{};
}
void tearDown() {}
void test_write_preserves_notes_and_updates_last_duplicate() {
  steps[0].note = 60;
  steps[0].fx[0] = {Fx::FLT, 10};
  steps[0].fx[1] = {Fx::RAT, 3};
  steps[0].fx[4] = {Fx::FLT, 20};
  TEST_ASSERT_TRUE(automationSet(steps[0], Fx::FLT, 99));
  TEST_ASSERT_EQUAL(99, automationGet(steps[0], Fx::FLT));
  TEST_ASSERT_EQUAL(60, steps[0].note);
  TEST_ASSERT_EQUAL(3, steps[0].fx[1].val);
}
void test_range_is_atomic_when_full() {
  for (auto &f : steps[2].fx)
    f = {Fx::RAT, 3};
  TEST_ASSERT_FALSE(automationRamp(steps, 4, Fx::FLT, 0, 3, 0, 127));
  TEST_ASSERT_EQUAL(-1, automationGet(steps[0], Fx::FLT));
  TEST_ASSERT_EQUAL(Fx::RAT, steps[2].fx[0].cmd);
}
void test_ramp_and_clear_only_selected_parameter() {
  steps[1].fx[0] = {Fx::PRB, 70};
  TEST_ASSERT_TRUE(automationRamp(steps, 4, Fx::PAN, 3, 0, 127, 0));
  TEST_ASSERT_EQUAL(0, automationGet(steps[0], Fx::PAN));
  TEST_ASSERT_EQUAL(42, automationGet(steps[1], Fx::PAN));
  TEST_ASSERT_EQUAL(127, automationGet(steps[3], Fx::PAN));
  automationClear(steps[1], Fx::PAN);
  TEST_ASSERT_EQUAL(-1, automationGet(steps[1], Fx::PAN));
  TEST_ASSERT_EQUAL(70, steps[1].fx[0].val);
}
void test_lane_paste_preserves_notes_and_is_atomic() {
  AutomationClip c;
  steps[0].fx[0] = {Fx::FLT, 5};
  automationCopy(steps, 4, Fx::FLT, c);
  steps[0].note = 64;
  steps[1].fx[0] = {Fx::FLT, 90};
  TEST_ASSERT_TRUE(automationPaste(steps, 4, Fx::FLT, c));
  TEST_ASSERT_EQUAL(64, steps[0].note);
  TEST_ASSERT_EQUAL(-1, automationGet(steps[1], Fx::FLT));
}
void test_position_tracks_speed_and_polymeter() {
  static Pattern p;
  p.clear();
  p.length = 16;
  p.trackLen[0] = 5;
  p.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  TEST_ASSERT_EQUAL(3, automationStep(p, 0, 1, 0, 200));
  p.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Quarter);
  TEST_ASSERT_EQUAL(4, automationStep(p, 0, 1, 1, 0));
}
void test_motion_latches_only_changed_values_and_fails_atomically() {
  MotionLatch latch;
  int16_t a[kAutomationParams]{}, b[kAutomationParams]{};
  b[0] = 80;
  latch.touch(a, b);
  TEST_ASSERT_TRUE(latch.write(steps[0]));
  TEST_ASSERT_EQUAL(80, automationGet(steps[0], Fx::FLT));
  TEST_ASSERT_EQUAL(-1, automationGet(steps[0], Fx::PAN));
  TEST_ASSERT_TRUE(latch.write(steps[1]));
  TEST_ASSERT_EQUAL(80, automationGet(steps[1], Fx::FLT));
  for (auto &f : steps[2].fx)
    f = {Fx::PRB, 50};
  TEST_ASSERT_FALSE(latch.write(steps[2]));
  TEST_ASSERT_EQUAL(Fx::PRB, steps[2].fx[0].cmd);
  latch.clear();
  TEST_ASSERT_TRUE(latch.write(steps[3]));
  TEST_ASSERT_EQUAL(-1, automationGet(steps[3], Fx::FLT));
}
void test_macro_values_only_apply_to_supported_instrument_types() {
  TrackCfg t;
  Instrument i;
  i.type = InstrType::Sample;
  TEST_ASSERT_EQUAL(-1, automationValue(t, i, Fx::DCY));
  TEST_ASSERT_EQUAL(-1, automationValue(t, i, Fx::SAT));
  i.type = InstrType::Fm;
  i.macro[0] = 37;
  i.fmSat = 81;
  TEST_ASSERT_EQUAL(37, automationValue(t, i, Fx::DCY));
  TEST_ASSERT_EQUAL(81, automationValue(t, i, Fx::SAT));
}
void test_motion_does_not_partially_write_multiple_locks() {
  MotionLatch m;
  int16_t a[kAutomationParams]{}, b[kAutomationParams]{};
  b[0] = 40;
  b[1] = 80;
  m.touch(a, b);
  for (int n = 0; n < kFxSlots - 1; ++n)
    steps[0].fx[n] = {Fx::RAT, 3};
  Step original = steps[0];
  TEST_ASSERT_FALSE(m.write(steps[0]));
  TEST_ASSERT_EQUAL_MEMORY(&original, &steps[0], sizeof(Step));
}
void test_set_clamps_and_clear_removes_all_duplicates() {
  TEST_ASSERT_TRUE(automationSet(steps[0], Fx::DTY, 127));
  TEST_ASSERT_EQUAL(static_cast<int>(DriveModel::Count) - 1,
                    automationGet(steps[0], Fx::DTY));
  steps[0].fx[1] = {Fx::DTY, 2};
  automationClear(steps[0], Fx::DTY);
  TEST_ASSERT_EQUAL(-1, automationGet(steps[0], Fx::DTY));
  TEST_ASSERT_FALSE(automationSet(steps[0], Fx::RAT, 3));
}
void test_paste_failure_preserves_every_step() {
  AutomationClip clip;
  steps[0].fx[0] = {Fx::FLT, 0};
  steps[1].fx[0] = {Fx::FLT, 127};
  automationCopy(steps, 4, Fx::FLT, clip);
  steps[0].fx[0].val = 20;
  for (auto &f : steps[1].fx)
    f = {Fx::PRB, 50};
  TEST_ASSERT_FALSE(automationPaste(steps, 4, Fx::FLT, clip));
  TEST_ASSERT_EQUAL(20, automationGet(steps[0], Fx::FLT));
  TEST_ASSERT_EQUAL(-1, automationGet(steps[1], Fx::FLT));
}
void test_position_boundaries_and_x4() {
  static Pattern p;
  p.clear();
  p.length = 16;
  p.trackLen[0] = 5;
  p.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X4);
  TEST_ASSERT_EQUAL(4, automationStep(p, 0, 1, 0, 0));
  TEST_ASSERT_EQUAL(2, automationStep(p, 0, 1, 0, 255));
  p.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  TEST_ASSERT_EQUAL(2, automationStep(p, 0, 15, 0, 255));
  TEST_ASSERT_EQUAL(3, automationStep(p, 0, 0, 1, 0));
}
void test_locks_on_empty_steps_reach_synth_events() {
  TEST_ASSERT_TRUE(automationSet(steps[0], Fx::FLT, 80));
  TrackCfg t;
  t.out = TrackOut::Int;
  ExpandCtx c{};
  c.stepUs = 125000;
  Rng rng(1);
  static ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(steps[0], t, c, rng, out));
  bool found = false;
  for (int n = 0; n < out.count; ++n)
    if (out.ev[n].kind == EvKind::SynthFx &&
        out.ev[n].note == static_cast<uint8_t>(Fx::FLT)) {
      TEST_ASSERT_EQUAL(80, out.ev[n].vel);
      found = true;
    }
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_EQUAL(kNoteEmpty, steps[0].note);
}
void test_motion_latches_until_pass_end_and_preserves_next_pass_without_movement() {
  MotionLatch m;
  int16_t a[kAutomationParams]{}, b[kAutomationParams]{};
  b[0] = 30;
  m.beginPass(7);
  m.touch(a,b);
  TEST_ASSERT_TRUE(m.write(steps[0]));
  m.beginPass(7);
  TEST_ASSERT_TRUE(m.write(steps[1]));
  TEST_ASSERT_EQUAL(30,automationGet(steps[1],Fx::FLT));
  automationSet(steps[0],Fx::FLT,80);
  m.beginPass(8);
  TEST_ASSERT_TRUE(m.write(steps[0]));
  TEST_ASSERT_EQUAL(80,automationGet(steps[0],Fx::FLT));
  b[0] = 40;
  m.touch(a,b);
  TEST_ASSERT_TRUE(m.write(steps[0]));
  TEST_ASSERT_EQUAL(40,automationGet(steps[0],Fx::FLT));
  m.beginPass(8);
  TEST_ASSERT_TRUE(m.write(steps[1]));
  TEST_ASSERT_EQUAL(40,automationGet(steps[1],Fx::FLT));
  automationSet(steps[1],Fx::FLT,95);
  m.beginPass(9);
  m.touch(b,b);
  TEST_ASSERT_TRUE(m.write(steps[0]));
  TEST_ASSERT_EQUAL(40,automationGet(steps[0],Fx::FLT));
  TEST_ASSERT_TRUE(m.write(steps[1]));
  TEST_ASSERT_EQUAL(95,automationGet(steps[1],Fx::FLT));
  m.clear(); m.beginPass(20); m.touch(a,b);
  TEST_ASSERT_TRUE(m.write(steps[2]));
  TEST_ASSERT_TRUE(m.write(steps[3]));
  TEST_ASSERT_EQUAL(40,automationGet(steps[3],Fx::FLT));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_motion_latches_until_pass_end_and_preserves_next_pass_without_movement);
  RUN_TEST(test_locks_on_empty_steps_reach_synth_events);
  RUN_TEST(test_macro_values_only_apply_to_supported_instrument_types);
  RUN_TEST(test_motion_does_not_partially_write_multiple_locks);
  RUN_TEST(test_set_clamps_and_clear_removes_all_duplicates);
  RUN_TEST(test_paste_failure_preserves_every_step);
  RUN_TEST(test_position_boundaries_and_x4);
  RUN_TEST(test_motion_latches_only_changed_values_and_fails_atomically);
  RUN_TEST(test_write_preserves_notes_and_updates_last_duplicate);
  RUN_TEST(test_range_is_atomic_when_full);
  RUN_TEST(test_ramp_and_clear_only_selected_parameter);
  RUN_TEST(test_lane_paste_preserves_notes_and_is_atomic);
  RUN_TEST(test_position_tracks_speed_and_polymeter);
  return UNITY_END();
}
