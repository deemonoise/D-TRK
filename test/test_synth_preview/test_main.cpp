#include <unity.h>
#include <math.h>
#include <string.h>
#include "synth_preview.h"
using namespace mt;
void setUp() {}
void tearDown() {}
static SynPreviewFrame frame, again;
static int16_t table[kWtTableSamples];

void test_hold_freezes_age_and_edits_still_render() {
  SynPreviewClock c;
  TEST_ASSERT_TRUE(c.update(100, false, false));
  TEST_ASSERT_FALSE(c.update(500, false, false));
  TEST_ASSERT_EQUAL_UINT32(0, c.ageSamples());
  TEST_ASSERT_TRUE(c.update(700, false, true));
  TEST_ASSERT_EQUAL_UINT32(0, c.ageSamples());
}
void test_live_is_capped_and_paused_time_is_excluded() {
  SynPreviewClock c;
  c.update(0, true, false);
  TEST_ASSERT_FALSE(c.update(99, true, false));
  TEST_ASSERT_TRUE(c.update(100, true, false));
  TEST_ASSERT_EQUAL_UINT32(4410, c.ageSamples());
  TEST_ASSERT_FALSE(c.update(200, false, false));  // Freeze the last visible frame, no restart jump.
  TEST_ASSERT_EQUAL_UINT32(4410, c.ageSamples());
  const uint32_t frozen = c.ageSamples();
  c.update(10000, false, false);
  c.update(10100, true, false);
  TEST_ASSERT_EQUAL_UINT32(frozen, c.ageSamples());
  c.update(10200, true, false);
  TEST_ASSERT_EQUAL_UINT32(frozen + 4410, c.ageSamples());
}
void test_edits_are_rate_limited_without_losing_the_latest_change() {
  SynPreviewClock c;
  TEST_ASSERT_TRUE(c.update(0, false, false));
  TEST_ASSERT_FALSE(c.update(10, false, true));
  TEST_ASSERT_FALSE(c.update(99, false, true));
  TEST_ASSERT_TRUE(c.update(100, false, true));
  TEST_ASSERT_EQUAL_UINT32(0, c.ageSamples());
}
void test_source_mix_and_mod_share_the_real_voice() {
  SynPreviewModel model;
  SynParams p; p.mode[0] = static_cast<uint8_t>(SynOsc::Saw); p.mode[1] = static_cast<uint8_t>(SynOsc::Tri);
  p.hz[0] = 261.6256f; p.hz[1] = 389.21f; p.mix = .37f;
  model.render(p, {}, false, frame);
  double energy = 0;
  for (int i = 0; i < kSynPreviewPoints; ++i) {
    const float want = frame.osc[0][i] * .63f + frame.osc[1][i] * .37f;
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mix[i]);
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mod[i]);
    energy += fabsf(frame.mix[i]);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(10.f, static_cast<float>(energy));
  p.fold = .7f; p.ring = .4f; p.sub = .3f; p.noise = .1f;
  model.render(p, {}, false, frame);
  SynVoice real; real.trigger();
  float block[32]; int point = 0;
  for (int k = 0; k < kSynPreviewPoints * kSynPreviewStride; k += 32) {
    memset(block, 0, sizeof block); real.control(p, 32); real.render(block, 32, 1);
    for (int j = 3; j < 32; j += 4) {
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, block[j], frame.mod[point]);
      float lo = block[j], hi = block[j];
      for (int a = j - 3; a < j; ++a) { lo = fminf(lo, block[a]); hi = fmaxf(hi, block[a]); }
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, lo, frame.modLo[point]);
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, hi, frame.modHi[point]);
      ++point;
    }
  }
}
void test_bin_ranges_retain_peaks_between_representative_points() {
  SynPreviewModel model; SynParams p;
  p.hz[0] = 4000; p.mode[0] = static_cast<uint8_t>(SynOsc::Square); p.shape[0] = 1;
  model.render(p, {}, false, frame);
  bool hiddenPeak = false;
  for (int i = 0; i < kSynPreviewPoints; ++i) {
    TEST_ASSERT_TRUE(frame.mixLo[i] <= frame.mix[i] && frame.mixHi[i] >= frame.mix[i]);
    if (frame.mixLo[i] < frame.mix[i] - .2f || frame.mixHi[i] > frame.mix[i] + .2f) hiddenPeak = true;
  }
  TEST_ASSERT_TRUE(hiddenPeak);
}
void test_hold_is_deterministic_and_live_changes_phase() {
  SynPreviewModel model;
  SynParams p; p.hz[0] = 261.6256f; p.hz[1] = 330;
  model.render(p, {}, false, frame);
  model.render(p, {}, false, again);
  TEST_ASSERT_EQUAL_MEMORY(frame.mix, again.mix, sizeof frame.mix);
  model.render(p, {}, true, again);
  TEST_ASSERT_TRUE(memcmp(frame.mix, again.mix, sizeof frame.mix) != 0);
}
void test_live_mix_reactivation_keeps_sources_coherent() {
  SynPreviewModel model; SynParams p;
  p.hz[0] = 261.6256f; p.hz[1] = 389.21f; p.mix = 0;
  model.render(p, {}, false, frame);
  p.mix = .37f; model.render(p, {}, true, frame);
  for (int i = 0; i < kSynPreviewPoints; ++i) {
    const float want = frame.osc[0][i] * .63f + frame.osc[1][i] * .37f;
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mix[i]);
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mod[i]);
  }
}
void test_missing_wt_is_silent_and_flagged() {
  SynPreviewModel model; SynParams p;
  p.mode[0] = static_cast<uint8_t>(SynOsc::Wt);
  model.render(p, {}, false, frame);
  TEST_ASSERT_TRUE(frame.missing[0]);
  TEST_ASSERT_FALSE(frame.missing[1]);
  for (float x : frame.osc[0]) TEST_ASSERT_EQUAL_FLOAT(0, x);
}
void test_wt_snapshot_adapter_preserves_scale_and_frame_layout() {
  int8_t src[kWtFrameLen]; for (int i = 0; i < kWtFrameLen; ++i) src[i] = i < 128 ? 100 : -100;
  synPreviewTable(src, table);
  TEST_ASSERT_EQUAL_INT16(25600, table[12]);
  TEST_ASSERT_EQUAL_INT16(-25600, table[200]);
  TEST_ASSERT_EQUAL_MEMORY(table, table + (kWtFrames - 1) * kWtFramePts, kWtFramePts * sizeof(int16_t));
  SynParams p; p.mode[0] = static_cast<uint8_t>(SynOsc::Wt); p.wt[0] = table;
  SynPreviewModel model; model.render(p, {}, false, frame);
  TEST_ASSERT_FALSE(frame.missing[0]);
  float peak = 0; for (float x : frame.mix) peak = fmaxf(peak, fabsf(x));
  TEST_ASSERT_GREATER_THAN_FLOAT(.8f, peak);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hold_freezes_age_and_edits_still_render);
  RUN_TEST(test_live_is_capped_and_paused_time_is_excluded);
  RUN_TEST(test_edits_are_rate_limited_without_losing_the_latest_change);
  RUN_TEST(test_source_mix_and_mod_share_the_real_voice);
  RUN_TEST(test_hold_is_deterministic_and_live_changes_phase);
  RUN_TEST(test_live_mix_reactivation_keeps_sources_coherent);
  RUN_TEST(test_bin_ranges_retain_peaks_between_representative_points);
  RUN_TEST(test_missing_wt_is_silent_and_flagged);
  RUN_TEST(test_wt_snapshot_adapter_preserves_scale_and_frame_layout);
  return UNITY_END();
}
