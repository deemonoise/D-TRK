#include <unity.h>
#include "perf_axis.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Every value of the range is reachable, the ends give lo and hi, the depth never decreases.
static void assertSpans(PerfFx f, int len) {
  const PerfRange r = perfRange(f);
  TEST_ASSERT_EQUAL(r.lo, perfAxisDepth(f, 0, len));
  TEST_ASSERT_EQUAL(r.hi, perfAxisDepth(f, len - 1, len));
  int prev = r.lo;
  bool seen[256] = {};
  for (int p = 0; p < len; ++p) {
    const int d = perfAxisDepth(f, p, len);
    TEST_ASSERT_TRUE(d >= prev);
    prev = d;
    seen[d + 128] = true;
  }
  for (int i = 0; i < perfDepthCount(f); ++i) TEST_ASSERT_TRUE(seen[perfDepthAt(f, i) + 128]);
}

void test_axis_spans_every_effect_on_both_pads() {
  for (int f = 1; f < static_cast<int>(PerfFx::Count); ++f) {
    assertSpans(static_cast<PerfFx>(f), 480);
    assertSpans(static_cast<PerfFx>(f), 272);
  }
}

void test_axis_clamps_and_none() {
  TEST_ASSERT_EQUAL(0, perfAxisDepth(PerfFx::Repeat, -50, 480));
  TEST_ASSERT_EQUAL(6, perfAxisDepth(PerfFx::Repeat, 9999, 480));
  TEST_ASSERT_EQUAL(0, perfAxisDepth(PerfFx::None, 100, 480));
  TEST_ASSERT_EQUAL(2, perfAxisDepth(PerfFx::Crush, 100, 0));  // no pad: lo
}

// REPEAT 1/2..1/32: five equal zones; the edge is the first pixel of the next value.
void test_axis_list_zones() {
  TEST_ASSERT_TRUE(perfAxisZoned(PerfFx::Repeat));
  TEST_ASSERT_TRUE(perfAxisZoned(PerfFx::Crush));
  TEST_ASSERT_FALSE(perfAxisZoned(PerfFx::Filter));
  TEST_ASSERT_FALSE(perfAxisZoned(PerfFx::None));
  for (int i = 1; i < perfDepthCount(PerfFx::Repeat); ++i) {
    const int e = perfAxisEdge(PerfFx::Repeat, i, 480);
    TEST_ASSERT_EQUAL(perfDepthAt(PerfFx::Repeat, i - 1), perfAxisDepth(PerfFx::Repeat, e - 1, 480));
    TEST_ASSERT_EQUAL(perfDepthAt(PerfFx::Repeat, i), perfAxisDepth(PerfFx::Repeat, e, 480));
  }
  TEST_ASSERT_EQUAL(96, perfAxisEdge(PerfFx::Repeat, 1, 480));
}

// FILTER: LP on the low side, OFF in a dead zone of len / 8 round the middle, HP on the high side.
void test_axis_filter_bipolar_dead_zone() {
  int from, to;
  TEST_ASSERT_TRUE(perfAxisDead(PerfFx::Filter, 480, from, to));
  TEST_ASSERT_EQUAL(210, from);
  TEST_ASSERT_EQUAL(270, to);
  TEST_ASSERT_EQUAL(-1, perfAxisDepth(PerfFx::Filter, from - 1, 480));
  TEST_ASSERT_EQUAL(0, perfAxisDepth(PerfFx::Filter, from, 480));
  TEST_ASSERT_EQUAL(0, perfAxisDepth(PerfFx::Filter, 240, 480));
  TEST_ASSERT_EQUAL(0, perfAxisDepth(PerfFx::Filter, to - 1, 480));
  TEST_ASSERT_EQUAL(1, perfAxisDepth(PerfFx::Filter, to, 480));
  TEST_ASSERT_TRUE(perfAxisDead(PerfFx::Filter, 272, from, to));
  TEST_ASSERT_EQUAL(119, from);
  TEST_ASSERT_EQUAL(153, to);
  TEST_ASSERT_FALSE(perfAxisDead(PerfFx::Crush, 480, from, to));
}

// A disc of radius 3 at (10, 5): row 5 spans 7..13, row 8 only x 10, row 9 nothing.
void test_trail_row_disc() {
  PerfTrail t;
  t.add(10, 5, 3);
  uint16_t row[20];
  for (uint16_t& p : row) p = 0;
  perfTrailRow(t, 5, row, 20, 0xABCD);
  for (int x = 0; x < 20; ++x) TEST_ASSERT_EQUAL_HEX16(x >= 7 && x <= 13 ? 0xABCD : 0, row[x]);
  for (uint16_t& p : row) p = 0;
  perfTrailRow(t, 8, row, 20, 0xABCD);
  for (int x = 0; x < 20; ++x) TEST_ASSERT_EQUAL_HEX16(x == 10 ? 0xABCD : 0, row[x]);
  for (uint16_t& p : row) p = 0;
  perfTrailRow(t, 9, row, 20, 0xABCD);
  for (int x = 0; x < 20; ++x) TEST_ASSERT_EQUAL_HEX16(0, row[x]);
}

// Discs at the row's ends are clipped: nothing written outside dst.
void test_trail_row_clips() {
  PerfTrail t;
  t.add(1, 0, 4);
  t.add(18, 0, 4);
  t.add(5, 0, 0);  // radius 0: not added
  TEST_ASSERT_EQUAL(2, t.n);
  uint16_t buf[24];
  for (uint16_t& p : buf) p = 0x1111;
  perfTrailRow(t, 0, buf + 2, 20, 0xFFFF);
  TEST_ASSERT_EQUAL_HEX16(0x1111, buf[0]);
  TEST_ASSERT_EQUAL_HEX16(0x1111, buf[1]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, buf[2]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, buf[21]);
  TEST_ASSERT_EQUAL_HEX16(0x1111, buf[22]);
  TEST_ASSERT_EQUAL_HEX16(0x1111, buf[23]);
}

void test_trail_keeps_at_most_max() {
  PerfTrail t;
  for (int i = 0; i < PerfTrail::kMax + 5; ++i) t.add(i, i, 2);
  TEST_ASSERT_EQUAL(PerfTrail::kMax, t.n);
}

// Project::perfXy as the pad plays it: None / out of range = None; Y on X's effect, or a buffer
// effect on both (one read head: re-pressing Y would take it from X on every move), = None.
static int pf(PerfFx f) { return static_cast<int>(f); }

void test_xy_axis_effective() {
  auto axis = [](uint8_t x, uint8_t y, int a) {
    const uint8_t xy[2] = {x, y};
    return static_cast<int>(perfXyAxis(xy, a));
  };
  const uint8_t flt = static_cast<uint8_t>(PerfFx::Filter), crs = static_cast<uint8_t>(PerfFx::Crush);
  const uint8_t rpt = static_cast<uint8_t>(PerfFx::Repeat), rol = static_cast<uint8_t>(PerfFx::Roll);
  const int none = static_cast<int>(PerfFx::None);
  TEST_ASSERT_EQUAL(flt, axis(flt, crs, 0));
  TEST_ASSERT_EQUAL(crs, axis(flt, crs, 1));
  TEST_ASSERT_EQUAL(none, axis(0, crs, 0));
  TEST_ASSERT_EQUAL(crs, axis(0, crs, 1));
  TEST_ASSERT_EQUAL(none, axis(200, crs, 0));
  TEST_ASSERT_EQUAL(none, axis(crs, 200, 1));
  TEST_ASSERT_EQUAL(none, axis(crs, static_cast<uint8_t>(PerfFx::Count), 1));
  TEST_ASSERT_EQUAL(flt, axis(flt, flt, 0));
  TEST_ASSERT_EQUAL(none, axis(flt, flt, 1));  // X drives it
  TEST_ASSERT_EQUAL(rpt, axis(rpt, rol, 0));
  TEST_ASSERT_EQUAL(none, axis(rpt, rol, 1));  // both buffered (one read head)
  TEST_ASSERT_EQUAL(pf(PerfFx::Gate), axis(rpt, static_cast<uint8_t>(PerfFx::Gate), 1));  // GATE is not a buffer effect
  TEST_ASSERT_EQUAL(rol, axis(0, rol, 1));
  TEST_ASSERT_EQUAL(rol, axis(200, rol, 1));
  TEST_ASSERT_EQUAL(flt, axis(rpt, flt, 1));
  TEST_ASSERT_EQUAL(rpt, axis(flt, rpt, 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_axis_spans_every_effect_on_both_pads);
  RUN_TEST(test_axis_clamps_and_none);
  RUN_TEST(test_axis_list_zones);
  RUN_TEST(test_axis_filter_bipolar_dead_zone);
  RUN_TEST(test_trail_row_disc);
  RUN_TEST(test_trail_row_clips);
  RUN_TEST(test_trail_keeps_at_most_max);
  RUN_TEST(test_xy_axis_effective);
  return UNITY_END();
}
