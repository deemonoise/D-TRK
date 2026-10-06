#include <math.h>
#include <string.h>
#include <unity.h>
#include "model.h"
#include "wt_builtin.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static int16_t table[kWtTableSamples];

void test_names() {
  TEST_ASSERT_EQUAL_STRING("*SAWSQR", wtBuiltinName(0));
  TEST_ASSERT_EQUAL(1, wtBuiltinFind("*pwm"));
  TEST_ASSERT_EQUAL(-1, wtBuiltinFind("SAWSQR"));
  TEST_ASSERT_EQUAL(-1, wtBuiltinFind(nullptr));
  TEST_ASSERT_TRUE(isWtBuiltin("*X"));
  TEST_ASSERT_FALSE(isWtBuiltin("X"));
  TEST_ASSERT_FALSE(isWtBuiltin(nullptr));
  for (int i = 0; i < kWtBuiltins; ++i) {
    TEST_ASSERT_TRUE(strlen(wtBuiltinName(i)) <= kSampleNameMax);
    TEST_ASSERT_TRUE(isWtBuiltin(wtBuiltinName(i)));
    TEST_ASSERT_EQUAL(i, wtBuiltinFind(wtBuiltinName(i)));
  }
}

void test_all_build() {
  for (int i = 0; i < kWtBuiltins; ++i) {
    WtBuiltinSrc s(i);
    TEST_ASSERT_TRUE_MESSAGE(wtBuild(s, table), wtBuiltinName(i));
  }
}

void test_sawsqr_ends() {
  WtBuiltinSrc s(0);
  wtBuild(s, table);
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(table, kWtFrameLen, re, im);  // frame 0: saw, h2 present
  TEST_ASSERT_TRUE(fabsf(im[1]) > 0.4f * fabsf(im[0]));
  wtAnalyze(table + 63 * kWtFramePts, kWtFrameLen, re, im);  // frame 63: square, no h2
  TEST_ASSERT_TRUE(fabsf(im[1]) < 0.01f * fabsf(im[0]));
}

void test_organ_only_drawbars() {
  WtBuiltinSrc s(5);
  float re[kWtHarm], im[kWtHarm];
  s.spectrum(10, re, im);
  for (int h = 1; h <= kWtHarm; ++h) {
    const bool bar = h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8;
    if (!bar) TEST_ASSERT_EQUAL_FLOAT(0, im[h - 1]);
    TEST_ASSERT_EQUAL_FLOAT(0, re[h - 1]);
  }
}

void test_bell_frame0_is_sine() {
  WtBuiltinSrc s(7);  // I = 0 at frame 0: pure sine
  float re[kWtHarm], im[kWtHarm];
  s.spectrum(0, re, im);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 20000, im[0]);
  for (int h = 2; h <= kWtHarm; ++h) TEST_ASSERT_TRUE(fabsf(im[h - 1]) + fabsf(re[h - 1]) < 50);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names);
  RUN_TEST(test_all_build);
  RUN_TEST(test_sawsqr_ends);
  RUN_TEST(test_organ_only_drawbars);
  RUN_TEST(test_bell_frame0_is_sine);
  return UNITY_END();
}
