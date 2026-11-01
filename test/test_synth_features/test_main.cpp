#include <unity.h>
#include <math.h>
#include <string.h>
#include "synth_syn.h"

using namespace mt;
void setUp() {}
void tearDown() {}

static SynParams sine() {
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Sine);
  p.hz[0] = p.hz[1] = kSynthRate / 64.f;
  return p;
}

static void block(SynVoice& v, const SynParams& p, float* b) {
  memset(b, 0, 32 * sizeof(float));
  v.control(p, 32);
  v.render(b, 32, 1.f);
}

void test_sine_is_clean_and_aligned() {
  SynVoice v;
  const auto p = sine();
  v.trigger();
  float b[32];
  block(v, p, b);
  TEST_ASSERT_EQUAL_FLOAT(0.f, b[0]);
  for (int i = 1; i < 32; ++i)
    TEST_ASSERT_FLOAT_WITHIN(.00001f, sinf(6.28318530718f * i / 64.f), b[i]);
}

void test_fixed_phase_offsets_and_retrigger() {
  SynVoice v;
  auto p = sine();
  float a[32], b[32];
  v.trigger(0, 32, 64);
  block(v, p, b);
  for (int i = 1; i < 32; ++i)
    TEST_ASSERT_FLOAT_WITHIN(.00001f, cosf(6.28318530718f * i / 64.f), b[i]);
  v.trigger(0, 32, 64);
  block(v, p, a);
  TEST_ASSERT_EQUAL_MEMORY(b, a, sizeof(a));
  p.mix = 1;
  v.trigger(0, 32, 64);
  block(v, p, b);
  for (int i = 1; i < 32; ++i)
    TEST_ASSERT_FLOAT_WITHIN(.00001f, -sinf(6.28318530718f * i / 64.f), b[i]);
}

void test_free_keeps_phase_only_for_reused_syn_voice() {
  SynVoice reference, free, fresh;
  const auto p = sine();
  float a[32], b[32], c[32];
  reference.trigger();
  free.trigger();
  block(reference, p, a);
  block(free, p, b);
  free.trigger(static_cast<uint8_t>(SynPhaseMode::Free), 0, 0, 0, true);
  block(reference, p, a);
  block(free, p, b);
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
  fresh.trigger(static_cast<uint8_t>(SynPhaseMode::Free), 32, 0, 0, false);
  block(fresh, p, c);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, cosf(6.28318530718f / 64.f), c[1]);
}

void test_random_phases_are_seeded_and_independent() {
  SynVoice v, w;
  auto p = sine();
  float a[32], b[32], c[32];
  v.trigger(static_cast<uint8_t>(SynPhaseMode::Random), 0, 0, 1234);
  w.trigger(static_cast<uint8_t>(SynPhaseMode::Random), 0, 0, 1234);
  block(v, p, a);
  block(w, p, b);
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
  v.trigger(static_cast<uint8_t>(SynPhaseMode::Random), 0, 0, 5678);
  block(v, p, c);
  double difference = 0;
  for (int i = 0; i < 32; ++i) difference += fabsf(a[i] - c[i]);
  TEST_ASSERT_TRUE(difference > 1);
  v.trigger(static_cast<uint8_t>(SynPhaseMode::Random), 0, 0, 1234);
  p.mix = 1;
  block(v, p, c);
  difference = 0;
  for (int i = 0; i < 32; ++i) difference += fabsf(a[i] - c[i]);
  TEST_ASSERT_TRUE(difference > 1);
}

void test_sub_sine_and_triangle_have_expected_waveforms() {
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Wt);
  p.hz[0] = kSynthRate / 32.f;
  p.sub = 1;
  for (int mode = 1; mode <= 2; ++mode) {
    p.subWave = static_cast<uint8_t>(mode);
    SynVoice v;
    v.trigger();
    float b[32];
    block(v, p, b);
    for (int i = 1; i < 32; ++i) {
      const float want = mode == 1 ? sinf(6.28318530718f * i / 64.f) : 4.f * i / 64.f - 1.f;
      TEST_ASSERT_FLOAT_WITHIN(.00001f, want, b[i]);
    }
  }
  p.subWave = static_cast<uint8_t>(SynSubWave::Sine);
  p.subOct = 2;
  SynVoice v;
  v.trigger();
  float b[32];
  block(v, p, b);
  for (int i = 1; i < 32; ++i)
    TEST_ASSERT_FLOAT_WITHIN(.00001f, sinf(6.28318530718f * i / 128.f), b[i]);
}

void test_sine_sync_corrects_reset_value_and_slope() {
  BlOsc o;
  o.t = .75f;
  o.prev = -1.f;
  const float h = -sinf(6.28318530718f * .8f);
  const float slope = 6.28318530718f * .1f * (1.f - cosf(6.28318530718f * .8f));
  const float before = o.sine(.1f, 10.f, .5f).v;
  const float after = o.sine(.1f, 10.f, -1.f).v;
  TEST_ASSERT_FLOAT_WITHIN(.00001f, -1.f + h * .125f + slope / 48.f, before);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, sinf(6.28318530718f * .05f) - h * .125f + slope / 48.f, after);
}

static int16_t phaseTable[kWtTableSamples];
void test_phase_settings_apply_to_wavetable_and_free_reuse() {
  for (int f = 0; f < kWtFrames; ++f)
    for (int k = 0; k < kWtLevels; ++k)
      for (int i = 0; i < wtLevelLen(k); ++i)
        phaseTable[f * kWtFramePts + wtLevelOff(k) + i] =
            static_cast<int16_t>((2.f * i / wtLevelLen(k) - 1.f) * kWtPeak);
  auto p = sine();
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Wt);
  p.wt[0] = p.wt[1] = phaseTable;
  SynVoice v, w;
  v.trigger(0, 32, 64);
  w.trigger(0, 32, 64);
  float a[32], b[32];
  block(v, p, a);
  block(w, p, b);
  TEST_ASSERT_FLOAT_WITHIN(.00005f, -.46875f, a[1]);
  v.trigger(static_cast<uint8_t>(SynPhaseMode::Free), 0, 0, 0, true);
  block(v, p, a);
  block(w, p, b);
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
  p.mix = 1;
  v.trigger(0, 32, 64);
  block(v, p, a);
  TEST_ASSERT_FLOAT_WITHIN(.00005f, .03125f, a[1]);
}

void test_sine_sync_master_reports_wraps_and_bounds_extreme_pitches() {
  BlOsc master;
  int wraps = 0;
  for (int i = 0; i < 4410; ++i)
    wraps += master.sine(1000.f / kSynthRate, kSynthRate / 1000.f, -1.f).wrap >= 0.f;
  TEST_ASSERT_INT_WITHIN(1, 100, wraps);
  auto p = sine();
  p.hz[0] = 50000;
  p.hz[1] = 73000;
  p.mix = 1;
  p.sync = true;
  SynVoice v;
  v.trigger();
  float b[32];
  for (int k = 0; k < 200; ++k) {
    block(v, p, b);
    for (float y : b) TEST_ASSERT_TRUE(isfinite(y) && fabsf(y) < 2.f);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sine_is_clean_and_aligned);
  RUN_TEST(test_fixed_phase_offsets_and_retrigger);
  RUN_TEST(test_free_keeps_phase_only_for_reused_syn_voice);
  RUN_TEST(test_random_phases_are_seeded_and_independent);
  RUN_TEST(test_sub_sine_and_triangle_have_expected_waveforms);
  RUN_TEST(test_sine_sync_corrects_reset_value_and_slope);
  RUN_TEST(test_phase_settings_apply_to_wavetable_and_free_reuse);
  RUN_TEST(test_sine_sync_master_reports_wraps_and_bounds_extreme_pitches);
  return UNITY_END();
}
