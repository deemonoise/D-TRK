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
  TEST_ASSERT_EQUAL_UINT32(3200, c.ageSamples());
  TEST_ASSERT_FALSE(c.update(200, false, false));  // Freeze the last visible frame, no restart jump.
  TEST_ASSERT_EQUAL_UINT32(3200, c.ageSamples());
  const uint32_t frozen = c.ageSamples();
  c.update(10000, false, false);
  c.update(10100, true, false);
  TEST_ASSERT_EQUAL_UINT32(frozen, c.ageSamples());
  c.update(10200, true, false);
  TEST_ASSERT_EQUAL_UINT32(frozen + 3200, c.ageSamples());
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
  model.render(p, false, frame);
  double energy = 0;
  for (int i = 0; i < kSynPreviewPoints; ++i) {
    const float want = frame.osc[0][i] * .63f + frame.osc[1][i] * .37f;
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mix[i]);
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mod[i]);
    energy += fabsf(frame.mix[i]);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(10.f, static_cast<float>(energy));
  p.sub = .3f; p.noise = .1f;
  model.render(p, false, frame);
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
  p.hz[0] = 4000; p.mode[0] = static_cast<uint8_t>(SynOsc::Square); p.shape[0] = .9f;  //95% duty;100% is silent
  model.render(p, false, frame);
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
  model.render(p, false, frame);
  model.render(p, false, again);
  TEST_ASSERT_EQUAL_MEMORY(frame.mix, again.mix, sizeof frame.mix);
  model.render(p, true, again);
  TEST_ASSERT_TRUE(memcmp(frame.mix, again.mix, sizeof frame.mix) != 0);
}
void test_live_mix_reactivation_keeps_sources_coherent() {
  SynPreviewModel model; SynParams p;
  p.hz[0] = 261.6256f; p.hz[1] = 389.21f; p.mix = 0;
  model.render(p, false, frame);
  p.mix = .37f; model.render(p, true, frame);
  for (int i = 0; i < kSynPreviewPoints; ++i) {
    const float want = frame.osc[0][i] * .63f + frame.osc[1][i] * .37f;
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mix[i]);
    TEST_ASSERT_FLOAT_WITHIN(2e-6f, want, frame.mod[i]);
  }
}
void test_missing_wt_is_silent_and_flagged() {
  SynPreviewModel model; SynParams p;
  p.mode[0] = static_cast<uint8_t>(SynOsc::Wt);
  model.render(p, false, frame);
  TEST_ASSERT_TRUE(frame.missing[0]);
  TEST_ASSERT_FALSE(frame.missing[1]);
  for (float x : frame.osc[0]) TEST_ASSERT_EQUAL_FLOAT(0, x);
}
void test_actual_wt_table_frames_are_used_without_snapshot_conversion() {
  for (int f=0; f<kWtFrames; ++f)
    for (int n=0; n<kWtFramePts; ++n) table[f*kWtFramePts+n] = f < 32 ? 15000 : -15000;
  SynParams p; p.mode[0]=uint8_t(SynOsc::Wt); p.wt[0]=table; p.hz[0]=261.6256f;
  SynPreviewModel model; model.render(p,false,frame);
  TEST_ASSERT_FALSE(frame.missing[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f,.5f,frame.mix[32]);
  p.shape[0]=1; model.render(p,false,frame);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f,-.5f,frame.mix[32]);
}
void test_preview_params_match_supported_senv_shape_and_pitch() {
  Instrument m; m.type=InstrType::Synth;
  m.synOsc[0]=uint8_t(SynOsc::Square); m.synOsc[1]=uint8_t(SynOsc::Tri);
  m.transpose=12; m.fine=25; m.synSemi=-12;
  m.macro[kMacDet]=64; m.macro[kMacShp1]=0; m.macro[kMacShp2]=64;
  m.macro[kMacSenv]=96; m.synEAtk=64; m.synEDec=0;
  SynParams p; synPreviewParams(m,0,p);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f,0,p.shape[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f,64.f/127,p.shape[1]);
  const uint32_t halfAttack=envTimeMs(m.synEAtk)*(kSynthRate/1000)/2;
  synPreviewParams(m,halfAttack,p);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f,.25f,p.shape[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f,64.f/127+.25f,p.shape[1]);
  TEST_ASSERT_FLOAT_WITHIN(.001f,noteHz(72.25f),p.hz[0]);
  TEST_ASSERT_FLOAT_WITHIN(.001f,noteHz(60.25f),p.hz[1]);
}
void test_clock_wrap_and_reset_keep_live_window_bounded() {
  SynPreviewClock c; c.update(0xfffffff0u,true,false);
  TEST_ASSERT_TRUE(c.update(84,true,false));
  TEST_ASSERT_EQUAL_UINT32(kSynthRate/10,c.ageSamples());
  c.reset();TEST_ASSERT_TRUE(c.update(10000,false,false));
  TEST_ASSERT_EQUAL_UINT32(0,c.ageSamples());
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
  RUN_TEST(test_actual_wt_table_frames_are_used_without_snapshot_conversion);
  RUN_TEST(test_preview_params_match_supported_senv_shape_and_pitch);
  RUN_TEST(test_clock_wrap_and_reset_keep_live_window_bounded);
  return UNITY_END();
}
