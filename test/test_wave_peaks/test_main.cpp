#include <string.h>
#include <unity.h>
#include <vector>
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

struct MemPeaks : PeakReader {
  const std::vector<int16_t>* d;
  std::vector<int8_t> pk;
  int reads = 0;
  bool pairs(uint32_t first, uint32_t n, int8_t* out) override {
    ++reads;
    if ((first + n) * 2 > pk.size()) return false;
    memcpy(out, pk.data() + first * 2, n * 2);
    return true;
  }
  bool frames(uint32_t first, uint32_t n, int16_t* out) override {
    ++reads;
    if (first + n > d->size()) return false;
    memcpy(out, d->data() + first, n * 2);
    return true;
  }
};

static std::vector<int16_t> noise(uint32_t n) {
  std::vector<int16_t> v(n);
  uint32_t x = 7;
  for (auto& s : v) {
    x = x * 1664525u + 1013904223u;
    s = static_cast<int16_t>(x >> 16);
  }
  return v;
}

void test_overview_pairs_per_block() {
  const auto d = noise(1000);
  std::vector<int8_t> pk(8);
  TEST_ASSERT_EQUAL(4, peakOverview(d.data(), 1000, pk.data()));  // 256, 256, 256, 232
  int lo = 32767, hi = -32768;
  for (int i = 768; i < 1000; ++i) {
    lo = d[i] < lo ? d[i] : lo;
    hi = d[i] > hi ? d[i] : hi;
  }
  TEST_ASSERT_EQUAL_INT8(lo >> 8, pk[6]);
  TEST_ASSERT_EQUAL_INT8(hi >> 8, pk[7]);
}

void test_read_wide_columns_from_overview() {
  const auto d = noise(100000);
  MemPeaks r;
  r.d = &d;
  r.pk.resize(2 * ((100000 + 255) / 256));
  peakOverview(d.data(), 100000, r.pk.data());
  int8_t mn[40], mx[40];
  TEST_ASSERT_TRUE(wavePeaksRead(r, 100000, 0, 100000, 40, 40, mn, mx));  // 2500 frames per column
  for (int c = 0; c < 40; ++c) {
    const uint32_t a = c * 2500 / kPeakBlock * kPeakBlock, b = ((c + 1) * 2500 + kPeakBlock - 1) / kPeakBlock * kPeakBlock;
    int lo = 32767, hi = -32768;
    for (uint32_t i = a; i < b && i < 100000; ++i) {
      lo = d[i] < lo ? d[i] : lo;
      hi = d[i] > hi ? d[i] : hi;
    }
    TEST_ASSERT_EQUAL_INT8(lo >> 8, mn[c]);
    TEST_ASSERT_EQUAL_INT8(hi >> 8, mx[c]);
  }
}

void test_read_narrow_columns_reads_frames() {
  const auto d = noise(5000);
  MemPeaks r;
  r.d = &d;  // no overview: narrow columns never ask for it
  int8_t mn[50], mx[50], wmn[50], wmx[50];
  TEST_ASSERT_TRUE(wavePeaksRead(r, 5000, 10, 5000, 100, 50, mn, mx));  // 50 frames per column
  wavePeaks(d.data(), 5000, 10, 5000, 100, 50, wmn, wmx);
  TEST_ASSERT_EQUAL_INT8_ARRAY(wmn, mn, 50);
  TEST_ASSERT_EQUAL_INT8_ARRAY(wmx, mx, 50);
}

void test_read_past_end_is_empty_column() {
  const auto d = noise(1000);
  MemPeaks r;
  r.d = &d;
  int8_t mn[4], mx[4];
  TEST_ASSERT_TRUE(wavePeaksRead(r, 1000, 30, 1000, 32, 4, mn, mx));  // columns 30..33 of 32
  TEST_ASSERT_TRUE(mn[2] > mx[2]);
  TEST_ASSERT_TRUE(mn[3] > mx[3]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_one_frame_per_column);
  RUN_TEST(test_columns_follow_the_grid);
  RUN_TEST(test_past_the_end_has_no_data);
  RUN_TEST(test_long_columns_are_probed);
  RUN_TEST(test_overview_pairs_per_block);
  RUN_TEST(test_read_wide_columns_from_overview);
  RUN_TEST(test_read_narrow_columns_reads_frames);
  RUN_TEST(test_read_past_end_is_empty_column);
  return UNITY_END();
}
