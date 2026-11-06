#include <cstring>
#include <initializer_list>
#include <unity.h>
#define private public
#include "synth.h"
#include "synth_syn_calibration.h"
#undef private
using namespace mt;

static SynthModel model;
static Synth* synth;
void setUp() { model = SynthModel(); synth = nullptr; }
void tearDown() { delete synth; }
static void exact(float expected, float actual) {
  uint32_t a, b;
  std::memcpy(&a, &expected, sizeof a);
  std::memcpy(&b, &actual, sizeof b);
  TEST_ASSERT_EQUAL_HEX32(a, b);
}
static Voice& start() {
  auto& m = model.instruments[0];
  instrSetType(m, InstrType::Synth);
  m.attack = m.decay = m.release = 0; m.sustain = 127;
  m.fltMode = uint8_t(FltMode::Lp); m.cutoff = 64; m.reso = 64;
  m.fenv = 32; m.fAtk = 64; m.fDec = 64; m.keytrack = 0; m.velCut = 0;
  m.lfoDepth = 0;
  for (auto& l : m.lfo) l.depth = 0;
  model.tracks[0].out = TrackOut::Int; model.tracks[0].instr = 0;
  synth = new Synth(model); synth->noteOn(0, 60, 100);
  return synth->voices_[synth->trackVoice(0)];
}
void test_cached_envelopes_keep_reference_bits_on_edits_and_hits() {
  SynVoice v;
  const uint8_t controls[] = {0,1,15,16,31,32,48,50,55,60,64,96,127,255};
  const uint32_t times[] = {0,1,43,441,10000,44100,1000000,UINT32_MAX};
  for (uint8_t a : controls) for (uint8_t d : controls) for (uint32_t t : times) {
    const float expected = synModEnv(t, a, d);
    exact(expected, v.cachedModEnv(t, a, d, false));
    exact(expected, v.cachedModEnv(t, a, d, false));
    exact(synModEnv(t, d, a), v.cachedModEnv(t, d, a, true));
    exact(expected, v.cachedModEnv(t, a, d, false));
  }
}
void test_gain_cache_keeps_fractional_resonance_and_repeated_edits() {
  SynVoice v;
  for (int i = -40; i <= 800; ++i) {
    const float raw = i * .25f;
    exact(synLpGain(raw), v.cachedLpGain(raw));
    exact(synLpGain(raw), v.cachedLpGain(raw));
    exact(synLpGain(112), v.cachedLpGain(112));
  }
}
void test_retrigger_does_not_reset_envelope_phase_or_hold_semantics() {
  SynVoice v;
  exact(synModEnv(30000, 64, 64), v.cachedModEnv(30000, 64, 64, false));
  v.trigger();
  exact(0, v.cachedModEnv(0, 64, 64, false));
  exact(1, v.cachedModEnv(UINT32_MAX, 0, 0, false));
  exact(synModEnv(441, 1, 1), v.cachedModEnv(441, 1, 1, false));
}
void test_live_filter_and_shape_edits_invalidate_independent_caches() {
  Voice& v = start(); auto& m = model.instruments[0];
  float la[int(LfoDest::Count)] = {};
  for (uint8_t a : {uint8_t(0), uint8_t(64), uint8_t(16)}) {
    m.fAtk = a; m.fDec = 60;
    m.synEAtk = 32; m.synEDec = a;
    for (uint32_t t : {0u,441u,30000u}) {
      v.fenvT = t; v.fenvDone = false;
      synth->controlFilter(v, m, 60, la);
      const float oct = synLpOctaves(m.cutoff) + synFenvOctaves(m.fenv) * synModEnv(t,a,60);
      TEST_ASSERT_FLOAT_WITHIN(.00001f, oct, v.octKey);
      exact(synModEnv(t,32,a), v.sv().cachedModEnv(t,32,a,true));
    }
  }
}
void test_engine_switch_and_voice_copy_preserve_cache_contract() {
  Voice v; v.useEngine(EngineKind::Syn);
  exact(synModEnv(30000,64,60), v.sv().cachedModEnv(30000,64,60,false));
  exact(synLpGain(112), v.sv().cachedLpGain(112));
  Voice copy = v;
  exact(synModEnv(441,64,60), copy.sv().cachedModEnv(441,64,60,false));
  for (auto kind : {EngineKind::Drum, EngineKind::Fm, EngineKind::String, EngineKind::Lofi}) {
    v.useEngine(kind); v.useEngine(EngineKind::Syn);
    exact(synModEnv(441,16,32), v.sv().cachedModEnv(441,16,32,false));
    exact(synLpGain(64), v.sv().cachedLpGain(64));
  }
}
void test_zero_lfos_clear_outputs_keep_phase_and_consume_wraps_only_on_tick() {
  Voice& v = start();
  for (int i = 0; i < kLfos; ++i) { v.lfoOut[i] = .75f; v.lfoPhase[i] = .3f; }
  v.lfoWrap = 0xF;
  synth->control(v, 0);
  TEST_ASSERT_EQUAL_HEX8(0xF, v.lfoWrap);
  for (int i = 0; i < kLfos; ++i) { exact(0, v.lfoOut[i]); exact(.3f, v.lfoPhase[i]); }
  synth->control(v, Synth::kControl);
  TEST_ASSERT_EQUAL_HEX8(0, v.lfoWrap);
  for (float phase : v.lfoPhase) exact(.3f, phase);
}
void test_depth_lock_reenables_zero_lfo_and_unlock_returns_to_bypass() {
  Voice& v = start();
  v.lfoLockMask = 1u << kLfoLkDepth;
  v.lfoLock[0].depth = 30;
  synth->control(v, Synth::kControl);
  TEST_ASSERT_TRUE(v.lfoPhase[0] > 0);
  const float phase = v.lfoPhase[0];
  v.lfoLockMask = 0;
  synth->control(v, Synth::kControl);
  exact(phase, v.lfoPhase[0]); exact(0, v.lfoOut[0]);
}
void test_zero_lfos_do_not_bypass_track_follower() {
  Voice& v = start();
  synth->folVoice_ = 1;
  synth->folLa_[0] = int(LfoDest::Cutoff);
  synth->folMod_[0] = -.25f;
  v.fenvDone = true;
  synth->control(v, 0);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, synLpOctaves(48), v.octKey);
}
void test_disabled_free_lfo_keeps_shared_clock_for_later_enable() {
  Voice& v = start(); auto& m = model.instruments[0];
  m.lfoSync = kLfoFree;
  synth->cacheInstrument(0);
  synth->advanceInstLfos();
  TEST_ASSERT_TRUE(synth->instLfoPhase_[0][0] > 0);
  const float phase = synth->instLfoPhase_[0][0];
  synth->control(v, Synth::kControl);
  exact(phase, synth->instLfoPhase_[0][0]);
  m.lfoDepth = 30; synth->cacheInstrument(0);
  synth->control(v, 0);
  exact(phase, synth->lfoPhase(synth->trackVoice(0),0));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cached_envelopes_keep_reference_bits_on_edits_and_hits);
  RUN_TEST(test_gain_cache_keeps_fractional_resonance_and_repeated_edits);
  RUN_TEST(test_retrigger_does_not_reset_envelope_phase_or_hold_semantics);
  RUN_TEST(test_live_filter_and_shape_edits_invalidate_independent_caches);
  RUN_TEST(test_engine_switch_and_voice_copy_preserve_cache_contract);
  RUN_TEST(test_zero_lfos_clear_outputs_keep_phase_and_consume_wraps_only_on_tick);
  RUN_TEST(test_depth_lock_reenables_zero_lfo_and_unlock_returns_to_bypass);
  RUN_TEST(test_zero_lfos_do_not_bypass_track_follower);
  RUN_TEST(test_disabled_free_lfo_keeps_shared_clock_for_later_enable);
  return UNITY_END();
}
