#include <cstring>
#include <unity.h>
#define private public
#include "synth.h"
#undef private
#include "mod_matrix.h"
using namespace mt;
static SynthModel m;
static Synth* s;
void setUp() {
  m = SynthModel(); s = nullptr;
  auto& i = m.instruments[0];
  i.type = InstrType::Chip; i.attack = i.decay = 0; i.sustain = 127;
  i.lfoWave = uint8_t(LfoWave::Square); i.lfoDepth = 0;
  m.tracks[0].out = TrackOut::Int; m.tracks[0].instr = 0; m.tracks[0].pan=64;
}
void tearDown() { delete s; }
static void route(int slot, ModSource src, LfoDest dest, int amount, ModSource via = ModSource::Off) {
  m.instruments[0].mod[slot] = {uint8_t(src),uint8_t(dest),int8_t(amount),0,uint8_t(via)};
}
static Voice& start(int velocity=127) { s = new Synth(m); s->noteOn(0,60,velocity); return s->voices_[s->trackVoice(0)]; }
void test_matrix_lfo_works_with_legacy_depth_zero_and_fans_out() {
  route(0,ModSource::Lfo1,LfoDest::Pan,-32);
  route(1,ModSource::Lfo1,LfoDest::Dly,32);
  auto& v=start();
  TEST_ASSERT_EQUAL(32,v.pan);
  TEST_ASSERT_FLOAT_WITHIN(.00001f,32.f/127,v.send);
  const float phase=s->lfoPhase(s->trackVoice(0),0);
  s->control(v,32);
  TEST_ASSERT_TRUE(s->lfoPhase(s->trackVoice(0),0)>phase);
  TEST_ASSERT_EQUAL_FLOAT(0,v.lfoOut[0]);  // legacy LFO->LFO source still disabled
}
void test_via_scales_one_route_without_scaling_other_destinations() {
  route(0,ModSource::Lfo1,LfoDest::Pan,-32,ModSource::Velocity);
  route(1,ModSource::Lfo1,LfoDest::Dly,32);
  auto& v=start(64);
  TEST_ASSERT_EQUAL(48,v.pan);
  TEST_ASSERT_FLOAT_WITHIN(.00001f,32.f/127,v.send);
}
void test_live_and_next_note_signed_depth_locks_and_bypass() {
  route(0,ModSource::Velocity,LfoDest::Pan,32);
  auto& v=start(); TEST_ASSERT_EQUAL(96,v.pan);
  s->rt_[0].noteStep=false; s->fx(0,uint8_t(Fx::M01),uint8_t(-32)); s->control(v,0);
  TEST_ASSERT_EQUAL(32,v.pan);
  s->fx(0,kSynthClearLocks,0); s->control(v,0); TEST_ASSERT_EQUAL(96,v.pan);
  s->rt_[0].noteStep=true; s->fx(0,uint8_t(Fx::M01),0); s->noteOn(0,64,127);
  TEST_ASSERT_EQUAL(64,s->voices_[s->trackVoice(0)].pan);
}
void test_filter_envelope_routes_without_enabling_legacy_filter_env() {
  auto& i=m.instruments[0]; i.fenv=0; i.fAtk=0; i.fDec=0;
  route(0,ModSource::FilterEnv,LfoDest::Pan,-16);
  auto& v=start(); TEST_ASSERT_EQUAL(48,v.pan);
}
void test_amp_envelope_and_follower_have_independent_route_depths() {
  route(0,ModSource::AmpEnv,LfoDest::Pan,32);
  route(1,ModSource::Follower,LfoDest::Pan,-16);
  auto& v=start();v.env.hold(.5f);s->folEnv_[0]=.5f;
  s->control(v,0);TEST_ASSERT_EQUAL(72,v.pan);
}
void test_matrix_rate_target_modulates_retriggered_lfo() {
  route(0,ModSource::Velocity,LfoDest::Rate1,16);
  route(1,ModSource::Lfo1,LfoDest::Pan,16);
  auto& v=start();
  s->control(v,32);
  TEST_ASSERT_FLOAT_WITHIN(1e-7f,2*lfoHz(m.instruments[0].lfoRate)*32/kSynthRate,v.lfoPhase[0]);
}
void test_shared_lfo_is_available_at_zero_legacy_depth() {
  m.instruments[0].lfoSync=kLfoFree;
  route(0,ModSource::Lfo1,LfoDest::Pan,-32);
  auto& v=start();TEST_ASSERT_EQUAL(32,v.pan);
  s->advanceInstLfos();s->control(v,32);
  TEST_ASSERT_FLOAT_WITHIN(1e-7f,s->instLfoPhase_[0][0],s->lfoPhase(s->trackVoice(0),0));
}
void test_random_is_held_per_note_and_reproducible_without_consuming_rng() {
  route(0,ModSource::Random,LfoDest::Pan,32);
  auto& v=start();const int pan=v.pan;const uint32_t rng=s->rng_;
  for(int i=0;i<8;++i)s->control(v,32);
  TEST_ASSERT_EQUAL(pan,v.pan);TEST_ASSERT_EQUAL(rng,s->rng_);
  delete s;s=nullptr;auto& again=start();TEST_ASSERT_EQUAL(pan,again.pan);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_matrix_lfo_works_with_legacy_depth_zero_and_fans_out);
  RUN_TEST(test_via_scales_one_route_without_scaling_other_destinations);
  RUN_TEST(test_live_and_next_note_signed_depth_locks_and_bypass);
  RUN_TEST(test_filter_envelope_routes_without_enabling_legacy_filter_env);
  RUN_TEST(test_amp_envelope_and_follower_have_independent_route_depths);
  RUN_TEST(test_matrix_rate_target_modulates_retriggered_lfo);
  RUN_TEST(test_shared_lfo_is_available_at_zero_legacy_depth);
  RUN_TEST(test_random_is_held_per_note_and_reproducible_without_consuming_rng);
  return UNITY_END();
}
