#include <string.h>
#include <unity.h>
#include <vector>
#include "sample_heads.h"
#include "sample_set.h"
#include "slices.h"

using namespace mt;

static Project* p;
constexpr uint32_t kCrc = 0xC0FFEE00u, kFrames = 100000;

static Instrument& sampler(int i) {
  Instrument& m = p->instruments[i];
  instrSetType(m, InstrType::Sample);
  strcpy(m.sample, "S");
  m.start = 0;
  m.end = 0xFFFF;
  m.loop = static_cast<uint8_t>(LoopMode::Off);
  m.reverse = false;
  m.sliceCount = 0;
  return m;
}

void setUp() {
  p = new Project();
  projSampleSet(*p, "S", kCrc, kFrames);
}
void tearDown() { delete p; }

void test_start_only() {
  sampler(0);
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(0, h[0].frame);
  TEST_ASSERT_EQUAL_UINT32(4410, h[0].len);
}

void test_forward_loop_adds_loop_start() {
  Instrument& m = sampler(0);
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  m.loopStart = 0x8000;
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(2, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(50000, h[1].frame);
  TEST_ASSERT_EQUAL_UINT32(4410, h[1].len);
}

void test_reverse_head_ends_at_start() {
  sampler(0).reverse = true;
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(kFrames - 4410, h[0].frame);
  TEST_ASSERT_EQUAL_UINT32(4410, h[0].len);
}

void test_slices_get_short_heads() {
  Instrument& m = sampler(0);
  m.start = 0x4000;  // the start head is not at 0
  TEST_ASSERT_TRUE(sliceInsert(m, 0x4000) >= 0);
  TEST_ASSERT_TRUE(sliceInsert(m, 0xC000) >= 0);
  HeadPoint h[kMaxHeads];
  const int n = headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads);
  uint32_t f, t;
  TEST_ASSERT_TRUE(sliceRegion(m, 1, kFrames, f, t));
  bool found = false;
  for (int i = 0; i < n; ++i) found |= h[i].frame == f && h[i].len == 1102;
  TEST_ASSERT_TRUE(found);
}

void test_same_points_merge_and_others_ignored() {
  sampler(0);
  sampler(1);  // same sample, same start
  Instrument& o = sampler(2);
  strcpy(o.sample, "OTHER");
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL(0, headPoints(*p, 0x1234u, kFrames, 44100, h, kMaxHeads));
}

void test_kit_lane_gets_frame_zero() {
  Instrument& k = p->instruments[3];
  instrSetType(k, InstrType::Kit);
  strcpy(k.kit[0].sample, "S");
  k.kit[0].instr = kNoInstr;
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(0, h[0].frame);
}

void test_cap_keeps_starts_first() {
  Instrument& m = sampler(0);
  m.start = 0x8000;
  for (int i = 0; i < kMaxSlices; ++i) sliceInsert(m, static_cast<uint16_t>(0x100 + i * 0x700));
  HeadPoint h[4];
  const int n = headPoints(*p, kCrc, kFrames, 44100, h, 4);
  TEST_ASSERT_EQUAL(4, n);
  bool start = false;
  for (int i = 0; i < n; ++i) start |= h[i].frame == fracToFrame(0x8000, kFrames);
  TEST_ASSERT_TRUE(start);
}

void test_layout_parse_round_trip() {
  HeadPoint pts[2] = {{0, 100}, {5000, 50}};
  int16_t index[kHeadIndexFrames];
  const uint32_t len = headLayout(pts, 2, index);
  TEST_ASSERT_EQUAL_UINT32(kHeadIndexFrames + 150, len);
  std::vector<int16_t> entry(len);
  memcpy(entry.data(), index, sizeof index);
  for (int i = 0; i < 100; ++i) entry[kHeadIndexFrames + i] = static_cast<int16_t>(i);
  for (int i = 0; i < 50; ++i) entry[kHeadIndexFrames + 100 + i] = static_cast<int16_t>(5000 + i);
  SampleHead h[kMaxHeads];
  int n = 0;
  TEST_ASSERT_TRUE(headParse(entry.data(), len, h, n));
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL_UINT32(5000, h[1].frame);
  TEST_ASSERT_EQUAL_INT16(5049, h[1].d[49]);
  TEST_ASSERT_TRUE(headSame(entry.data(), len, pts, 2));
  pts[1].len = 51;
  TEST_ASSERT_FALSE(headSame(entry.data(), len, pts, 2));
}

void test_parse_rejects_broken_index() {
  std::vector<int16_t> entry(kHeadIndexFrames + 10, 0);
  entry[0] = 1;  // one head ...
  const uint32_t frame = 0, len = 500, off = kHeadIndexFrames;  // ... longer than the entry
  memcpy(&entry[2], &frame, 4);
  memcpy(&entry[4], &len, 4);
  memcpy(&entry[6], &off, 4);
  SampleHead h[kMaxHeads];
  int n = 0;
  TEST_ASSERT_FALSE(headParse(entry.data(), static_cast<uint32_t>(entry.size()), h, n));
}

void test_head_entry_counts_as_used() {
  BankEntry e = {};
  headKey(kCrc, e.name);
  TEST_ASSERT_EQUAL_STRING("hc0ffee00", e.name);
  TEST_ASSERT_TRUE(bankEntryUsed(*p, e));
  headKey(0x1234u, e.name);
  TEST_ASSERT_FALSE(bankEntryUsed(*p, e));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_only);
  RUN_TEST(test_forward_loop_adds_loop_start);
  RUN_TEST(test_reverse_head_ends_at_start);
  RUN_TEST(test_slices_get_short_heads);
  RUN_TEST(test_same_points_merge_and_others_ignored);
  RUN_TEST(test_kit_lane_gets_frame_zero);
  RUN_TEST(test_cap_keeps_starts_first);
  RUN_TEST(test_layout_parse_round_trip);
  RUN_TEST(test_parse_rejects_broken_index);
  RUN_TEST(test_head_entry_counts_as_used);
  return UNITY_END();
}
