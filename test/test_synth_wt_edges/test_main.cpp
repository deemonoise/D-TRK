#include <unity.h>
#include <math.h>
#include <string.h>
#include "synth_syn.h"

using namespace mt;
void setUp() {}
void tearDown() {}
static int16_t tables[kWtTableSamples];
static void levels() {
  for (int f = 0; f < kWtFrames; ++f)
    for (int k = 0; k < kWtLevels; ++k)
      for (int i = 0; i < wtLevelLen(k); ++i)
        tables[f * kWtFramePts + wtLevelOff(k) + i] = (k & 1) ? -kWtPeak : kWtPeak;
}
void test_mip_transition_does_not_step() {
  levels(); WtOsc o; o.setLevel(100);
  for (int i = 0; i < 64; ++i) o.next(tables, uint32_t(1000000), 0);
  o.setLevel(140);
  float previous = 1;
  for (int i = 0; i < 64; ++i) {
    const float y = o.next(tables, uint32_t(1000000), 0);
    TEST_ASSERT_TRUE(fabsf(y - previous) <= 0.063f);
    previous = y;
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.f, previous);
}
void test_mip_threshold_does_not_chatter() {
  levels(); WtOsc o; o.setLevel(120);
  for (int i = 0; i < 64; ++i) o.next(tables, uint32_t(1000000), 0);
  for (int j = 0; j < 12; ++j) {
    o.setLevel(j & 1 ? 126 : 124);
    for (int i = 0; i < 32; ++i)
      TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, o.next(tables, uint32_t(1000000), 0));
  }
}
void test_mip_reversal_mid_transition_stays_continuous() {
  levels(); WtOsc o; o.setLevel(100);
  for (int i = 0; i < 64; ++i) o.next(tables, uint32_t(1000000), 0);
  o.setLevel(140); float previous = 1;
  for (int i = 0; i < 96; ++i) {
    if (i == 8 || i == 40 || i == 72) o.setLevel(100);
    const float y = o.next(tables, uint32_t(1000000), 0);
    TEST_ASSERT_TRUE(fabsf(y - previous) <= 0.063f); previous = y;
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, previous);
}
void test_wt_sync_corrects_both_sides_of_reset() {
  for (int f = 0; f < kWtFrames; ++f)
    for (int k = 0; k < kWtLevels; ++k)
      for (int i = 0; i < wtLevelLen(k); ++i)
        tables[f * kWtFramePts + wtLevelOff(k) + i] = static_cast<int16_t>((2.f * i / wtLevelLen(k) - 1.f) * kWtPeak);
  WtOsc o; o.ph = 0xc0000000u; o.prev = 0.5f;
  const float before = o.next(tables, 0.1f, 0.f, 0.5f);
  const float after = o.next(tables, 0.1f, 0.f, -1.f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.3f, before);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.7f, after);
}
static float dry[2048], folded[2048];
static void voice(SynParams p, float* out) {
  SynVoice v; v.trigger(); memset(out, 0, 2048 * sizeof(float));
  for (int i = 0; i < 2048; i += 32) { v.control(p, 32); v.render(out + i, 32, 1); }
}
void test_fold_follows_audible_oscillator() {
  SynParams p; p.hz[0] = 220; p.hz[1] = 6000; p.mix = 1;
  voice(p, dry); p.fold = 1; voice(p, folded);
  TEST_ASSERT_EQUAL_MEMORY(dry, folded, sizeof(dry));
  p.hz[0] = 6000; p.hz[1] = 220; p.fold = 0; voice(p, dry);
  p.fold = 1; voice(p, folded); float difference = 0;
  for (int i = 512; i < 2048; ++i) difference += fabsf(dry[i] - folded[i]);
  TEST_ASSERT_TRUE(difference > 100);
}
void test_fold_includes_ring_and_sync_sources() {
  SynParams p; p.hz[0] = 220; p.hz[1] = 6000; p.ring = 1;
  voice(p, dry); p.fold = 1; voice(p, folded);
  TEST_ASSERT_EQUAL_MEMORY(dry, folded, sizeof(dry));
  p.ring = 0; p.mix = 1; p.sync = true; p.hz[0] = 6000; p.hz[1] = 220; p.fold = 0;
  voice(p, dry); p.fold = 1; voice(p, folded);
  TEST_ASSERT_EQUAL_MEMORY(dry, folded, sizeof(dry));
}
// A new, initially inaudible ring/mix source must not step the existing folder's drive.
void test_fold_limit_changes_ramp_with_ring_and_mix() {
  for (int mode = 0; mode < 2; ++mode) {
    SynVoice a, b; SynParams p;
    p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Tri);
    p.hz[0] = 1000; p.hz[1] = mode ? 6000 : 1000; p.fold = 1;
    a.trigger(); b.trigger();
    for (int block = 0; block < 3; ++block) {
      float x[32] = {}, y[32] = {};
      a.control(p, 32); b.control(p, 32); a.render(x, 32, 1); b.render(y, 32, 1);
    }
    float x[32] = {}, y[32] = {};
    a.control(p, 32);
    if (mode) p.mix = 1.f / 127; else p.ring = 1.f / 127;
    b.control(p, 32); a.render(x, 32, 1); b.render(y, 32, 1);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, x[0], y[0]);
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fold_limit_changes_ramp_with_ring_and_mix);
  RUN_TEST(test_mip_transition_does_not_step);
  RUN_TEST(test_mip_threshold_does_not_chatter);
  RUN_TEST(test_mip_reversal_mid_transition_stays_continuous);
  RUN_TEST(test_wt_sync_corrects_both_sides_of_reset);
  RUN_TEST(test_fold_follows_audible_oscillator);
  RUN_TEST(test_fold_includes_ring_and_sync_sources);
  return UNITY_END();
}
