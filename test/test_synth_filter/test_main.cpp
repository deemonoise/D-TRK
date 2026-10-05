#include <math.h>
#include <unity.h>
#include "synth_filter.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// RMS of a sine at hz through the filter, after the transient.
static float rmsThrough(Svf::Mode m, float cutoff, float q, float hz) {
  Svf f;
  f.set(m, cutoff, q);
  double acc = 0;
  const int n = kSynthRate / 4;
  for (int i = 0; i < n; ++i) {
    const float y = f.process(sinf(6.2831853f * hz * i / kSynthRate));
    if (i >= n / 2) acc += y * y;
  }
  return sqrtf(static_cast<float>(acc / (n / 2)));
}

void test_lp_passes_low_cuts_high() {
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.707f, rmsThrough(Svf::Mode::Lp, 1000, 0.707f, 100));
  TEST_ASSERT_TRUE(rmsThrough(Svf::Mode::Lp, 1000, 0.707f, 8000) < 0.05f);
}

void test_hp_passes_high_cuts_low() {
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.707f, rmsThrough(Svf::Mode::Hp, 1000, 0.707f, 8000));
  TEST_ASSERT_TRUE(rmsThrough(Svf::Mode::Hp, 1000, 0.707f, 100) < 0.05f);
}

void test_bp_peaks_at_cutoff() {
  const float at = rmsThrough(Svf::Mode::Bp, 1000, 2, 1000);
  TEST_ASSERT_TRUE(at > rmsThrough(Svf::Mode::Bp, 1000, 2, 200));
  TEST_ASSERT_TRUE(at > rmsThrough(Svf::Mode::Bp, 1000, 2, 5000));
}

void test_stable_at_extremes() {
  Svf f;
  f.set(Svf::Mode::Lp, 1e6f, 100);  // clamped below Nyquist
  float y = 0;
  for (int i = 0; i < kSynthRate; ++i) y = f.process((i & 1) ? 1.f : -1.f);
  TEST_ASSERT_FALSE(isnan(y) || isinf(y));
  TEST_ASSERT_TRUE(fabsf(y) < 200);
  f.set(Svf::Mode::Lp, 0, 0);  // clamped to 20 Hz, q 0.5
  for (int i = 0; i < 1000; ++i) y = f.process(1);
  TEST_ASSERT_FALSE(isnan(y));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lp_passes_low_cuts_high);
  RUN_TEST(test_hp_passes_high_cuts_low);
  RUN_TEST(test_bp_peaks_at_cutoff);
  RUN_TEST(test_stable_at_extremes);
  return UNITY_END();
}
