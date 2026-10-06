#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_delay.h"

using namespace mt;

constexpr uint32_t kLen = 1000;
static int16_t line[kLen];
static Delay* d;

void setUp() {
  d = new Delay();
  d->setBuffer(line, kLen);
}
void tearDown() { delete d; }

// Runs n samples with an impulse of height a at sample 0 (none if at < 0), collects the mix.
static void run(float* mix, int n, uint32_t delay, uint8_t fb, uint8_t tone, uint8_t level, float a = 1.f) {
  float send[64];
  for (int i = 0; i < n; i += 64) {
    const int k = n - i < 64 ? n - i : 64;
    for (int j = 0; j < k; ++j) send[j] = (i + j == 0) ? a : 0.f;
    for (int j = 0; j < k; ++j) mix[i + j] = 0;
    d->process(send, mix + i, k, delay, fb, tone, level);
  }
}

static int peakAt(const float* mix, int from, int to) {
  int best = from;
  for (int i = from; i < to; ++i)
    if (fabsf(mix[i]) > fabsf(mix[best])) best = i;
  return best;
}

void test_impulse_returns_after_delay() {
  static float mix[512];
  run(mix, 512, 100, 0, 127, 127);
  for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, mix[i]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.f, mix[100]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.f, mix[101]);
}

void test_level_scales_the_return() {
  static float mix[256];
  run(mix, 256, 50, 0, 127, 64);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 64.f / 127.f, mix[50]);
}

void test_no_feedback_one_echo() {
  static float mix[512];
  run(mix, 512, 100, 0, 127, 127);
  for (int i = 101; i < 512; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.f, mix[i]);
}

void test_feedback_repeats_decaying() {
  static float mix[512];
  run(mix, 512, 100, 127, 127, 127);
  TEST_ASSERT_TRUE(mix[200] > 0.5f && mix[200] < mix[100]);
  TEST_ASSERT_TRUE(mix[300] > 0.f && mix[300] < mix[200]);
}

void test_tone_darkens_the_echo() {
  static float mix[512];
  run(mix, 512, 100, 0, 0, 127);
  const int p = peakAt(mix, 0, 512);
  TEST_ASSERT_TRUE(p >= 100);
  TEST_ASSERT_TRUE(mix[p] < 0.5f);  // smeared: the energy spreads over many samples
  TEST_ASSERT_TRUE(mix[150] > 0.f);
}

void test_no_buffer_leaves_mix() {
  d->setBuffer(nullptr, 0);
  float send[8] = {1, 1, 1, 1, 1, 1, 1, 1}, mix[8] = {0.5f};
  d->process(send, mix, 8, 4, 127, 127, 127);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, mix[0]);
  for (int i = 1; i < 8; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, mix[i]);
}

void test_delay_clamped_to_buffer() {
  static float mix[2048];
  run(mix, 2048, 5000, 0, 127, 127);
  TEST_ASSERT_EQUAL(static_cast<int>(kLen - 1), peakAt(mix, 0, 2048));
}

void test_clear_silences_the_line() {
  static float mix[256];
  float send[1] = {1.f}, m[1] = {0};
  d->process(send, m, 1, 100, 127, 127, 127);
  d->clear();
  run(mix, 256, 100, 127, 127, 127, 0.f);
  for (int i = 0; i < 256; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, mix[i]);
}

void test_line_saturates_instead_of_wrapping() {
  static float mix[256];
  run(mix, 256, 100, 0, 127, 127, 10.f);  // above the line's +-4 range
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 32767.f / Delay::kScale, mix[100]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_impulse_returns_after_delay);
  RUN_TEST(test_level_scales_the_return);
  RUN_TEST(test_no_feedback_one_echo);
  RUN_TEST(test_feedback_repeats_decaying);
  RUN_TEST(test_tone_darkens_the_echo);
  RUN_TEST(test_no_buffer_leaves_mix);
  RUN_TEST(test_delay_clamped_to_buffer);
  RUN_TEST(test_clear_silences_the_line);
  RUN_TEST(test_line_saturates_instead_of_wrapping);
  return UNITY_END();
}
