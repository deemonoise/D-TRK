#include <math.h>
#include <string.h>
#include <unity.h>
#include "onset.h"
#include "slices.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kRate = 32000;
constexpr uint32_t kLen = 64000;
int32_t acc[kLen];
int16_t data[kLen];
Onset o[kMaxOnsets];
uint32_t seed = 1;

int32_t rnd(int32_t amp) {  // LCG, -amp..amp
  seed = seed * 1664525u + 1013904223u;
  return static_cast<int32_t>((seed >> 8) % (2 * amp + 1)) - amp;
}

void clear(bool noise) {
  seed = 1;
  for (uint32_t i = 0; i < kLen; ++i) acc[i] = noise ? rnd(20) : 0;
}

// Decaying burst: |x| = amp * e^(-t/800), random sign.
void burst(uint32_t start, float amp) {
  for (uint32_t i = start; i < kLen; ++i) {
    const float a = amp * expf(-static_cast<float>(i - start) / 800.f);
    if (a < 1.f) break;
    acc[i] += static_cast<int32_t>(rnd(1) >= 0 ? a : -a);
  }
}

int detect(int max = kMaxOnsets) {
  for (uint32_t i = 0; i < kLen; ++i)
    data[i] = static_cast<int16_t>(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
  return detectOnsets(data, kLen, kRate, o, max);
}

}  // namespace

void test_silence_has_no_onsets() {
  clear(false);
  TEST_ASSERT_EQUAL(0, detect());
}

void test_noise_floor_has_no_onsets() {
  clear(true);
  TEST_ASSERT_EQUAL(0, detect());
}

void test_clicks_found_at_their_positions() {
  clear(true);
  const uint32_t starts[3] = {4000, 16000, 40000};
  for (uint32_t s : starts) burst(s, 20000.f);
  TEST_ASSERT_EQUAL(3, detect());
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_UINT32_WITHIN(64, starts[i], o[i].pos);
    TEST_ASSERT_TRUE(o[i].pos <= starts[i] + 32);
  }
}

void test_unaligned_click_refined_back() {
  clear(true);
  burst(10017, 15000.f);
  TEST_ASSERT_EQUAL(1, detect());
  TEST_ASSERT_TRUE(o[0].pos <= 10017 && o[0].pos + 32 > 10017);
}

void test_strengths_normalized() {
  clear(true);
  burst(4000, 20000.f);
  burst(30000, 4000.f);
  TEST_ASSERT_EQUAL(2, detect());
  TEST_ASSERT_EQUAL(1000, o[0].strength);
  TEST_ASSERT_TRUE(o[1].strength < 1000);
  TEST_ASSERT_TRUE(o[1].strength > 0);
}

void test_close_peaks_merged() {
  clear(true);
  burst(8000, 20000.f);
  burst(8400, 20000.f);
  TEST_ASSERT_EQUAL(1, detect());
  TEST_ASSERT_UINT32_WITHIN(64, 8000, o[0].pos);
}

void test_max_keeps_strongest() {
  clear(true);
  uint32_t starts[10];
  for (int i = 0; i < 10; ++i) {
    starts[i] = 2048 + static_cast<uint32_t>(i) * 6144;
    burst(starts[i], 20000.f / powf(1.4f, static_cast<float>(9 - i)));
  }
  TEST_ASSERT_EQUAL(10, detect());
  TEST_ASSERT_EQUAL(4, detect(4));
  for (int i = 0; i < 4; ++i) TEST_ASSERT_UINT32_WITHIN(64, starts[6 + i], o[i].pos);
}

void test_nearest_and_step() {
  const Onset h[3] = {{100, 1}, {500, 1}, {900, 1}};
  TEST_ASSERT_EQUAL(1, nearestOnset(h, 3, 480));
  TEST_ASSERT_EQUAL(0, nearestOnset(h, 3, 0));
  TEST_ASSERT_EQUAL(2, nearestOnset(h, 3, 5000));
  TEST_ASSERT_EQUAL(-1, nearestOnset(h, 0, 480));
  TEST_ASSERT_EQUAL(2, stepOnset(h, 3, 500, +1));
  TEST_ASSERT_EQUAL(0, stepOnset(h, 3, 500, -1));
  TEST_ASSERT_EQUAL(-1, stepOnset(h, 3, 900, +1));
  TEST_ASSERT_EQUAL(-1, stepOnset(h, 3, 100, -1));
  TEST_ASSERT_EQUAL(1, stepOnset(h, 3, 300, +1));
}

void test_chop_transients_threshold() {
  const Onset h[3] = {{8000, 1000}, {16000, 300}, {24000, 800}};
  const int expect[3][2] = {{100, 4}, {50, 3}, {0, 2}};
  for (const auto& e : expect) {
    Instrument m;
    m.start = 0;
    m.end = 0xFFFF;
    m.chopThresh = static_cast<uint8_t>(e[0]);
    chopTransients(m, h, 3, 32000, 32000);
    TEST_ASSERT_EQUAL(e[1], m.sliceCount);
    TEST_ASSERT_EQUAL(0, m.slices[0]);
    for (int i = 1; i < m.sliceCount; ++i) TEST_ASSERT_TRUE(m.slices[i - 1] < m.slices[i]);
  }
}

void test_chop_transients_inside_region_only() {
  const Onset h[4] = {{4000, 1000}, {8100, 1000}, {16000, 1000}, {24000, 1000}};
  Instrument m;
  m.start = 0x4000;  // frame 8000
  m.end = 0xC000;    // frame 24000
  m.chopThresh = 100;
  chopTransients(m, h, 4, 32000, 32000);
  TEST_ASSERT_EQUAL(2, m.sliceCount);
  TEST_ASSERT_EQUAL_HEX16(m.start, m.slices[0]);
  TEST_ASSERT_UINT32_WITHIN(1, 16000, fracToFrame(m.slices[1], 32000));
}

void test_chop_transients_keeps_strongest() {
  Onset h[40];
  for (int i = 0; i < 40; ++i) h[i] = {static_cast<uint32_t>(1000 + i * 1000), static_cast<uint16_t>(i + 1)};
  Instrument m;
  m.chopThresh = 100;
  m.slices[5] = 123;
  chopTransients(m, h, 40, 64000, 32000);
  TEST_ASSERT_EQUAL(kMaxSlices, m.sliceCount);
  TEST_ASSERT_EQUAL(0, m.slices[0]);
  // The 31 strongest are the last ones: frames 10000..40000.
  TEST_ASSERT_UINT32_WITHIN(1, 10000, fracToFrame(m.slices[1], 64000));
  TEST_ASSERT_UINT32_WITHIN(1, 40000, fracToFrame(m.slices[kMaxSlices - 1], 64000));
  for (int i = 1; i < m.sliceCount; ++i) TEST_ASSERT_TRUE(m.slices[i - 1] < m.slices[i]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_silence_has_no_onsets);
  RUN_TEST(test_noise_floor_has_no_onsets);
  RUN_TEST(test_clicks_found_at_their_positions);
  RUN_TEST(test_unaligned_click_refined_back);
  RUN_TEST(test_strengths_normalized);
  RUN_TEST(test_close_peaks_merged);
  RUN_TEST(test_max_keeps_strongest);
  RUN_TEST(test_nearest_and_step);
  RUN_TEST(test_chop_transients_threshold);
  RUN_TEST(test_chop_transients_inside_region_only);
  RUN_TEST(test_chop_transients_keeps_strongest);
  return UNITY_END();
}
