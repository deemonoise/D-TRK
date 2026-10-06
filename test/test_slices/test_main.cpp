#include <unity.h>
#include <initializer_list>
#include "slices.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_insert_keeps_order_and_rejects_duplicates() {
  Instrument m;
  TEST_ASSERT_EQUAL(0, sliceInsert(m, 0x8000));
  TEST_ASSERT_EQUAL(0, sliceInsert(m, 0x1000));
  TEST_ASSERT_EQUAL(2, sliceInsert(m, 0x9000));
  TEST_ASSERT_EQUAL(-1, sliceInsert(m, 0x8000));
  TEST_ASSERT_EQUAL(3, m.sliceCount);
  TEST_ASSERT_EQUAL_HEX16(0x1000, m.slices[0]);
  TEST_ASSERT_EQUAL_HEX16(0x8000, m.slices[1]);
  TEST_ASSERT_EQUAL_HEX16(0x9000, m.slices[2]);
}

void test_insert_full_fails() {
  Instrument m;
  for (int i = 0; i < kMaxSlices; ++i) TEST_ASSERT_TRUE(sliceInsert(m, static_cast<uint16_t>(i * 100 + 1)) >= 0);
  TEST_ASSERT_EQUAL(-1, sliceInsert(m, 0xF000));
}

void test_remove_and_clear() {
  Instrument m;
  sliceInsert(m, 10);
  sliceInsert(m, 20);
  sliceInsert(m, 30);
  sliceRemove(m, 1);
  TEST_ASSERT_EQUAL(2, m.sliceCount);
  TEST_ASSERT_EQUAL(30, m.slices[1]);
  sliceRemove(m, 5);  // out of range: no-op
  TEST_ASSERT_EQUAL(2, m.sliceCount);
  sliceClear(m);
  TEST_ASSERT_EQUAL(0, m.sliceCount);
}

void test_move_clamps_between_neighbours() {
  Instrument m;
  sliceInsert(m, 100);
  sliceInsert(m, 200);
  sliceInsert(m, 300);
  TEST_ASSERT_EQUAL(299, sliceMove(m, 1, 5000));
  TEST_ASSERT_EQUAL(101, sliceMove(m, 1, 0));
  TEST_ASSERT_EQUAL(0, sliceMove(m, 0, 0));
}

void test_region_of_slices() {
  Instrument m;
  m.end = 0xFFFF;
  sliceInsert(m, 0);
  sliceInsert(m, 0x8000);
  uint32_t a, b;
  TEST_ASSERT_TRUE(sliceRegion(m, 0, 1000, a, b));
  TEST_ASSERT_EQUAL(0, a);
  TEST_ASSERT_EQUAL(500, b);
  TEST_ASSERT_TRUE(sliceRegion(m, 1, 1000, a, b));
  TEST_ASSERT_EQUAL(500, a);
  TEST_ASSERT_EQUAL(1000, b);  // the last one ends at End
  TEST_ASSERT_FALSE(sliceRegion(m, 2, 1000, a, b));
}

void test_chop_equal_within_start_end() {
  Instrument m;
  m.start = 0;
  m.end = 0x8000;
  m.chopN = 4;
  chopEqual(m);
  TEST_ASSERT_EQUAL(4, m.sliceCount);
  TEST_ASSERT_EQUAL(0, m.slices[0]);
  TEST_ASSERT_UINT16_WITHIN(1, 0x2000, m.slices[1]);
  TEST_ASSERT_UINT16_WITHIN(1, 0x6000, m.slices[3]);
}

void test_chop_equal_tiny_region_stays_ascending() {
  Instrument m;
  m.start = 100;
  m.end = 103;
  m.chopN = 8;
  chopEqual(m);
  TEST_ASSERT_TRUE(m.sliceCount >= 1 && m.sliceCount <= 3);
  for (int i = 1; i < m.sliceCount; ++i) TEST_ASSERT_TRUE(m.slices[i] > m.slices[i - 1]);
  m.start = m.end = 500;
  chopEqual(m);
  TEST_ASSERT_EQUAL(1, m.sliceCount);
}

void test_last_slice_past_end_plays_to_sample_end() {
  Instrument m;
  sliceInsert(m, 0x8000);
  m.end = 0x4000;
  uint32_t a, b;
  TEST_ASSERT_TRUE(sliceRegion(m, 0, 1000, a, b));
  TEST_ASSERT_EQUAL(1000, b);
}

void test_chop_equal_clamps_n() {
  Instrument m;
  m.chopN = 99;
  chopEqual(m);
  TEST_ASSERT_EQUAL(kMaxSlices, m.sliceCount);
}

void test_frac_frame_roundtrip() {
  for (uint32_t len : {1000u, 44100u, 320000u})
    for (uint32_t f : {0u, 1u, len / 3, len - 1})
      TEST_ASSERT_UINT32_WITHIN(1, f, fracToFrame(frameToFrac(f, len), len));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_insert_keeps_order_and_rejects_duplicates);
  RUN_TEST(test_insert_full_fails);
  RUN_TEST(test_remove_and_clear);
  RUN_TEST(test_move_clamps_between_neighbours);
  RUN_TEST(test_region_of_slices);
  RUN_TEST(test_chop_equal_within_start_end);
  RUN_TEST(test_chop_equal_clamps_n);
  RUN_TEST(test_chop_equal_tiny_region_stays_ascending);
  RUN_TEST(test_last_slice_past_end_plays_to_sample_end);
  RUN_TEST(test_frac_frame_roundtrip);
  return UNITY_END();
}
