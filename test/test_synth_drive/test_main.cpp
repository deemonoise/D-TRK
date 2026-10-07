#include <math.h>
#include <initializer_list>
#include <unity.h>
#include "synth_drive.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_drive_zero_is_identity() {
  Drive d;
  d.set(0);
  TEST_ASSERT_FALSE(d.on());
  for (float x : {-1.5f, -0.3f, 0.f, 0.25f, 1.f, 7.f}) TEST_ASSERT_TRUE(d.process(x) == x);
  d.set(90);
  d.set(0);  // back off: bypass again
  TEST_ASSERT_TRUE(d.process(0.37f) == 0.37f);
}

void test_drive_saturates_and_is_monotonic() {
  Drive d;
  d.set(127);
  TEST_ASSERT_TRUE(d.on());
  float prev = -2;
  for (int i = -40; i <= 40; ++i) {
    const float y = d.process(i * 0.1f);
    TEST_ASSERT_TRUE(y >= prev - 1e-6f);
    prev = y;
    TEST_ASSERT_TRUE(fabsf(y) <= 1.0001f);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, d.process(1.f));  // normalized: tanh(g) / tanh(g)
  TEST_ASSERT_FLOAT_WITHIN(0.01f, tanhf(0.8f) / tanhf(8.f), d.process(0.1f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.f, d.process(-50.f));  // clamped input
}

void test_drive_low_is_gentle() {
  Drive d;
  d.set(1);  // g just above 1: close to tanh(x) / tanh(1)
  TEST_ASSERT_FLOAT_WITHIN(0.01f, tanhf(0.5f * (1.f + 7.f / 127.f)) / tanhf(1.f + 7.f / 127.f), d.process(0.5f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_drive_zero_is_identity);
  RUN_TEST(test_drive_saturates_and_is_monotonic);
  RUN_TEST(test_drive_low_is_gentle);
  return UNITY_END();
}
