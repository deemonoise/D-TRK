#include <math.h>
#include <string.h>
#include <unity.h>
#include "wt_file.h"
#include "wt_mip.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static int16_t in[64 * 2048];
static int16_t table[kWtTableSamples];

// n frames of len points; frame f = sine of harmonic (f % 8) + 1.
static void fill(int n, int len) {
  for (int f = 0; f < n; ++f)
    for (int i = 0; i < len; ++i)
      in[f * len + i] = static_cast<int16_t>(20000 * sinf(6.2831853f * (f % 8 + 1) * i / len));
}
static int strongestHarm(int frame) {
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(table + frame * kWtFramePts, kWtFrameLen, re, im);
  int best = 0;
  for (int h = 1; h < kWtHarm; ++h)
    if (fabsf(im[h]) + fabsf(re[h]) > fabsf(im[best]) + fabsf(re[best])) best = h;
  return best + 1;
}

void test_format_detect() {
  WtFormat f;
  TEST_ASSERT_EQUAL(WtErr::Ok, wtDetect(16384, 0, f));
  TEST_ASSERT_EQUAL(256, f.frameLen);
  TEST_ASSERT_EQUAL(64, f.frames);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtDetect(16384, 2048, f));
  TEST_ASSERT_EQUAL(2048, f.frameLen);
  TEST_ASSERT_EQUAL(8, f.frames);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtDetect(256 * 2048, 0, f));  // Serum without clm
  TEST_ASSERT_EQUAL(2048, f.frameLen);
  TEST_ASSERT_EQUAL(256, f.frames);
  TEST_ASSERT_EQUAL(WtErr::BadLength, wtDetect(1000, 0, f));
  TEST_ASSERT_EQUAL(WtErr::BadLength, wtDetect(0, 0, f));
}

void test_waveedit_identity() {
  fill(64, 256);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 16384, 0, table));
  TEST_ASSERT_EQUAL(1, strongestHarm(0));
  TEST_ASSERT_EQUAL(4, strongestHarm(3));
  TEST_ASSERT_EQUAL(8, strongestHarm(63));
}

void test_serum_2048_decimated() {
  fill(8, 2048);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 8 * 2048, 2048, table));
  TEST_ASSERT_EQUAL(1, strongestHarm(0));   // source frame 0
  TEST_ASSERT_EQUAL(8, strongestHarm(63));  // source frame 7
}

void test_many_frames_picked_evenly() {
  fill(64, 2048);  // 64 frames of 2048: each output frame = one source frame
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 64 * 2048, 2048, table));
  TEST_ASSERT_EQUAL(2, strongestHarm(1));
}

void test_single_frame() {
  fill(1, 256);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 256, 0, table));
  TEST_ASSERT_EQUAL(1, strongestHarm(40));
}

void test_silent_rejected() {
  memset(in, 0, sizeof(int16_t) * 16384);
  TEST_ASSERT_EQUAL(WtErr::Silent, wtImport(in, 16384, 0, table));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_format_detect);
  RUN_TEST(test_waveedit_identity);
  RUN_TEST(test_serum_2048_decimated);
  RUN_TEST(test_many_frames_picked_evenly);
  RUN_TEST(test_single_frame);
  RUN_TEST(test_silent_rejected);
  return UNITY_END();
}
