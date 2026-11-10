#include <cstring>
#include <unity.h>
#include "fx_info.h"
#include "preset_io.h"
#include "mod_matrix.h"
#include "automation.h"
#include "voice_cost.h"
#include <vector>
using namespace mt;

void setUp() {}
void tearDown() {}

static ModRoute route(ModSource s, LfoDest d, int a, ModPolarity p = ModPolarity::Native, ModSource via = ModSource::Off) {
  return {uint8_t(s), uint8_t(d), int8_t(a), uint8_t(p), uint8_t(via)};
}
void test_one_source_feeds_independent_targets_and_multiple_sources_sum() {
  Instrument m;
  m.mod[0] = route(ModSource::Lfo1, LfoDest::Cutoff, 32);
  m.mod[1] = route(ModSource::Lfo1, LfoDest::Pan, -64);
  m.mod[2] = route(ModSource::Velocity, LfoDest::Cutoff, 16);
  float src[kModSources] = {}, out[kModTargets] = {};
  src[int(ModSource::Lfo1)] = .5f; src[int(ModSource::Velocity)] = 1;
  modEvaluate(m, src, out);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, .5f, out[int(LfoDest::Cutoff)]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -.5f, out[int(LfoDest::Pan)]);
  TEST_ASSERT_EQUAL(1, modLfoMask(m));
}
void test_polarity_and_via_affect_only_selected_route() {
  Instrument m;
  m.mod[0] = route(ModSource::Lfo1, LfoDest::Pitch, 32, ModPolarity::Unipolar, ModSource::Velocity);
  m.mod[1] = route(ModSource::Velocity, LfoDest::Pan, 32, ModPolarity::Bipolar);
  float src[kModSources] = {}, out[kModTargets] = {};
  src[int(ModSource::Lfo1)] = -1;
  modEvaluate(m, src, out);
  TEST_ASSERT_EQUAL_FLOAT(0, out[int(LfoDest::Pitch)]);
  TEST_ASSERT_EQUAL_FLOAT(-.5f, out[int(LfoDest::Pan)]);
  src[int(ModSource::Lfo1)] = 1; src[int(ModSource::Velocity)] = .5f;
  modEvaluate(m, src, out);
  TEST_ASSERT_EQUAL_FLOAT(.25f, out[int(LfoDest::Pitch)]);
  TEST_ASSERT_EQUAL_FLOAT(0, out[int(LfoDest::Pan)]);
  m.mod[0].via = uint8_t(ModSource::Lfo4);
  TEST_ASSERT_EQUAL(9, modLfoMask(m));
}
void test_signed_lock_overrides_amount_including_zero_and_disabled_route() {
  Instrument m;
  m.mod[3] = route(ModSource::Velocity, LfoDest::Cutoff, 32);
  float src[kModSources] = {}, out[kModTargets] = {};
  uint8_t locks[kLocks] = {};
  src[int(ModSource::Velocity)] = 1;
  locks[kLockMatrix0 + 3] = uint8_t(-64);
  modEvaluate(m, src, out, 1u << (kLockMatrix0 + 3), locks);
  TEST_ASSERT_EQUAL_FLOAT(-1, out[int(LfoDest::Cutoff)]);
  locks[kLockMatrix0 + 3] = 0;
  modEvaluate(m, src, out, 1u << (kLockMatrix0 + 3), locks);
  TEST_ASSERT_EQUAL_FLOAT(0, out[int(LfoDest::Cutoff)]);
  m.mod[3].source = 0;
  locks[kLockMatrix0 + 3] = 63;
  modEvaluate(m, src, out, 1u << (kLockMatrix0 + 3), locks);
  TEST_ASSERT_EQUAL_FLOAT(0, out[int(LfoDest::Cutoff)]);
}
void test_targets_skip_reset_and_unsupported_engine_destinations() {
  for(int type=0;type<int(InstrType::Count);++type){
    uint8_t d=0;
    for(int n=0;n<kModTargets+4;++n){d=modDestStep(d,1,InstrType(type));TEST_ASSERT_TRUE(modDestValid(d));TEST_ASSERT_TRUE(lfoDestValid(d,InstrType(type)));}
    for(int n=0;n<kModTargets+4;++n)d=modDestStep(d,-1,InstrType(type));
    TEST_ASSERT_EQUAL(0,d);
  }
}
void test_invalid_routes_do_not_write_outside_target_array() {
  Instrument m;
  m.mod[0]={255,255,63,255,255};
  m.mod[1]={uint8_t(ModSource::Velocity),uint8_t(LfoDest::Rtrg1),63,0,0};
  float src[kModSources]={},out[kModTargets]={};src[int(ModSource::Velocity)]=1;
  modEvaluate(m,src,out);for(float x:out)TEST_ASSERT_EQUAL_FLOAT(0,x);
}
void test_type_change_disables_unsupported_routes_without_retuning() {
 Instrument m;m.type=InstrType::Synth;
 m.mod[0]=route(ModSource::Lfo1,LfoDest::Sub,42,ModPolarity::Bipolar,ModSource::Velocity);
 m.mod[1]=route(ModSource::Velocity,LfoDest::Cutoff,31);
 m.mod[2]=route(ModSource::Lfo2,LfoDest::Rtrg1,22);
 instrSetType(m,InstrType::Sample);
 TEST_ASSERT_EQUAL(0,m.mod[0].source);TEST_ASSERT_EQUAL(LfoDest::Pitch,m.mod[0].dest);
 TEST_ASSERT_EQUAL(42,m.mod[0].amount);TEST_ASSERT_EQUAL(ModPolarity::Bipolar,m.mod[0].polarity);TEST_ASSERT_EQUAL(ModSource::Velocity,m.mod[0].via);
 TEST_ASSERT_EQUAL(ModSource::Velocity,m.mod[1].source);TEST_ASSERT_EQUAL(LfoDest::Cutoff,m.mod[1].dest);
 TEST_ASSERT_EQUAL(0,m.mod[2].source);
}
int main(){UNITY_BEGIN();
 RUN_TEST(test_type_change_disables_unsupported_routes_without_retuning);
 RUN_TEST(test_one_source_feeds_independent_targets_and_multiple_sources_sum);
 RUN_TEST(test_polarity_and_via_affect_only_selected_route);
 RUN_TEST(test_signed_lock_overrides_amount_including_zero_and_disabled_route);
 RUN_TEST(test_targets_skip_reset_and_unsupported_engine_destinations);
 RUN_TEST(test_invalid_routes_do_not_write_outside_target_array);
 return UNITY_END();}
