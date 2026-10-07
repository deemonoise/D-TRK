#include <unity.h>
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_record_step_rounds_to_nearest);
  return UNITY_END();
}
