#include <unity.h>
#include <string.h>
#include "euclid.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Renders euclid(hits, steps, rot) as "x..x" text.
static void pat(int hits, int steps, int rot, char* buf) {
  bool out[128];
  euclid(hits, steps, rot, out);
  for (int i = 0; i < steps; ++i) buf[i] = out[i] ? 'x' : '.';
  buf[steps] = 0;
}

static void expectPat(const char* want, int hits, int steps, int rot = 0) {
  char buf[130];
  pat(hits, steps, rot, buf);
  TEST_ASSERT_EQUAL_STRING(want, buf);
}

void test_references() {
  expectPat("x..x..x.", 3, 8);
  expectPat("x.xx.xx.", 5, 8);
  expectPat("x.x..", 2, 5);
  expectPat("x...x...x...x...", 4, 16);
  expectPat("x..x.x.x..x.x.x.", 7, 16);
  expectPat("x.x.x.x.", 4, 8);
  expectPat("x", 1, 1);
  expectPat("x....", 1, 5);
}

void test_zero_and_full() {
  expectPat("........", 0, 8);
  expectPat("xxxxxxxx", 8, 8);
  expectPat("xxxxx", 9, 5);   // hits clamped to steps
  expectPat(".....", -2, 5);  // negative clamped to 0
}

void test_rotation() {
  expectPat(".x..x..x", 3, 8, 1);
  expectPat(".x..x.x.", 3, 8, -2);  // left by 2
}

void test_rotation_wraps() {
  expectPat("x..x..x.", 3, 8, 8);
  expectPat(".x..x..x", 3, 8, 9);
}

void test_euclid_128_steps() {
  bool out[128];
  euclid(37, 128, 0, out);
  int cnt = 0, last = -1, minGap = 1000, maxGap = 0;
  for (int i = 0; i < 128; ++i)
    if (out[i]) {
      if (last >= 0) {
        const int g = i - last;
        if (g < minGap) minGap = g;
        if (g > maxGap) maxGap = g;
      }
      last = i;
      ++cnt;
    }
  TEST_ASSERT_EQUAL(37, cnt);
  TEST_ASSERT_TRUE(out[0]);
  TEST_ASSERT_EQUAL(3, minGap);
  TEST_ASSERT_EQUAL(4, maxGap);

  euclid(1, 128, 127, out);
  TEST_ASSERT_TRUE(out[127]);
  TEST_ASSERT_FALSE(out[0]);
}

void test_euclid_hits_above_steps() {
  bool out[128];
  euclid(200, 128, 0, out);
  for (int i = 0; i < 128; ++i) TEST_ASSERT_TRUE(out[i]);
  euclid(300, 300, 0, out);  // steps clamped to 128
  for (int i = 0; i < 128; ++i) TEST_ASSERT_TRUE(out[i]);
  expectPat("xxx", 7, 3);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_references);
  RUN_TEST(test_zero_and_full);
  RUN_TEST(test_rotation);
  RUN_TEST(test_rotation_wraps);
  RUN_TEST(test_euclid_128_steps);
  RUN_TEST(test_euclid_hits_above_steps);
  return UNITY_END();
}
