#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_modfx.h"

using namespace mt;

static constexpr int kN = 128, kRate = 32000;
static constexpr uint8_t kChorus = 1, kEnsemble = 2, kFlanger = 3, kPhaser = 4;
static ModFx fx;

static int16_t fxLine[ModFx::kLineLen];
void setUp() { fx.setBuffer(fxLine); }
void tearDown() {}

static uint32_t seed = 1;
static float noise() {
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 8) * (2.f / 16777216.f) - 1.f;
}

void test_rate_mapping() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.05f, fxRateHz(0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 8.f, fxRateHz(127));
  for (int i = 0; i < 1000; ++i) {
    const float p = i / 1000.f;
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, sinf(6.2831853f * p), fxSin(p));
  }
}

void test_bypass_is_bit_exact() {
  float in[kN], l[kN], r[kN], l0[kN], r0[kN];
  for (int i = 0; i < kN; ++i) in[i] = noise(), l0[i] = l[i] = noise(), r0[i] = r[i] = noise();
  for (int b = 0; b < 8; ++b) fx.process(in, l, r, kN, 0, 60, 64, 40, 127);  // OFF
  for (uint8_t t = kChorus; t <= kPhaser; ++t) {
    fx.reset();
    for (int b = 0; b < 8; ++b) fx.process(in, l, r, kN, t, 60, 64, 40, 0);  // level 0
    float z[kN] = {};
    fx.reset();
    for (int b = 0; b < 8; ++b) fx.process(z, l, r, kN, t, 60, 64, 40, 127);  // silent send
  }
  TEST_ASSERT_EQUAL_MEMORY(l0, l, sizeof(l));
  TEST_ASSERT_EQUAL_MEMORY(r0, r, sizeof(r));
}

// Every type: noise in -> a stereo return (L != R), bounded, finite; at full fb for 10 s.
void test_each_type_stereo_bounded() {
  for (uint8_t t = kChorus; t <= kPhaser; ++t) {
    fx.reset();
    float in[kN], l[kN], r[kN];
    double diff = 0, pow = 0;
    float peak = 0;
    for (int b = 0; b < 10 * kRate / kN; ++b) {
      for (int i = 0; i < kN; ++i) in[i] = noise(), l[i] = r[i] = 0;
      fx.process(in, l, r, kN, t, 90, 127, 127, 127);
      for (int i = 0; i < kN; ++i) {
        TEST_ASSERT_FALSE(isnan(l[i]) || isnan(r[i]) || isinf(l[i]) || isinf(r[i]));
        diff += (l[i] - r[i]) * (l[i] - r[i]);
        pow += l[i] * l[i];
        peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i])));
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff > 0.05 * pow, "return is not stereo");
    TEST_ASSERT_TRUE_MESSAGE(peak < 12.f, "return not bounded");
    TEST_ASSERT_TRUE(pow > 0);
  }
}

// Chorus: an impulse comes back after 12 ms at depth 0; with depth the echo time moves.
static void echoTimes(uint8_t depth, int* times, int count, uint8_t type = kChorus) {
  fx.reset();
  const int period = 8000;
  static float l[period], r[period];
  float in[kN];
  for (int e = 0; e < count; ++e) {
    for (int b = 0; b < period / kN + 1; ++b) {
      const int n = b == period / kN ? period % kN : kN;
      if (n == 0) break;
      for (int i = 0; i < n; ++i) in[i] = (b == 0 && i == 0) ? 1.f : 0.f;
      for (int i = 0; i < n; ++i) l[b * kN + i] = r[b * kN + i] = 0;
      fx.process(in, l + b * kN, r + b * kN, n, type, 75, depth, 0, 127);
    }
    int best = 0;
    for (int i = 0; i < period; ++i)
      if (fabsf(l[i]) > fabsf(l[best])) best = i;
    times[e] = best;
  }
}

void test_chorus_modulates_delay() {
  int t[12];
  echoTimes(0, t, 4);
  for (int e = 0; e < 4; ++e) TEST_ASSERT_EQUAL_INT(384, t[e]);
  echoTimes(127, t, 12);
  int lo = t[0], hi = t[0];
  for (int e = 0; e < 12; ++e) {
    lo = t[e] < lo ? t[e] : lo;
    hi = t[e] > hi ? t[e] : hi;
    TEST_ASSERT_TRUE(t[e] >= 384 - 193 && t[e] <= 384 + 193);
  }
  TEST_ASSERT_TRUE(hi - lo > 100);
  echoTimes(0, t, 2, kEnsemble);  // ensemble: 10 ms base
  TEST_ASSERT_EQUAL_INT(320, t[0]);
  echoTimes(127, t, 12, kEnsemble);
  lo = hi = t[0];
  for (int e = 0; e < 12; ++e) lo = t[e] < lo ? t[e] : lo, hi = t[e] > hi ? t[e] : hi;
  TEST_ASSERT_TRUE(hi - lo > 60);
}

// Amplitude ratio (max / min of the 256-sample RMS) of dry + return for a 1 kHz sine over 4 s.
static float sweepRatio(uint8_t type, uint8_t depth) {
  fx.reset();
  float in[kN], l[kN], r[kN];
  float mx = 0, mn = 1e9f;
  double acc = 0;
  int cnt = 0;
  for (int b = 0; b < 4 * kRate / kN; ++b) {
    for (int i = 0; i < kN; ++i) {
      in[i] = 0.5f * sinf(6.2831853f * 1000.f * (b * kN + i) / kRate);
      l[i] = r[i] = in[i];  // the dry track already in the mix
    }
    fx.process(in, l, r, kN, type, 75, depth, 0, 127);
    if (b < 10) continue;
    for (int i = 0; i < kN; ++i) acc += l[i] * l[i];
    if ((cnt += kN) >= 256) {
      const float rms = sqrtf(static_cast<float>(acc / cnt));
      mx = fmaxf(mx, rms);
      mn = fminf(mn, rms);
      acc = 0;
      cnt = 0;
    }
  }
  return mx / (mn > 1e-9f ? mn : 1e-9f);
}

void test_phaser_notch_moves() {
  TEST_ASSERT_TRUE(sweepRatio(kPhaser, 0) < 1.05f);
  TEST_ASSERT_TRUE(sweepRatio(kPhaser, 127) > 5.f);
}

void test_flanger_sweeps() {
  TEST_ASSERT_TRUE(sweepRatio(kFlanger, 0) < 1.05f);
  TEST_ASSERT_TRUE(sweepRatio(kFlanger, 127) > 5.f);
}

void test_goes_idle_after_silence() {
  for (uint8_t t = kChorus; t <= kPhaser; ++t) {
    fx.reset();
    float in[kN] = {}, l[kN] = {}, r[kN] = {};
    in[0] = 1.f;
    fx.process(in, l, r, kN, t, 60, 100, 100, 127);
    TEST_ASSERT_FALSE(fx.idle());
    in[0] = 0;
    int b = 0;
    for (; b < 2000 && !fx.idle(); ++b) fx.process(in, l, r, kN, t, 60, 100, 100, 127);
    TEST_ASSERT_TRUE(fx.idle());
    float l0[kN], r0[kN];
    for (int i = 0; i < kN; ++i) l0[i] = l[i] = noise(), r0[i] = r[i] = noise();
    fx.process(in, l, r, kN, t, 60, 100, 100, 127);
    TEST_ASSERT_EQUAL_MEMORY(l0, l, sizeof(l));
    TEST_ASSERT_EQUAL_MEMORY(r0, r, sizeof(r));
  }
}

// Type change: the old type fades out in 64 samples, the line is cleared: with a silent send the
// new type returns nothing after the fade.
void test_type_change_fades_and_clears() {
  float in[kN], l[kN], r[kN];
  for (int b = 0; b < 50; ++b) {
    for (int i = 0; i < kN; ++i) in[i] = noise(), l[i] = r[i] = 0;
    fx.process(in, l, r, kN, kChorus, 60, 100, 0, 127);
  }
  float last = l[kN - 1];
  for (int i = 0; i < kN; ++i) in[i] = 0, l[i] = r[i] = 0;
  fx.process(in, l, r, kN, kFlanger, 60, 100, 100, 127);
  TEST_ASSERT_TRUE(fabsf(l[0] - last) < 0.5f);
  for (int i = 0; i < 64; ++i) TEST_ASSERT_TRUE(fabsf(l[i]) <= 1.5f * (64 - i) / 64.f + 1e-6f);
  for (int i = 64; i < kN; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, l[i]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ModFxType::Flanger), static_cast<int>(fx.type()));
  for (int i = 0; i < kN; ++i) l[i] = r[i] = 0;
  fx.process(in, l, r, kN, kFlanger, 60, 100, 100, 127);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, l[i]);
}

// Max sample-to-sample step of the return of a 200 Hz sine while `change` moves a param mid-run.
static float maxStep(uint8_t type, int change) {
  fx.reset();
  float in[kN], l[kN], r[kN], prev = 0, step = 0;
  for (int b = 0; b < 200; ++b) {
    for (int i = 0; i < kN; ++i) in[i] = 0.5f * sinf(6.2831853f * 200.f * (b * kN + i) / kRate), l[i] = r[i] = 0;
    const bool after = b >= 100;
    const uint8_t depth = (change == 1 && after) ? 127 : 0;
    const uint8_t level = (change == 2 && !after) ? 0 : 127;
    const uint8_t fb = (change == 3 && after) ? 127 : 0;
    fx.process(in, l, r, kN, type, 60, depth, fb, level);
    for (int i = 0; i < kN; ++i) {
      if (b > 20) step = fmaxf(step, fabsf(l[i] - prev));
      prev = l[i];
    }
  }
  return step;
}

void test_param_changes_are_smooth() {
  // 200 Hz, 0.5: the sine's own step is 0.0196; a click would be ~0.5.
  for (uint8_t t = kChorus; t <= kPhaser; ++t) {
    TEST_ASSERT_TRUE(maxStep(t, 0) < 0.04f);
    TEST_ASSERT_TRUE(maxStep(t, 1) < 0.08f);  // depth jump
    TEST_ASSERT_TRUE(maxStep(t, 2) < 0.08f);  // level 0 -> 127
    TEST_ASSERT_TRUE(maxStep(t, 3) < 0.2f);   // fb jump (resonance grows the signal)
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rate_mapping);
  RUN_TEST(test_bypass_is_bit_exact);
  RUN_TEST(test_each_type_stereo_bounded);
  RUN_TEST(test_chorus_modulates_delay);
  RUN_TEST(test_phaser_notch_moves);
  RUN_TEST(test_flanger_sweeps);
  RUN_TEST(test_goes_idle_after_silence);
  RUN_TEST(test_type_change_fades_and_clears);
  RUN_TEST(test_param_changes_are_smooth);
  return UNITY_END();
}
