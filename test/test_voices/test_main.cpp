#include <unity.h>
#include "voices.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Voices v;

void test_retrigger_and_stale_off() {
  v.clear();
  TEST_ASSERT_FALSE(v.noteOn(0, 60, 1));   // fresh note
  TEST_ASSERT_TRUE(v.noteOn(0, 60, 2));    // retrigger: caller must send off first
  TEST_ASSERT_FALSE(v.noteOff(0, 60, 1));  // stale off of the first note is ignored
  TEST_ASSERT_TRUE(v.active(0, 60));
  TEST_ASSERT_TRUE(v.noteOff(0, 60, 2));
  TEST_ASSERT_FALSE(v.active(0, 60));
}

void test_release_all() {
  v.clear();
  v.noteOn(0, 60, 1);
  v.noteOn(9, 36, 2);
  int n = 0;
  v.releaseAll([&](uint8_t ch, uint8_t note) {
    ++n;
    TEST_ASSERT_TRUE((ch == 0 && note == 60) || (ch == 9 && note == 36));
  });
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_FALSE(v.active(0, 60));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_retrigger_and_stale_off);
  RUN_TEST(test_release_all);
  return UNITY_END();
}
