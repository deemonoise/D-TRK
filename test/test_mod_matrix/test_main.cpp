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
struct Bytes : ByteSink, ByteSource {
  std::vector<uint8_t> b; size_t pos = 0;
  bool write(const void* p, size_t n) override { const auto* s = static_cast<const uint8_t*>(p); b.insert(b.end(), s, s+n); return true; }
  bool read(void* p, size_t n) override { if (pos+n>b.size()) return false; memcpy(p,b.data()+pos,n);pos+=n;return true; }
  bool skip(size_t n) override { if(pos+n>b.size())return false;pos+=n;return true; }
};
void test_matrix_survives_preset_and_project_roundtrip() {
  static Project p, q;
  p.reset();
  auto& m = p.instruments[31];
  m.mod[7] = route(ModSource::FilterEnv, LfoDest::Fold, -37, ModPolarity::Bipolar, ModSource::Random);
  Bytes preset; TEST_ASSERT_TRUE(savePreset(m,preset));
  Instrument loaded;
  TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadPreset(preset,loaded)));
  TEST_ASSERT_EQUAL_MEMORY(&m.mod[7],&loaded.mod[7],sizeof(ModRoute));
  Bytes project; TEST_ASSERT_TRUE(saveProject(p,project));
  TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(project,q)));
  TEST_ASSERT_EQUAL_MEMORY(m.mod,q.instruments[31].mod,sizeof m.mod);
}
void test_signed_automation_ramp_and_motion_keep_negative_values() {
  Step steps[3];
  TEST_ASSERT_TRUE(automationRamp(steps,3,Fx::M01,0,2,uint8_t(-32),32));
  TEST_ASSERT_EQUAL(0,automationGet(steps[1],Fx::M01));
  TEST_ASSERT_EQUAL_INT(-32,fxSigned(uint8_t(automationGet(steps[0],Fx::M01))));
  Instrument m; TrackCfg t;
  m.mod[0].amount=-17;
  TEST_ASSERT_EQUAL(239,automationValue(t,m,Fx::M01));
}
void test_bad_records_disable_invalid_routes_and_ignore_partial_route() {
  Instrument m;
  uint8_t b[kModRecSize] = {};
  b[0] = 255; b[1] = 255; b[2] = 127; b[3] = 255; b[4] = 255;
  b[5] = uint8_t(ModSource::Velocity); b[6] = uint8_t(LfoDest::Pan); b[7] = uint8_t(-128);
  unpackMod(b,9,m); // the second route is incomplete
  TEST_ASSERT_EQUAL(0,m.mod[0].source);
  TEST_ASSERT_EQUAL(0,m.mod[0].dest);
  TEST_ASSERT_EQUAL(63,m.mod[0].amount);
  TEST_ASSERT_EQUAL(0,m.mod[0].polarity);
  TEST_ASSERT_EQUAL(0,m.mod[0].via);
  TEST_ASSERT_EQUAL(0,m.mod[1].source);
  unpackMod(b,sizeof b,m);
  TEST_ASSERT_EQUAL(-64,m.mod[1].amount);
  b[1]=uint8_t(LfoDest::Rtrg1);b[0]=uint8_t(ModSource::Lfo1);
  unpackMod(b,sizeof b,m); TEST_ASSERT_EQUAL(0,m.mod[0].source);
}
void test_legacy_v10_presets_and_projects_load_with_empty_matrix() {
  Instrument m;
  m.mod[0] = route(ModSource::Velocity,LfoDest::Pan,32);
  Bytes p; TEST_ASSERT_TRUE(savePreset(m,p));
  p.b.resize(kPresetSizeV10);p.b[4]=10;
  uint32_t crc=crc32(p.b.data(),p.b.size()-4);
  for(int i=0;i<4;++i)p.b[p.b.size()-4+i]=crc>>(8*i);
  TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadPreset(p,m)));
  for(const auto& r:m.mod) TEST_ASSERT_EQUAL(0,r.source);
  static Project project,loaded;project.reset();
  Bytes file;TEST_ASSERT_TRUE(saveProject(project,file));
  TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(file,loaded)));
  for(const auto& inst:loaded.instruments) for(const auto& r:inst.mod) TEST_ASSERT_EQUAL(0,r.source);
}
void test_destinations_skip_discrete_reset_and_cost_reserves_locked_routes() {
  for(int i=0;i<int(InstrType::Count);++i) {
    uint8_t dest=0;
    for(int n=0;n<kModTargets+4;++n) {
      dest=modDestStep(dest,1,InstrType(i));
      TEST_ASSERT_TRUE(modDestValid(dest));
      TEST_ASSERT_TRUE(lfoDestValid(dest,InstrType(i)));
    }
    for(int n=0;n<kModTargets+4;++n) dest=modDestStep(dest,-1,InstrType(i));
    TEST_ASSERT_EQUAL(0,dest);
  }
  Instrument m;
  m.mod[0]=route(ModSource::Velocity,LfoDest::Fold,0);
  m.mod[1]=route(ModSource::AmpEnv,LfoDest::Reso,0);
  m.mod[2]=route(ModSource::Note,LfoDest::Pitch,0);
  TEST_ASSERT_EQUAL(kCostPotentialFold|kCostPotentialSynLpLimit|kCostPotentialSamplePitch,modCostPotential(m));
}

void test_matrix_depth_has_signed_step_locks() {
  Fx found = Fx::None;
  for (int i = 1; i < int(Fx::Count); ++i)
    if (std::strcmp(fxName(Fx(i)), "M01") == 0) found = Fx(i);
  TEST_ASSERT_NOT_EQUAL(int(Fx::None), int(found));
  TEST_ASSERT_TRUE(fxSynthOnly(found));
  TEST_ASSERT_EQUAL_INT(-64, fxSigned(fxStep(found, 0, -100)));
  TEST_ASSERT_EQUAL_INT(63, fxSigned(fxStep(found, 0, 100)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_matrix_depth_has_signed_step_locks);
  RUN_TEST(test_one_source_feeds_independent_targets_and_multiple_sources_sum);
  RUN_TEST(test_polarity_and_via_affect_only_selected_route);
  RUN_TEST(test_signed_lock_overrides_amount_including_zero_and_disabled_route);
  RUN_TEST(test_matrix_survives_preset_and_project_roundtrip);
  RUN_TEST(test_signed_automation_ramp_and_motion_keep_negative_values);
  RUN_TEST(test_bad_records_disable_invalid_routes_and_ignore_partial_route);
  RUN_TEST(test_legacy_v10_presets_and_projects_load_with_empty_matrix);
  RUN_TEST(test_destinations_skip_discrete_reset_and_cost_reserves_locked_routes);
  return UNITY_END();
}
