#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_comp.h"

using namespace mt;

static constexpr int kN = 128;
static constexpr float kRate = 44100.f;

void setUp() {}
void tearDown() {}

// Peak of a sine of amplitude amp through c after ms of signal (amt 64, release 50).
static float sinePeakAfter(Compressor& c, float amp, int ms, uint8_t amt = 64) {
  const int blocks = static_cast<int>(ms * kRate / 1000 / kN);
  float peak = 0;
  int t = 0;
  for (int b = 0; b < blocks; ++b) {
    float mix[kN], r[kN];
    for (int i = 0; i < kN; ++i, ++t) mix[i] = r[i] = amp * sinf(6.2831853f * 200.f * t / kRate);
    c.process(mix, r, nullptr, nullptr, kN, amt, 50, 0);
    TEST_ASSERT_EQUAL_MEMORY(mix, r, sizeof(r));
    if (b >= blocks - 40)  // the last 116 ms
      for (float x : mix) peak = fabsf(x) > peak ? fabsf(x) : peak;
  }
  return peak;
}

void test_bypass_bit_exact() {
  Compressor c;
  float mix[kN], r[kN], ref[kN];
  for (int i = 0; i < kN; ++i) mix[i] = r[i] = ref[i] = sinf(i * 0.1f);
  c.process(mix, r, nullptr, nullptr, kN, 0, 50, 127);
  TEST_ASSERT_EQUAL_MEMORY(ref, mix, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, r, sizeof(ref));
}

void test_below_threshold_unity_plus_makeup() {
  Compressor c;
  const float makeupDb = (6.f + 24.f * 64.f / 127.f) / 4.f;  // threshold drop / 2 / 2
  const float peak = sinePeakAfter(c, 0.1f, 500);  // -20 dB, threshold ~ -18 dB
  TEST_ASSERT_FLOAT_WITHIN(0.3f, -20.f + makeupDb, 20.f * log10f(peak));
}

void test_above_threshold_reduces() {
  Compressor c;
  const float peak = sinePeakAfter(c, 1.f, 500);  // 0 dB: -13.5 dB at 4:1, + 4.5 dB makeup
  TEST_ASSERT_FLOAT_WITHIN(1.f, -9.05f, 20.f * log10f(peak));
}

void test_sidechain_ducks() {
  Compressor c;
  float before = 0, during = 0;
  for (int b = 0; b < 200; ++b) {
    float mix[kN], r[kN], sc[kN], scR[kN] = {0};
    for (int i = 0; i < kN; ++i) {
      mix[i] = r[i] = 0.1f * sinf(6.2831853f * 200.f * (b * kN + i) / kRate);
      sc[i] = b >= 100 && b < 110 ? 1.f : 0.f;  // a 29 ms hit, left only
    }
    c.process(mix, r, sc, scR, kN, 64, 50, 127);
    for (float x : mix) {
      if (b >= 80 && b < 100) before = fabsf(x) > before ? fabsf(x) : before;
      if (b >= 103 && b < 110) during = fabsf(x) > during ? fabsf(x) : during;
    }
  }
  TEST_ASSERT_TRUE(20.f * log10f(during / before) < -6.f);
}

// The detector takes the louder channel; both get the same gain.
void test_one_loud_channel_ducks_both() {
  Compressor c;
  float l[kN], r[kN];
  for (int b = 0; b < 100; ++b) {
    for (int i = 0; i < kN; ++i) {
      l[i] = sinf(6.2831853f * 200.f * (b * kN + i) / kRate);
      r[i] = 0.1f * l[i];
    }
    c.process(l, r, nullptr, nullptr, kN, 64, 50, 0);
  }
  for (int i = 0; i < kN; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f * l[i], r[i]);
  TEST_ASSERT_TRUE(c.gainDb() < -2.f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bypass_bit_exact);
  RUN_TEST(test_below_threshold_unity_plus_makeup);
  RUN_TEST(test_above_threshold_reduces);
  RUN_TEST(test_sidechain_ducks);
  RUN_TEST(test_one_loud_channel_ducks_both);
  return UNITY_END();
}
