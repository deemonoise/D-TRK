#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_eq.h"

using namespace mt;

static constexpr int kN = 128, kRate = 32000;

void setUp() {}
void tearDown() {}

// Gain in dB of a sine at hz through the EQ (steady state, RMS ratio over the last 0.25 s).
static float gainDb(float hz, uint8_t low, uint8_t high) {
  MasterEq eq;
  const int blocks = kRate / kN;  // 1 s
  double in2 = 0, out2 = 0;
  float l[kN], r[kN];
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < kN; ++i) l[i] = r[i] = 0.5f * sinf(6.2831853f * hz * (b * kN + i) / kRate);
    float ref[kN];
    memcpy(ref, l, sizeof(ref));
    eq.process(l, r, kN, low, high);
    for (int i = 0; i < kN; ++i) TEST_ASSERT_EQUAL_FLOAT(l[i], r[i]);
    if (b >= blocks * 3 / 4)
      for (int i = 0; i < kN; ++i) in2 += ref[i] * ref[i], out2 += l[i] * l[i];
  }
  return 10.f * log10f(static_cast<float>(out2 / in2));
}

void test_db_mapping() {
  TEST_ASSERT_EQUAL_FLOAT(0.f, MasterEq::db(64));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -12.f, MasterEq::db(0));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12.f, MasterEq::db(127));
}

void test_flat_is_bit_exact() {
  MasterEq eq;
  float l[kN], r[kN], l0[kN], r0[kN];
  for (int i = 0; i < kN; ++i) l0[i] = l[i] = sinf(i * 0.37f) * 1.7f, r0[i] = r[i] = cosf(i * 0.11f);
  for (int b = 0; b < 4; ++b) eq.process(l, r, kN, 64, 64);
  TEST_ASSERT_EQUAL_MEMORY(l0, l, sizeof(l));
  TEST_ASSERT_EQUAL_MEMORY(r0, r, sizeof(r));
}

void test_back_to_flat_is_bit_exact_again() {
  MasterEq eq;
  float l[kN], r[kN];
  for (int i = 0; i < kN; ++i) l[i] = r[i] = 0.3f;
  eq.process(l, r, kN, 127, 0);
  eq.process(l, r, kN, 64, 64);  // ramps back to 0
  float l0[kN], r0[kN];
  for (int i = 0; i < kN; ++i) l0[i] = l[i] = r0[i] = r[i] = sinf(i * 0.2f);
  eq.process(l, r, kN, 64, 64);
  TEST_ASSERT_EQUAL_MEMORY(l0, l, sizeof(l));
  TEST_ASSERT_EQUAL_MEMORY(r0, r, sizeof(r));
}

void test_low_shelf_boost() {
  TEST_ASSERT_FLOAT_WITHIN(1.f, 12.f, gainDb(50, 127, 64));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.f, gainDb(8000, 127, 64));
}

void test_low_shelf_cut() {
  TEST_ASSERT_FLOAT_WITHIN(1.f, -12.f, gainDb(30, 0, 64));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.f, gainDb(8000, 0, 64));
}

void test_high_shelf_boost_and_cut() {
  TEST_ASSERT_FLOAT_WITHIN(1.f, 12.f, gainDb(12000, 64, 127));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.f, gainDb(100, 64, 127));
  TEST_ASSERT_FLOAT_WITHIN(1.f, -12.f, gainDb(12000, 64, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.f, gainDb(100, 64, 0));
}

void test_dc_and_nyquist_exact() {
  MasterEq eq;
  float l[kN], r[kN];
  for (int b = 0; b < 200; ++b) {  // DC, low +12: x 3.98
    for (int i = 0; i < kN; ++i) l[i] = r[i] = 0.1f;
    eq.process(l, r, kN, 127, 0);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.1f * powf(10.f, 12.f / 20.f), l[kN - 1]);  // high shelf: no effect at DC
  MasterEq e2;
  for (int b = 0; b < 200; ++b) {  // Nyquist, high +12, low -12: low shelf no effect
    for (int i = 0; i < kN; ++i) l[i] = r[i] = (i & 1) ? -0.1f : 0.1f;
    e2.process(l, r, kN, 0, 127);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.1f * powf(10.f, 12.f / 20.f), l[kN - 1]);
}

void test_tilt_bounded_no_nan() {
  MasterEq eq;
  float l[kN], r[kN];
  uint32_t s = 1;
  for (int b = 0; b < 500; ++b) {
    for (int i = 0; i < kN; ++i) {
      s = s * 1664525u + 1013904223u;
      l[i] = (s >> 8) * (2.f / 16777216.f) - 1.f;
      r[i] = -l[i];
    }
    eq.process(l, r, kN, static_cast<uint8_t>(b * 7 % 128), static_cast<uint8_t>(b * 13 % 128));
    for (int i = 0; i < kN; ++i) {
      TEST_ASSERT_FALSE(isnan(l[i]) || isnan(r[i]));
      TEST_ASSERT_TRUE(fabsf(l[i]) < 10.f && fabsf(r[i]) < 10.f);
    }
  }
}

// A gain jump on a steady sine: no step larger than the sine's own slope x the new gain.
void test_gain_change_is_smooth() {
  MasterEq eq;
  float l[kN], r[kN], prev = 0;
  float maxStep = 0;
  for (int b = 0; b < 100; ++b) {
    for (int i = 0; i < kN; ++i) l[i] = r[i] = 0.5f * sinf(6.2831853f * 60.f * (b * kN + i) / kRate);
    eq.process(l, r, kN, b < 50 ? 64 : 127, 64);
    for (int i = 0; i < kN; ++i) {
      if (b > 0 || i > 0) maxStep = fmaxf(maxStep, fabsf(l[i] - prev));
      prev = l[i];
    }
  }
  // 60 Hz, 0.5 amplitude: slope 0.0059 / sample; x 4 at +12 dB ~ 0.024.
  TEST_ASSERT_TRUE(maxStep < 0.03f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_db_mapping);
  RUN_TEST(test_flat_is_bit_exact);
  RUN_TEST(test_back_to_flat_is_bit_exact_again);
  RUN_TEST(test_low_shelf_boost);
  RUN_TEST(test_low_shelf_cut);
  RUN_TEST(test_high_shelf_boost_and_cut);
  RUN_TEST(test_dc_and_nyquist_exact);
  RUN_TEST(test_tilt_bounded_no_nan);
  RUN_TEST(test_gain_change_is_smooth);
  return UNITY_END();
}
