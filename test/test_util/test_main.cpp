#include <unity.h>
#include "note_name.h"
#include "rng.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_note_names() {
  char b[4];
  noteName(60, b); TEST_ASSERT_EQUAL_STRING("C-4", b);
  noteName(61, b); TEST_ASSERT_EQUAL_STRING("C#4", b);
  noteName(127, b); TEST_ASSERT_EQUAL_STRING("G-9", b);
  noteName(0, b); TEST_ASSERT_EQUAL_STRING("C-m", b);
  noteName(kNoteEmpty, b); TEST_ASSERT_EQUAL_STRING("---", b);
  noteName(kNoteOff, b); TEST_ASSERT_EQUAL_STRING("OFF", b);
}

void test_rng_deterministic_and_bounded() {
  Rng a(42), b(42);
  for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL_UINT32(a.next(), b.next());
  Rng c(0);
  TEST_ASSERT_NOT_EQUAL(0, c.next());
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_TRUE(c.below(100) < 100);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_note_names);
  RUN_TEST(test_rng_deterministic_and_bounded);
  return UNITY_END();
}
