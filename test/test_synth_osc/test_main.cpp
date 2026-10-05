#include <math.h>
#include <unity.h>
#include "synth_osc.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_note_hz() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 440.f, noteHz(69));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 880.f, noteHz(81));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 261.63f, noteHz(60));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 452.89f, noteHz(69.5f));
}

void test_pulse_duty() {
  ChipOsc o;
  int high = 0;
  for (int i = 0; i < kSynthRate; ++i) {
    const float v = o.next(Wave::Pulse, 1000.f / kSynthRate, 0.25f);
    TEST_ASSERT_TRUE(v == 1.f || v == -1.f);
    if (v > 0) ++high;
  }
  TEST_ASSERT_INT_WITHIN(kSynthRate / 100, kSynthRate / 4, high);
}

static int downCrossings(Wave w, float hz) {
  ChipOsc o;
  float prev = o.next(w, hz / kSynthRate, 0.5f);
  int n = 0;
  for (int i = 1; i < kSynthRate; ++i) {
    const float v = o.next(w, hz / kSynthRate, 0.5f);
    if (prev >= 0 && v < 0) ++n;
    prev = v;
  }
  return n;
}

void test_saw_frequency() {
  TEST_ASSERT_INT_WITHIN(1, 1000, downCrossings(Wave::Saw, 1000.f));
  TEST_ASSERT_INT_WITHIN(1, 440, downCrossings(Wave::Saw, 440.f));
}

void test_triangle_range() {
  ChipOsc o;
  float mx = -2, mn = 2, sum = 0;
  for (int i = 0; i < kSynthRate; ++i) {
    const float v = o.next(Wave::Triangle, 100.f / kSynthRate, 0.5f);
    mx = v > mx ? v : mx;
    mn = v < mn ? v : mn;
    sum += v;
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, mx);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.f, mn);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, sum / kSynthRate);
  TEST_ASSERT_INT_WITHIN(1, 100, downCrossings(Wave::Triangle, 100.f));
}

void test_noise_no_dc() {
  ChipOsc o;
  float sum = 0;
  int changes = 0;
  float prev = 0;
  for (int i = 0; i < kSynthRate; ++i) {
    const float v = o.next(Wave::Noise, 0.5f, 0.5f);
    TEST_ASSERT_TRUE(v == 1.f || v == -1.f);
    if (i > 0 && v != prev) ++changes;
    prev = v;
    sum += v;
  }
  TEST_ASSERT_TRUE(fabsf(sum) < 0.05f * kSynthRate);
  TEST_ASSERT_TRUE(changes > 1000);  // it is noise, not a constant
}

void test_metal_period_93() {
  ChipOsc o;
  float seq[400];
  for (int i = 0; i < 400; ++i) seq[i] = o.next(Wave::Metal, 1.f, 0.5f);  // one LFSR step per sample
  for (int i = 0; i < 300; ++i) TEST_ASSERT_EQUAL_FLOAT(seq[i], seq[i + 93]);
  bool differs31 = false;
  for (int i = 0; i < 300; ++i) differs31 = differs31 || seq[i] != seq[i + 31];
  TEST_ASSERT_TRUE(differs31);
}

void test_noise_rate_follows_frequency() {
  // Low pitch: the value holds for many samples.
  ChipOsc o;
  int changes = 0;
  float prev = o.next(Wave::Noise, 100.f / kSynthRate, 0.5f);
  for (int i = 1; i < kSynthRate; ++i) {
    const float v = o.next(Wave::Noise, 100.f / kSynthRate, 0.5f);
    if (v != prev) ++changes;
    prev = v;
  }
  TEST_ASSERT_TRUE(changes <= 100);
}

void test_wavetables_normalized() {
  for (int n = 0; n < kWavetables; ++n) {
    int mx = -128, mn = 128, peak = 0;
    for (int i = 0; i < kWtLen; ++i) {
      const int v = kWavetable[n][i];
      mx = v > mx ? v : mx;
      mn = v < mn ? v : mn;
      peak = (v < 0 ? -v : v) > peak ? (v < 0 ? -v : v) : peak;
    }
    TEST_ASSERT_TRUE(mx <= 127);
    TEST_ASSERT_TRUE(mn >= -127);
    TEST_ASSERT_TRUE(peak >= 100);
  }
}

void test_wavetable_playback() {
  ChipOsc o;
  const Wave w = static_cast<Wave>(static_cast<int>(Wave::Wt1) + 0);  // sine
  // inc 1/32: one table entry per sample.
  for (int i = 0; i < kWtLen; ++i)
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, kWavetable[0][i] / 127.f, o.next(w, 1.f / kWtLen, 0.5f));
  TEST_ASSERT_INT_WITHIN(1, 500, downCrossings(w, 500.f));
  const Wave last = static_cast<Wave>(static_cast<int>(Wave::Wt1) + kWavetables - 1);
  ChipOsc o2;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, kWavetable[kWavetables - 1][0] / 127.f, o2.next(last, 0.01f, 0.5f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_note_hz);
  RUN_TEST(test_pulse_duty);
  RUN_TEST(test_saw_frequency);
  RUN_TEST(test_triangle_range);
  RUN_TEST(test_noise_no_dc);
  RUN_TEST(test_metal_period_93);
  RUN_TEST(test_noise_rate_follows_frequency);
  RUN_TEST(test_wavetables_normalized);
  RUN_TEST(test_wavetable_playback);
  return UNITY_END();
}
