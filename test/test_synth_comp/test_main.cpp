#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_comp.h"
#include "synth_osc.h"

using namespace mt;

static constexpr int kN = 128;
static constexpr float kRate = kSynthRate;

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
    if (b >= blocks - 40)  // the last 160 ms
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
    float mix[kN], r[kN], sc[kN];
    for (int i = 0; i < kN; ++i) {
      mix[i] = r[i] = 0.1f * sinf(6.2831853f * 200.f * (b * kN + i) / kRate);
      sc[i] = b >= 100 && b < 110 ? 1.f : 0.f;  // a 40 ms hit
    }
    c.process(mix, r, sc, sc, kN, 64, 50, 127);
    for (float x : mix) {
      if (b >= 80 && b < 100) before = fabsf(x) > before ? fabsf(x) : before;
      if (b >= 103 && b < 110) during = fabsf(x) > during ? fabsf(x) : during;
    }
  }
  TEST_ASSERT_TRUE(20.f * log10f(during / before) < -6.f);
}

// Equal sides give the old mono compressor's output on both: fixed samples recorded from the mono
// overload before it went (re-recorded at 44.1 kHz).
void test_equal_sides_match_old_mono_output() {
  Compressor s;
  for (int b = 0; b < 100; ++b) {
    float l[kN], r[kN], sc[kN];
    for (int i = 0; i < kN; ++i) {
      const int t = b * kN + i;
      l[i] = r[i] = (b < 50 ? 1.f : 0.1f) * sinf(6.2831853f * 200.f * t / kRate);
      sc[i] = b >= 60 && b < 70 ? 0.8f : 0.f;
    }
    s.process(l, r, sc, sc, kN, 80, 40, 100);
    TEST_ASSERT_EQUAL_MEMORY(l, r, sizeof(l));
    static const int kBlocks[4] = {5, 49, 65, 99};  // l[3], l[50], l[127] of these blocks
    static const float kWant[4][3] = {{-0.15463981f, 0.224811286f, 0.0413839072f},
                                      {0.079466708f, -0.270260781f, 0.0390807688f},
                                      {-0.0144225145f, -0.00365826231f, 0.013458468f},
                                      {0.00369930058f, -0.0330874883f, 0.0100235287f}};
    for (int k = 0; k < 4; ++k)
      if (b == kBlocks[k]) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, kWant[k][0], l[3]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, kWant[k][1], l[50]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, kWant[k][2], l[127]);
      }
  }
}

// One gain for both sides: a quiet side is scaled exactly like the loud one (r = l x 2^-7, an exact
// scale, so equal gains give r_out == l_out x 2^-7 bit for bit).
void test_stereo_linked_gain() {
  Compressor c;
  for (int b = 0; b < 100; ++b) {
    float l[kN], r[kN];
    for (int i = 0; i < kN; ++i) {
      l[i] = sinf(6.2831853f * 200.f * (b * kN + i) / kRate);
      r[i] = l[i] * 0.0078125f;
    }
    c.process(l, r, nullptr, nullptr, kN, 64, 50, 0);
    for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(r[i] == l[i] * 0.0078125f);
  }
  TEST_ASSERT_TRUE(c.gainDb() < 0);  // the loud side is compressed, the quiet one follows
}

void test_stereo_sidechain_ducks_both() {
  Compressor c;
  float bl = 0, br = 0, dl = 0, dr = 0;
  for (int b = 0; b < 200; ++b) {
    float l[kN], r[kN], scL[kN], scR[kN];
    for (int i = 0; i < kN; ++i) {
      const float s = sinf(6.2831853f * 200.f * (b * kN + i) / kRate);
      l[i] = 0.1f * s;
      r[i] = 0.05f * s;
      scL[i] = 0.f;
      scR[i] = b >= 100 && b < 110 ? 1.f : 0.f;  // a 40 ms hit on the right sidechain
    }
    c.process(l, r, scL, scR, kN, 64, 50, 127);
    for (int i = 0; i < kN; ++i) {
      if (b >= 80 && b < 100) {
        bl = fmaxf(bl, fabsf(l[i]));
        br = fmaxf(br, fabsf(r[i]));
      }
      if (b >= 103 && b < 110) {
        dl = fmaxf(dl, fabsf(l[i]));
        dr = fmaxf(dr, fabsf(r[i]));
      }
    }
  }
  TEST_ASSERT_TRUE(20.f * log10f(dl / bl) < -6.f);
  TEST_ASSERT_TRUE(20.f * log10f(dr / br) < -6.f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bypass_bit_exact);
  RUN_TEST(test_below_threshold_unity_plus_makeup);
  RUN_TEST(test_above_threshold_reduces);
  RUN_TEST(test_sidechain_ducks);
  RUN_TEST(test_equal_sides_match_old_mono_output);
  RUN_TEST(test_stereo_linked_gain);
  RUN_TEST(test_stereo_sidechain_ducks_both);
  return UNITY_END();
}
