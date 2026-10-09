#include <unity.h>
#include "wave_peaks.h"

using namespace mt;

static int16_t data[2000];
static int8_t mn[480], mx[480];

void setUp() {
  for (int i = 0; i < 2000; ++i) data[i] = static_cast<int16_t>((i % 2 ? 1 : -1) * i * 16);
}
void tearDown() {}

void test_one_frame_per_column() {
  // span == width: column g is frame g.
  wavePeaks(data, 2000, 10, 480, 480, 4, mn, mx);
  for (int c = 0; c < 4; ++c) {
    TEST_ASSERT_EQUAL_INT8(data[10 + c] >> 8, mn[c]);
    TEST_ASSERT_EQUAL_INT8(data[10 + c] >> 8, mx[c]);
  }
}

void test_columns_follow_the_grid() {
  // 2000 frames on 480 columns: column g covers [g * 2000 / 480, (g + 1) * 2000 / 480).
  wavePeaks(data, 2000, 0, 2000, 480, 480, mn, mx);
  for (int g = 0; g < 480; ++g) {
    const int a = g * 2000 / 480, b = (g + 1) * 2000 / 480;
    int lo = 32767, hi = -32768;
    for (int k = a; k < b; ++k) {
      if (data[k] < lo) lo = data[k];
      if (data[k] > hi) hi = data[k];
    }
    TEST_ASSERT_EQUAL_INT8(lo >> 8, mn[g]);
    TEST_ASSERT_EQUAL_INT8(hi >> 8, mx[g]);
  }
  // A piece from column 100 is the same as the whole view's columns from 100 on.
  int8_t mn2[40], mx2[40];
  wavePeaks(data, 2000, 100, 2000, 480, 40, mn2, mx2);
  TEST_ASSERT_EQUAL_INT8_ARRAY(mn + 100, mn2, 40);
  TEST_ASSERT_EQUAL_INT8_ARRAY(mx + 100, mx2, 40);
}

void test_past_the_end_has_no_data() {
  // A short sample at the 1:1 view: columns past frame 300 are empty.
  wavePeaks(data, 300, 0, 480, 480, 480, mn, mx);
  TEST_ASSERT_TRUE(mn[299] <= mx[299]);
  TEST_ASSERT_TRUE(mn[300] > mx[300]);
  TEST_ASSERT_TRUE(mn[479] > mx[479]);
  wavePeaks(nullptr, 300, 0, 480, 480, 2, mn, mx);
  TEST_ASSERT_TRUE(mn[0] > mx[0]);
}

void test_long_columns_are_probed() {
  // A column of 100000 frames reads every 390th: a lone spike between probes is missed, one on a
  // probe is found.
  static int16_t big[100000];
  big[1] = 30000;
  big[390 * 7] = -30000;
  wavePeaks(big, 100000, 0, 100000, 1, 1, mn, mx);
  TEST_ASSERT_EQUAL_INT8(-30000 >> 8, mn[0]);
  TEST_ASSERT_EQUAL_INT8(0, mx[0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_one_frame_per_column);
  RUN_TEST(test_columns_follow_the_grid);
  RUN_TEST(test_past_the_end_has_no_data);
  RUN_TEST(test_long_columns_are_probed);
  return UNITY_END();
}
