#include <unity.h>
#include <string.h>
#include "record.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_record_step_rounds_to_nearest() {
  TEST_ASSERT_EQUAL(3, recordStepFor(3, 0, 16));
  TEST_ASSERT_EQUAL(3, recordStepFor(3, 127, 16));
  TEST_ASSERT_EQUAL(4, recordStepFor(3, 128, 16));
  TEST_ASSERT_EQUAL(0, recordStepFor(15, 200, 16));  // wraps
  TEST_ASSERT_EQUAL(2, recordStepFor(18, 0, 16));    // a position past a shrunk length wraps too
  TEST_ASSERT_EQUAL(0, recordStepFor(3, 0, 0));
}

void test_record_hold_gate() {
  TEST_ASSERT_EQUAL(1, recordGate(0, 100000));
  TEST_ASSERT_EQUAL(1, recordGate(10000, 0));
  TEST_ASSERT_EQUAL(50, recordGate(50000, 100000));
  TEST_ASSERT_EQUAL(100, recordGate(100000, 100000));
  TEST_ASSERT_EQUAL(101, recordGate(107000, 100000));
  TEST_ASSERT_EQUAL(114, recordGate(200000, 100000));
  TEST_ASSERT_EQUAL(200, recordGate(800000, 100000));
  TEST_ASSERT_EQUAL(200, recordGate(60000000, 100000));
}

void test_long_hold_ties_and_releases_between_steps() {
  Step steps[16];
  steps[2].note = 60;
  steps[2].fx[0] = {Fx::GAT, 200};
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::Ok),
                    static_cast<int>(recordHold(steps, 16, 2, 950000, 100000, 24, 50)));
  TEST_ASSERT_NOT_NULL(steps[2].find(Fx::TIE));
  TEST_ASSERT_NULL(steps[2].find(Fx::GAT));
  for (int i = 3; i < 11; ++i) TEST_ASSERT_TRUE(steps[i].isEmpty());
  TEST_ASSERT_EQUAL(kNoteEmpty, steps[11].note);
  TEST_ASSERT_NOT_NULL(steps[11].find(Fx::OFF));
  TEST_ASSERT_EQUAL(12, steps[11].find(Fx::OFF)->val);
}

void test_long_hold_wrap_and_exact_boundary() {
  Step steps[16];
  steps[12].note = 60;
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::Ok),
                    static_cast<int>(recordHold(steps, 16, 12, 1000000, 100000, 24, 50)));
  TEST_ASSERT_NOT_NULL(steps[12].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteOff, steps[6].note);
  TEST_ASSERT_FALSE(steps[6].hasFx());
}

void test_long_hold_occupied_path_is_atomic() {
  Step steps[16];
  steps[0].note = 60;
  steps[9].note = 64;
  Step before[16];
  memcpy(before, steps, sizeof steps);
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::Occupied),
                    static_cast<int>(recordHold(steps, 16, 0, 1000000, 100000, 24, 50)));
  TEST_ASSERT_EQUAL_MEMORY(before, steps, sizeof steps);
}

void test_long_hold_reuses_gate_slot_or_reports_full() {
  Step steps[16];
  steps[0].note = 60;
  for (auto& fx : steps[0].fx) fx = {Fx::CCA, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::NoSlot),
                    static_cast<int>(recordHold(steps, 16, 0, 900000, 100000, 24, 50)));
  TEST_ASSERT_TRUE(steps[9].isEmpty());
  steps[0].fx[3] = {Fx::GAT, 200};
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::Ok),
                    static_cast<int>(recordHold(steps, 16, 0, 900000, 100000, 24, 50)));
  TEST_ASSERT_NOT_NULL(steps[0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteOff, steps[9].note);
}

void test_long_hold_cannot_overwrite_its_start_after_full_loop() {
  Step steps[16];
  steps[0].note = 60;
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::Clipped),
                    static_cast<int>(recordHold(steps, 16, 0, 3000000, 100000, 24, 50)));
  TEST_ASSERT_EQUAL(60, steps[0].note);
  TEST_ASSERT_NOT_NULL(steps[0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(23, steps[15].find(Fx::OFF)->val);
}

void test_short_hold_keeps_gate_and_boundary_is_not_tied() {
  Step steps[16];
  steps[0].note = 60;
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::Ok),
                    static_cast<int>(recordHold(steps, 16, 0, 800000, 100000, 24, 50)));
  TEST_ASSERT_NULL(steps[0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(200, steps[0].find(Fx::GAT)->val);
  TEST_ASSERT_TRUE(steps[8].isEmpty());
}

void test_long_hold_slow_quarter_release_does_not_wrap_off_byte() {
  Step steps[16];
  steps[0].note = 60;
  TEST_ASSERT_EQUAL(static_cast<int>(RecordHold::Ok),
                    static_cast<int>(recordHold(steps, 16, 0, 990000, 100000, 384, 50)));
  TEST_ASSERT_EQUAL(60, steps[9].note);
  TEST_ASSERT_NULL(steps[9].find(Fx::OFF));
  TEST_ASSERT_EQUAL(90, steps[9].find(Fx::GAT)->val);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_record_step_rounds_to_nearest);
  RUN_TEST(test_record_hold_gate);
  RUN_TEST(test_long_hold_slow_quarter_release_does_not_wrap_off_byte);
  RUN_TEST(test_long_hold_ties_and_releases_between_steps);
  RUN_TEST(test_long_hold_wrap_and_exact_boundary);
  RUN_TEST(test_long_hold_occupied_path_is_atomic);
  RUN_TEST(test_long_hold_reuses_gate_slot_or_reports_full);
  RUN_TEST(test_long_hold_cannot_overwrite_its_start_after_full_loop);
  RUN_TEST(test_short_hold_keeps_gate_and_boundary_is_not_tied);
  return UNITY_END();
}
