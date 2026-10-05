#include <unity.h>
#include "oneshot.h"

using namespace mt;

static int16_t data[64];
static int16_t out[256];

void setUp() {
  for (int i = 0; i < 64; ++i) data[i] = static_cast<int16_t>(100 * (i + 1));
  for (auto& x : out) x = 0;
}
void tearDown() {}

void test_idle_adds_nothing() {
  OneShot o;
  TEST_ASSERT_FALSE(o.playing());
  o.mix(out, 16, 1.f);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_INT16(0, out[i]);
}

void test_full_rate_copies_then_stops() {
  OneShot o;
  o.start(data, 64, kSynthRate);
  TEST_ASSERT_TRUE(o.playing());
  o.mix(out, 100, 1.f);
  for (int i = 0; i < 64; ++i) TEST_ASSERT_EQUAL_INT16(data[i], out[i]);
  for (int i = 64; i < 100; ++i) TEST_ASSERT_EQUAL_INT16(0, out[i]);
  TEST_ASSERT_FALSE(o.playing());
}

void test_half_rate_interpolates_twice_as_long() {
  OneShot o;
  o.start(data, 64, kSynthRate / 2);
  o.mix(out, 256, 1.f);
  TEST_ASSERT_EQUAL_INT16(100, out[0]);
  TEST_ASSERT_EQUAL_INT16(150, out[1]);  // halfway between frames 0 and 1
  TEST_ASSERT_EQUAL_INT16(200, out[2]);
  TEST_ASSERT_EQUAL_INT16(6400, out[126]);
  TEST_ASSERT_EQUAL_INT16(0, out[128]);
  TEST_ASSERT_FALSE(o.playing());
}

void test_mix_adds_with_gain_and_saturates() {
  OneShot o;
  o.start(data, 64, kSynthRate);
  out[0] = 1000;
  out[1] = 32700;
  o.mix(out, 2, 0.5f);
  TEST_ASSERT_EQUAL_INT16(1050, out[0]);
  TEST_ASSERT_EQUAL_INT16(32767, out[1]);
}

void test_resumes_across_blocks() {
  OneShot o;
  o.start(data, 64, kSynthRate);
  o.mix(out, 10, 1.f);
  o.mix(out + 10, 10, 1.f);
  for (int i = 0; i < 20; ++i) TEST_ASSERT_EQUAL_INT16(data[i], out[i]);
}

void test_stop_silences() {
  OneShot o;
  o.start(data, 64, kSynthRate);
  o.stop();
  TEST_ASSERT_FALSE(o.playing());
  o.mix(out, 16, 1.f);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_INT16(0, out[i]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_adds_nothing);
  RUN_TEST(test_full_rate_copies_then_stops);
  RUN_TEST(test_half_rate_interpolates_twice_as_long);
  RUN_TEST(test_mix_adds_with_gain_and_saturates);
  RUN_TEST(test_resumes_across_blocks);
  RUN_TEST(test_stop_silences);
  return UNITY_END();
}
