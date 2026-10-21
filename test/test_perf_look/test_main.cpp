#include <unity.h>
#include <math.h>
#include "perf_look.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {
PerfLook one(PerfFx f, int depth, float steps = 0, float held = 0) {
  const PerfVisIn in{f, depth, held};
  return perfLook(&in, 1, steps);
}
uint16_t be(uint16_t rgb) { return static_cast<uint16_t>(rgb >> 8 | rgb << 8); }  // panel byte order
}  // namespace

void test_identity() {
  const PerfLook v = perfLook(nullptr, 0, 0);
  TEST_ASSERT_EQUAL(256, v.mul);
  TEST_ASSERT_EQUAL(0, v.wash);
  TEST_ASSERT_EQUAL(256, v.contrast);
  TEST_ASSERT_EQUAL(5, v.bits);
  TEST_ASSERT_EQUAL(1, v.block);
  TEST_ASSERT_EQUAL(0, v.tile);
  TEST_ASSERT_FALSE(v.invert || v.idle || v.mirror || v.freeze);
  TEST_ASSERT_TRUE(perfLookPlainColour(v));
}

void test_filter() {
  TEST_ASSERT_EQUAL(64, one(PerfFx::Filter, -64).mul);
  TEST_ASSERT_EQUAL(189, one(PerfFx::Filter, 63).wash);
  TEST_ASSERT_EQUAL(256, one(PerfFx::Filter, 0).mul);
  TEST_ASSERT_EQUAL(0, one(PerfFx::Filter, 0).wash);
}

void test_crush_and_downsample() {
  TEST_ASSERT_EQUAL(5, one(PerfFx::Crush, 12).bits);
  TEST_ASSERT_FALSE(one(PerfFx::Crush, 12).idle);
  TEST_ASSERT_EQUAL(3, one(PerfFx::Crush, 6).bits);
  TEST_ASSERT_EQUAL(1, one(PerfFx::Crush, 2).bits);
  TEST_ASSERT_TRUE(one(PerfFx::Crush, 2).idle);
  const int blocks[] = {2, 3, 4, 8, 16, 32};
  for (int d = 0; d < 6; ++d) TEST_ASSERT_EQUAL(blocks[d], one(PerfFx::Downsample, d).block);
}

void test_gate_phase() {
  TEST_ASSERT_FALSE(one(PerfFx::Gate, 3, 0.2f).invert);  // 1/16: one step per period
  TEST_ASSERT_TRUE(one(PerfFx::Gate, 3, 0.7f).invert);
  TEST_ASSERT_TRUE(one(PerfFx::Gate, 3, 5.7f).invert);
}

void test_tape_stop() {
  TEST_ASSERT_EQUAL(0, one(PerfFx::TapeStop, 2, 0, 0).slide);  // 1/4 bar = 4 steps
  const PerfLook end = one(PerfFx::TapeStop, 2, 0, 4);
  TEST_ASSERT_EQUAL(kPerfLookH, end.slide);
  TEST_ASSERT_EQUAL(56, end.mul);
  TEST_ASSERT_EQUAL(kPerfLookH, one(PerfFx::TapeStop, 2, 0, 9).slide);  // stays stopped
}

void test_geometry_flags() {
  TEST_ASSERT_EQUAL(160, one(PerfFx::Repeat, 0).tile);
  TEST_ASSERT_EQUAL(10, one(PerfFx::Repeat, 6).tile);
  TEST_ASSERT_EQUAL(80, one(PerfFx::Roll, 1).tile);
  TEST_ASSERT_TRUE(one(PerfFx::Reverse, 2).mirror);
  TEST_ASSERT_TRUE(one(PerfFx::Freeze, 50).freeze);
  TEST_ASSERT_EQUAL(0, one(PerfFx::Scratch, 1, 0).shift);  // sine starts at 0
  TEST_ASSERT_EQUAL(60, one(PerfFx::Scratch, 1, 0.5f).shift);  // 1/8 = 2 steps: a quarter period
  TEST_ASSERT_EQUAL(24, one(PerfFx::ModThrow, 127).waveAmp);
  TEST_ASSERT_EQUAL(8, one(PerfFx::TapeWobble, 127).wobAmp);
  TEST_ASSERT_EQUAL(128, one(PerfFx::DelayThrow, 127).ghost);
  TEST_ASSERT_EQUAL(3, one(PerfFx::ReverbThrow, 127).blur);
  TEST_ASSERT_EQUAL(0, one(PerfFx::ReverbThrow, 0).blur);
}

void test_stack_multiplies() {
  const PerfVisIn in[] = {{PerfFx::Filter, -64, 0}, {PerfFx::TapeStop, 2, 4}};
  TEST_ASSERT_EQUAL(64 * 56 / 256, perfLook(in, 2, 0).mul);
}

void test_src_row() {
  PerfLook v;
  TEST_ASSERT_EQUAL(100, perfLookSrcRow(v, 100));
  v.slide = 50;
  TEST_ASSERT_EQUAL(-1, perfLookSrcRow(v, 49));
  TEST_ASSERT_EQUAL(0, perfLookSrcRow(v, 50));
  v.slide = 0;
  v.tile = 40;
  TEST_ASSERT_EQUAL(5, perfLookSrcRow(v, 85));
  v.tile = 0;
  v.block = 8;
  TEST_ASSERT_EQUAL(80, perfLookSrcRow(v, 87));
}

void test_shift() {
  PerfLook v;
  v.shift = 7;
  TEST_ASSERT_EQUAL(7, perfLookShift(v, 10));
  v.waveAmp = 10;
  v.waveLen = 64;
  TEST_ASSERT_EQUAL(17, perfLookShift(v, 16));  // a quarter wave: +amp
}

void test_row_geometry() {
  uint16_t src[8], dst[8];
  for (int i = 0; i < 8; ++i) src[i] = static_cast<uint16_t>(i + 1);
  PerfLook v;
  perfLookRow(v, src, dst, 8, 0);
  for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(i + 1, dst[i]);
  v.mirror = true;
  perfLookRow(v, src, dst, 8, 0);
  TEST_ASSERT_EQUAL(8, dst[0]);
  TEST_ASSERT_EQUAL(1, dst[7]);
  v.mirror = false;
  perfLookRow(v, src, dst, 8, 2);  // moved right by 2, wrapped
  TEST_ASSERT_EQUAL(7, dst[0]);
  TEST_ASSERT_EQUAL(1, dst[2]);
  perfLookRow(v, src, dst, 8, -1);
  TEST_ASSERT_EQUAL(2, dst[0]);
  TEST_ASSERT_EQUAL(1, dst[7]);
  v.block = 4;
  perfLookRow(v, src, dst, 8, 0);
  TEST_ASSERT_EQUAL(1, dst[3]);
  TEST_ASSERT_EQUAL(5, dst[4]);
}

void test_row_colour() {
  const uint16_t white = be(0xFFFF), red = be(0xF800);
  uint16_t src[4] = {white, white, white, white}, dst[4];
  PerfLook v;
  v.mul = 128;
  perfLookRow(v, src, dst, 4, 0);
  TEST_ASSERT_EQUAL_HEX16(be(0x7BEF), dst[0]);  // half: 15/31/15 of 31/63/31
  v.mul = 256;
  v.bits = 1;
  src[0] = be(0x7BEF);  // 15/31/15: top bit off in every channel
  perfLookRow(v, src, dst, 1, 0);
  TEST_ASSERT_EQUAL_HEX16(be(0x0000), dst[0]);
  v.bits = 5;
  v.wash = 256;
  src[0] = red;
  perfLookRow(v, src, dst, 1, 0);
  TEST_ASSERT_EQUAL_HEX16(white, dst[0]);
}

void test_row_ghost_and_blur() {
  const uint16_t white = be(0xFFFF);
  uint16_t src[kPerfGhostDx + 1] = {}, dst[kPerfGhostDx + 1];
  src[kPerfGhostDx] = white;
  PerfLook v;
  v.ghost = 128;
  perfLookRow(v, src, dst, kPerfGhostDx + 1, 0);
  TEST_ASSERT_EQUAL_HEX16(be(0x7BEF), dst[0]);  // half of the white one kPerfGhostDx to the right
  PerfLook b;
  b.blur = 1;
  uint16_t s3[3] = {0, white, 0}, d3[3];
  perfLookRow(b, s3, d3, 3, 0);
  TEST_ASSERT_EQUAL_HEX16(be(0x52AA), d3[1]);  // a third: 10/21/10 (blur clamps at the edges)
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_identity);
  RUN_TEST(test_filter);
  RUN_TEST(test_crush_and_downsample);
  RUN_TEST(test_gate_phase);
  RUN_TEST(test_tape_stop);
  RUN_TEST(test_geometry_flags);
  RUN_TEST(test_stack_multiplies);
  RUN_TEST(test_src_row);
  RUN_TEST(test_shift);
  RUN_TEST(test_row_geometry);
  RUN_TEST(test_row_colour);
  RUN_TEST(test_row_ghost_and_blur);
  return UNITY_END();
}
