#include <unity.h>
#include "sample_region.h"
#include "slices.h"

using namespace mt;

static Instrument m;

void setUp() {
  m = Instrument();
  m.start = 0;
  m.end = 0xFFFF;
  m.loopStart = 0;
  m.loop = static_cast<uint8_t>(LoopMode::Off);
  m.reverse = false;
  m.sliceCount = 0;
}
void tearDown() {}

void test_whole_region_plays_from_start() {
  const SampleRegion r = sampleRegion(m, -1, 1000, 0);
  TEST_ASSERT_EQUAL_UINT32(0, r.from);
  TEST_ASSERT_EQUAL_UINT32(1000, r.to);
  TEST_ASSERT_EQUAL_UINT32(0, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(1000, r.loopHi);
  TEST_ASSERT_EQUAL(1, r.dir);
  TEST_ASSERT_TRUE(r.pos == 0);
}

void test_forward_loop_and_ofs() {
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  m.loopStart = 0x8000;
  const SampleRegion r = sampleRegion(m, -1, 1000, 64);
  TEST_ASSERT_EQUAL_UINT32(500, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(1000, r.loopHi);
  TEST_ASSERT_TRUE(r.pos == (int64_t(250) << 32));
}

void test_reverse_mirrors_loop_and_start() {
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  m.loopStart = 0x8000;
  m.reverse = true;
  const SampleRegion r = sampleRegion(m, -1, 1000, 64);
  TEST_ASSERT_EQUAL(-1, r.dir);
  TEST_ASSERT_EQUAL_UINT32(0, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(500, r.loopHi);
  TEST_ASSERT_TRUE(r.pos == (int64_t(749) << 32));
}

void test_end_before_start_clamps() {
  m.start = 0xFFFF;
  m.end = 0;
  const SampleRegion r = sampleRegion(m, -1, 1000, 0);
  TEST_ASSERT_EQUAL_UINT32(999, r.from);
  TEST_ASSERT_EQUAL_UINT32(1000, r.to);
}

void test_slice_ignores_loop() {
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  TEST_ASSERT_TRUE(sliceInsert(m, 0x4000) >= 0);
  TEST_ASSERT_TRUE(sliceInsert(m, 0x8000) >= 0);
  uint32_t f = 0, t = 0;
  TEST_ASSERT_TRUE(sliceRegion(m, 1, 1000, f, t));
  const SampleRegion r = sampleRegion(m, 1, 1000, 0);
  TEST_ASSERT_EQUAL_UINT32(f, r.from);
  TEST_ASSERT_EQUAL_UINT32(t, r.to);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LoopMode::Off), r.loopMode);
  TEST_ASSERT_EQUAL_UINT32(f, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(t, r.loopHi);
  TEST_ASSERT_TRUE(r.pos == (int64_t(f) << 32));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_whole_region_plays_from_start);
  RUN_TEST(test_forward_loop_and_ofs);
  RUN_TEST(test_reverse_mirrors_loop_and_start);
  RUN_TEST(test_end_before_start_clamps);
  RUN_TEST(test_slice_ignores_loop);
  return UNITY_END();
}
