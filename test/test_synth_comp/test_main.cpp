#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_comp.h"
#include "synth_osc.h"

using namespace mt;
static constexpr int kN = 128;
void setUp() {}
void tearDown() {}

static float dc(Compressor& c, float level, int samples, uint8_t amt = 127,
                uint8_t rel = 50, float key = 0, int depth = -1) {
  float l = level, r = level;
  for (int done = 0; done < samples;) {
    float a[kN], b[kN], sc[kN];
    const int n = samples - done < kN ? samples - done : kN;
    for (int i = 0; i < n; ++i) a[i] = b[i] = level, sc[i] = key;
    c.process(a, b, depth < 0 ? nullptr : sc, depth < 0 ? nullptr : sc, n, amt, rel,
              depth < 0 ? 0 : depth);
    l = a[n - 1]; r = b[n - 1]; done += n;
  }
  TEST_ASSERT_EQUAL_FLOAT(l, r);
  return l;
}
static float db(float x) { return 20.f * log10f(x); }

void test_cold_bypass_bit_exact() {
  Compressor c;
  float l[kN], r[kN], ref[kN];
  for (int i = 0; i < kN; ++i) l[i] = r[i] = ref[i] = sinf(i * .1f);
  c.process(l, r, nullptr, nullptr, kN, 0, 50, 127);
  TEST_ASSERT_EQUAL_MEMORY(ref, l, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, r, sizeof(ref));
}
void test_below_threshold_has_no_makeup() {
  Compressor c;
  TEST_ASSERT_FLOAT_WITHIN(1e-7f, .01f, dc(c, .01f, kSynthRate));
}
void test_minimum_amount_is_nearly_unity() {
  Compressor c;
  TEST_ASSERT_TRUE(fabsf(db(dc(c, 1, kSynthRate, 1))) < .25f);
}
void test_amount_monotonic_and_never_boosts() {
  float previous = 1;
  for (int amt = 1; amt <= 127; amt += 7) {
    Compressor c;
    const float y = dc(c, 1, kSynthRate, amt);
    TEST_ASSERT_TRUE(y <= previous && y > 0);
    previous = y;
  }
  Compressor c;
  TEST_ASSERT_FLOAT_WITHIN(.03f, -13.5f, db(dc(c, 1, kSynthRate)));
}
void test_static_ratio_above_knee() {
  Compressor a, b;
  const float y1 = dc(a, powf(10.f, -12.f / 20), kSynthRate);
  const float y2 = dc(b, powf(10.f, -4.f / 20), kSynthRate);
  TEST_ASSERT_FLOAT_WITHIN(.02f, 2.f, db(y2 / y1));
}
void test_soft_knee_curve_and_boundaries() {
  const float levels[] = {-21.01f, -21.f, -20.99f, -18.f, -15.01f, -15.f, -14.99f};
  float previous = 0;
  for (float inputDb : levels) {
    Compressor c;
    const float input = powf(10.f, inputDb / 20.f);
    const float y = dc(c, input, kSynthRate);
    const float over = inputDb + 18.f;
    const float reduction = over <= -3 ? 0 : (over >= 3 ? .75f * over : .75f * (over + 3) * (over + 3) / 12.f);
    TEST_ASSERT_FLOAT_WITHIN(.015f, inputDb - reduction, db(y));
    TEST_ASSERT_TRUE(y >= previous);
    previous = y;
  }
}
void test_silent_external_key_ignores_loud_mix() {
  Compressor c;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.f, dc(c, 2, kSynthRate, 127, 50, 0, 127));
}
void test_external_key_independent_of_program_level() {
  Compressor a, b;
  const float key = .5f;
  const float quiet = (dc(a, key + .02f, kSynthRate, 127, 50, key, 127) - key) / .02f;
  const float loud = (dc(b, key + 2.f, kSynthRate, 127, 50, key, 127) - key) / 2.f;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, quiet, loud);
  TEST_ASSERT_TRUE(loud < .6f);
}
void test_external_key_remains_dry() {
  Compressor c;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, .8f, dc(c, .8f, kSynthRate, 127, 50, .8f, 127));
}
void test_sidechain_depth_scales_reduction_db() {
  Compressor a, b;
  const float key = .8f, pad = .1f;
  const float half = (dc(a, key + pad, kSynthRate, 127, 50, key, 64) - key) / pad;
  const float full = (dc(b, key + pad, kSynthRate, 127, 50, key, 127) - key) / pad;
  TEST_ASSERT_TRUE(half > 0 && half < 1 && full > 0 && full < half);
  TEST_ASSERT_FLOAT_WITHIN(.015f, db(full) * 64.f / 127.f, db(half));
}
void test_disable_ramps_then_becomes_bit_exact() {
  Compressor c;
  const float compressed = dc(c, 1, kSynthRate);
  const float first = dc(c, 1, 1, 0);
  TEST_ASSERT_TRUE(first > compressed && first - compressed < .01f);
  dc(c, 1, 512, 0);
  float l[kN], r[kN], ref[kN];
  for (int i = 0; i < kN; ++i) l[i] = r[i] = ref[i] = sinf(i * .2f);
  c.process(l, r, nullptr, nullptr, kN, 0, 50, 0);
  TEST_ASSERT_EQUAL_MEMORY(ref, l, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, r, sizeof(ref));
}
void test_reenable_does_not_restore_stale_reduction() {
  Compressor c;
  dc(c, 1, kSynthRate);
  dc(c, 0, kSynthRate, 0);
  const float y = dc(c, .001f, 1);
  TEST_ASSERT_FLOAT_WITHIN(1e-7f, .001f, y);
}
void test_reset_matches_fresh_instance_sample_exact() {
  Compressor a, b;
  dc(a, 1, 135);
  a.reset();
  float l[kN], r[kN], x[kN], y[kN];
  for (int i = 0; i < kN; ++i) l[i] = r[i] = x[i] = y[i] = .7f;
  a.process(l, r, nullptr, nullptr, kN, 64, 50, 0);
  b.process(x, y, nullptr, nullptr, kN, 64, 50, 0);
  TEST_ASSERT_EQUAL_MEMORY(x, l, sizeof(l));
  TEST_ASSERT_EQUAL_MEMORY(y, r, sizeof(r));
}
void test_stereo_linked_gain() {
  Compressor c;
  for (int b = 0; b < 100; ++b) {
    float l[kN], r[kN];
    for (int i = 0; i < kN; ++i) {
      l[i] = sinf(6.2831853f * 200 * (b * kN + i) / kSynthRate);
      r[i] = l[i] * .0078125f;
    }
    c.process(l, r, nullptr, nullptr, kN, 127, 50, 0);
    for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(r[i] == l[i] * .0078125f);
  }
}
void test_gain_interpolates_between_control_samples() {
  Compressor c;
  float l[2048], r[2048];
  for (int i = 0; i < 2048; ++i) l[i] = r[i] = 1;
  c.process(l, r, nullptr, nullptr, 2048, 127, 50, 0);
  int changing = 0, held = 0;
  for (int i = 600; i < 1500; ++i) {
    if (l[i] != l[i - 1]) ++changing; else ++held;
    TEST_ASSERT_TRUE(fabsf(l[i] - l[i - 1]) < .005f);
  }
  TEST_ASSERT_TRUE(changing > 800 && held < 100);
}
void test_program_partition_invariant() {
  constexpr int n = 4096;
  Compressor a, b;
  float l[n], r[n], x[n], y[n], sl[n], sr[n];
  for (int i = 0; i < n; ++i) {
    sl[i] = .5f * sinf(i * .07f); sr[i] = .2f * sinf(i * .11f);
    l[i] = x[i] = sl[i] + sinf(i * .13f); r[i] = y[i] = sr[i] + .5f * cosf(i * .17f);
  }
  a.process(l, r, sl, sr, n, 97, 33, 84);
  for (int i = 0; i < n;) {
    const int step = (i % 29) + 1, count = i + step < n ? step : n - i;
    b.process(x + i, y + i, sl + i, sr + i, count, 97, 33, 84);
    i += count;
  }
  TEST_ASSERT_EQUAL_MEMORY(l, x, sizeof(l));
  TEST_ASSERT_EQUAL_MEMORY(r, y, sizeof(r));
}
void test_silence_stays_finite() {
  Compressor c;
  dc(c, 1, 1000);
  const float y = dc(c, 0, 4 * kSynthRate);
  TEST_ASSERT_TRUE(isfinite(y));
  TEST_ASSERT_EQUAL_FLOAT(0, y);
  TEST_ASSERT_TRUE(isfinite(c.gainDb()));
}
// Establish the setting during quiet input so the enable fade does not mask attack time.
static float burstAfterQuiet(uint8_t attack) {
  Compressor c;
  float l[kN], r[kN];
  for (int b = 0; b < 100; ++b) {
    for (int i = 0; i < kN; ++i) l[i] = r[i] = .001f;
    c.process(l, r, nullptr, nullptr, kN, 127, 50, 0, attack);
  }
  for (int i = 0; i < kN; ++i) l[i] = r[i] = 1;
  c.process(l, r, nullptr, nullptr, kN, 127, 50, 0, attack);
  float sum = 0;
  for (float x : l) sum += x;
  return sum / kN;
}
void test_attack_preserves_transient() {
  const float fast = burstAfterQuiet(0), normal = burstAfterQuiet(83), slow = burstAfterQuiet(127);
  TEST_ASSERT_TRUE(fast < normal && normal < slow);
  TEST_ASSERT_TRUE(normal > fast * 1.5f);
}
void test_release_recovers_in_requested_direction() {
  Compressor fast, slow;
  dc(fast, 1, kSynthRate, 127, 0);
  dc(slow, 1, kSynthRate, 127, 127);
  const float a = dc(fast, .01f, kSynthRate / 10, 127, 0);
  const float b = dc(slow, .01f, kSynthRate / 10, 127, 127);
  TEST_ASSERT_TRUE(a > .009f && b < .004f && a > b);
}
void test_amount_change_has_no_gain_jump() {
  Compressor c;
  const float before = dc(c, 1, kSynthRate);
  float l[512], r[512];
  for (int i = 0; i < 512; ++i) l[i] = r[i] = 1;
  c.process(l, r, nullptr, nullptr, 512, 1, 50, 0);
  TEST_ASSERT_TRUE(l[0] > before && l[0] - before < .005f);
  for (int i = 1; i < 512; ++i) TEST_ASSERT_TRUE(fabsf(l[i] - l[i - 1]) < .005f);
  TEST_ASSERT_TRUE(l[511] > .99f);
}
void test_sidechain_mode_change_is_smooth() {
  Compressor c;
  const float before = dc(c, 1, kSynthRate);
  float l[1024], r[1024], key[1024] = {};
  for (int i = 0; i < 1024; ++i) l[i] = r[i] = 1;
  c.process(l, r, key, key, 1024, 127, 50, 127);
  TEST_ASSERT_TRUE(fabsf(l[0] - before) < .005f * (44100.f / kSynthRate));
  for (int i = 1; i < 1024; ++i) TEST_ASSERT_TRUE(fabsf(l[i] - l[i - 1]) < .008f * (44100.f / kSynthRate));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, l[1023]);
  const float switched = dc(c, 1, 1, 127, 50, 0, 0);
  TEST_ASSERT_TRUE(switched > .99f);
  TEST_ASSERT_TRUE(dc(c, 1, 1024, 127, 50, 0, 0) < .25f);
}
void test_sidechain_depth_change_is_smooth() {
  Compressor c;
  const float key = .5f, pad = .2f;
  const float before = dc(c, key + pad, kSynthRate, 127, 50, key, 127);
  float l[1024], r[1024], sc[1024];
  for (int i = 0; i < 1024; ++i) l[i] = r[i] = key + pad, sc[i] = key;
  c.process(l, r, sc, sc, 1024, 127, 50, 1);
  TEST_ASSERT_TRUE(l[0] >= before && l[0] - before < .001f);
  for (int i = 1; i < 1024; ++i) TEST_ASSERT_TRUE(fabsf(l[i] - l[i - 1]) < .002f);
  TEST_ASSERT_TRUE(l[1023] > .69f);
}
void test_clamped_values_match_127() {
  Compressor a, b;
  float l[1024], r[1024], x[1024], y[1024], key[1024];
  for (int i = 0; i < 1024; ++i) l[i] = r[i] = x[i] = y[i] = 1, key[i] = .5f;
  a.process(l, r, key, key, 1024, 255, 255, 255, 255);
  b.process(x, y, key, key, 1024, 127, 127, 127, 127);
  TEST_ASSERT_EQUAL_MEMORY(l, x, sizeof(l));
  TEST_ASSERT_EQUAL_MEMORY(r, y, sizeof(r));
}
void test_zero_length_does_not_change_state_or_buffers() {
  Compressor a, b;
  dc(a, 1, 137); dc(b, 1, 137);
  float l = 1, r = 1;
  a.process(&l, &r, nullptr, nullptr, 0, 0, 0, 0);
  TEST_ASSERT_EQUAL_FLOAT(1, l); TEST_ASSERT_EQUAL_FLOAT(1, r);
  const float expected = dc(b, 1, 1), actual = dc(a, 1, 1);
  TEST_ASSERT_EQUAL_FLOAT(expected, actual);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cold_bypass_bit_exact);
  RUN_TEST(test_below_threshold_has_no_makeup);
  RUN_TEST(test_minimum_amount_is_nearly_unity);
  RUN_TEST(test_amount_monotonic_and_never_boosts);
  RUN_TEST(test_static_ratio_above_knee);
  RUN_TEST(test_soft_knee_curve_and_boundaries);
  RUN_TEST(test_silent_external_key_ignores_loud_mix);
  RUN_TEST(test_external_key_independent_of_program_level);
  RUN_TEST(test_external_key_remains_dry);
  RUN_TEST(test_sidechain_depth_scales_reduction_db);
  RUN_TEST(test_disable_ramps_then_becomes_bit_exact);
  RUN_TEST(test_reenable_does_not_restore_stale_reduction);
  RUN_TEST(test_reset_matches_fresh_instance_sample_exact);
  RUN_TEST(test_stereo_linked_gain);
  RUN_TEST(test_gain_interpolates_between_control_samples);
  RUN_TEST(test_program_partition_invariant);
  RUN_TEST(test_silence_stays_finite);
  RUN_TEST(test_attack_preserves_transient);
  RUN_TEST(test_release_recovers_in_requested_direction);
  RUN_TEST(test_amount_change_has_no_gain_jump);
  RUN_TEST(test_sidechain_mode_change_is_smooth);
  RUN_TEST(test_sidechain_depth_change_is_smooth);
  RUN_TEST(test_clamped_values_match_127);
  RUN_TEST(test_zero_length_does_not_change_state_or_buffers);
  return UNITY_END();
}
