#include <math.h>
#include <string.h>
#include <unity.h>
#include "wt_mip.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static int16_t table[kWtTableSamples];

// Saw spectrum on every frame: sin harmonics 1/h.
struct SawSrc : WtSpectrumSource {
  void spectrum(int, float* re, float* im) override {
    for (int h = 1; h <= kWtHarm; ++h) {
      re[h - 1] = 0;
      im[h - 1] = 1.f / h;
    }
  }
};
// Frame f: single harmonic f + 1 (to tell frames apart).
struct OneHarm : WtSpectrumSource {
  void spectrum(int f, float* re, float* im) override {
    for (int h = 1; h <= kWtHarm; ++h) re[h - 1] = im[h - 1] = 0;
    im[f] = 1;
  }
};

void test_layout() {
  TEST_ASSERT_EQUAL(256, wtLevelLen(0));
  TEST_ASSERT_EQUAL(128, wtLevelLen(1));
  TEST_ASSERT_EQUAL(64, wtLevelLen(7));
  TEST_ASSERT_EQUAL(0, wtLevelOff(0));
  TEST_ASSERT_EQUAL(384, wtLevelOff(2));
  TEST_ASSERT_EQUAL(704, wtLevelOff(7));
  TEST_ASSERT_EQUAL(768, kWtFramePts);
  TEST_ASSERT_EQUAL(49152, kWtTableSamples);
}

void test_analyze_roundtrip() {
  int16_t x[256];
  for (int n = 0; n < 256; ++n)
    x[n] = static_cast<int16_t>(10000 * sinf(6.2831853f * 3 * n / 256) + 5000 * cosf(6.2831853f * 7 * n / 256) + 3000);
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(x, 256, re, im);
  TEST_ASSERT_FLOAT_WITHIN(5, 10000, im[2]);
  TEST_ASSERT_FLOAT_WITHIN(5, 5000, re[6]);
  TEST_ASSERT_FLOAT_WITHIN(5, 0, re[0]);  // DC is not a harmonic
}

void test_build_levels_bandlimited() {
  SawSrc s;
  TEST_ASSERT_TRUE(wtBuild(s, table));
  for (int k = 0; k < kWtLevels; ++k) {
    const int len = wtLevelLen(k), top = kWtHarm >> k;
    const int16_t* lv = table + 5 * kWtFramePts + wtLevelOff(k);
    float re[kWtHarm], im[kWtHarm];
    wtAnalyze(lv, len, re, im);
    long sum = 0;
    for (int i = 0; i < len; ++i) sum += lv[i];
    TEST_ASSERT_TRUE(labs(sum / len) < 20);  // no DC
    TEST_ASSERT_TRUE(fabsf(im[0]) > 1000);   // fundamental present
    for (int h = top + 1; h <= len / 2 && h <= kWtHarm; ++h)
      TEST_ASSERT_TRUE(fabsf(re[h - 1]) + fabsf(im[h - 1]) < 40);  // nothing above the level's limit
  }
}

void test_build_normalized() {
  SawSrc s;
  wtBuild(s, table);
  int peak = 0;
  for (int i = 0; i < kWtTableSamples; ++i) peak = abs(table[i]) > peak ? abs(table[i]) : peak;
  TEST_ASSERT_INT_WITHIN(2, kWtPeak, peak);
}

void test_frames_distinct() {
  OneHarm s;
  wtBuild(s, table);
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(table + 9 * kWtFramePts, 256, re, im);
  TEST_ASSERT_TRUE(fabsf(im[9]) > 10000);
  TEST_ASSERT_TRUE(fabsf(im[0]) < 50);
}

void test_silent_source_fails() {
  struct Zero : WtSpectrumSource {
    void spectrum(int, float* re, float* im) override {
      for (int h = 0; h < kWtHarm; ++h) re[h] = im[h] = 0;
    }
  } z;
  TEST_ASSERT_FALSE(wtBuild(z, table));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_layout);
  RUN_TEST(test_analyze_roundtrip);
  RUN_TEST(test_build_levels_bandlimited);
  RUN_TEST(test_build_normalized);
  RUN_TEST(test_frames_distinct);
  RUN_TEST(test_silent_source_fails);
  return UNITY_END();
}
