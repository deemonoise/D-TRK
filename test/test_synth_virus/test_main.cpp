#include <cmath>
#include <utility>
#include <initializer_list>
#include <unity.h>
#define private public
#include "synth.h"
#include "synth_syn_calibration.h"
#undef private
using namespace mt;
static SynthModel model;
static Synth* synth;
void setUp() { model=SynthModel(); synth=nullptr; }
void tearDown() { delete synth; }
static Voice& start(InstrType type, uint8_t attack=0, uint8_t decay=0, uint8_t sustain=127, uint8_t release=0) {
 auto& m=model.instruments[0];instrSetType(m,type);
 m.attack=attack;m.decay=decay;m.sustain=sustain;m.release=release;m.mono=true;
 m.fltMode=uint8_t(FltMode::Lp);m.cutoff=64;m.reso=64;m.keytrack=0;m.fenv=0;m.velCut=0;
 model.tracks[0].out=TrackOut::Int;model.tracks[0].instr=0;
 synth=new Synth(model);synth->noteOn(0,60,100);
 for(auto& v:synth->voices_)if(v.on)return v;
 TEST_FAIL_MESSAGE("no voice");return synth->voices_[0];
}
static float transition(Env env, bool rising) {
 int first=-1;
 for(int n=0;n<kSynthRate*5;++n) {
  const float v=env.next();
  if(first<0 && (rising?v>=.1f:v<=.9f)) first=n;
  if(first>=0 && (rising?v>=.9f:v<=.1f))return (n-first)*1000.f/kSynthRate;
 }
 TEST_FAIL_MESSAGE("transition not reached");return -1;
}
void test_measured_classic_lp_cutoff_and_resonance() {
 auto& v=start(InstrType::Synth);
 TEST_ASSERT_FLOAT_WITHIN(20.f,791.f,v.octHz);
 TEST_ASSERT_FLOAT_WITHIN(.3f,5.06f,v.fltQ);
 float la[static_cast<int>(LfoDest::Count)]={};auto& m=model.instruments[0];m.reso=0;
 synth->controlFilter(v,m,60,la);TEST_ASSERT_FLOAT_WITHIN(.03f,.714f,v.fltQ);
 for(auto pair:{std::pair<int,float>{56,521.39f},{72,1234.79f},{88,2946.14f},{104,6976.34f}}) {
  m.cutoff=pair.first;synth->controlFilter(v,m,60,la);
  TEST_ASSERT_FLOAT_WITHIN(pair.second*.01f,pair.second,v.octHz);
 }
}
void test_syn_lp_resonance_has_measured_level_compensation() {
 auto& v=start(InstrType::Synth);const float resonant=v.amp;
 model.instruments[0].reso=0;synth->cacheInstrument(0);synth->control(v,0);
 TEST_ASSERT_FLOAT_WITHIN(.03f,.622f,resonant/v.amp);
}
void test_syn_attack_matches_recorded_time() {
 auto& v=start(InstrType::Synth,64,0,127,0);
 TEST_ASSERT_FLOAT_WITHIN(4.f,69.92f,transition(v.env,true));
}
void test_syn_decay_matches_recorded_time() {
 auto& v=start(InstrType::Synth,0,64,0,0);v.env.next();
 TEST_ASSERT_FLOAT_WITHIN(25.f,775.5f,transition(v.env,false));
}
void test_syn_release_matches_recorded_time() {
 auto& v=start(InstrType::Synth,0,0,127,64);v.env.next();v.env.next();
 synth->noteOff(0,60);TEST_ASSERT_FLOAT_WITHIN(25.f,767.4f,transition(v.env,false));
}
void test_other_engines_retain_linear_envelope_and_filter() {
 auto& v=start(InstrType::Chip,0,64,0,64);v.env.next();
 TEST_ASSERT_FLOAT_WITHIN(1.f,.8f*envTimeMs(64),transition(v.env,false));
 TEST_ASSERT_FLOAT_WITHIN(.01f,cutoffHz(64),v.octHz);
 TEST_ASSERT_FLOAT_WITHIN(.001f,resoQ(64),v.fltQ);
}
void test_named_filter_models_retain_their_mapping() {
 auto& v=start(InstrType::Synth);auto& m=model.instruments[0];float la[static_cast<int>(LfoDest::Count)]={};
 for(auto mode:{FltMode::Ladder,FltMode::Ms20,FltMode::Comb,FltMode::Formant,FltMode::Sid,FltMode::Bp,FltMode::Hp}) {
  m.fltMode=uint8_t(mode);synth->controlFilter(v,m,60,la);
  TEST_ASSERT_FLOAT_WITHIN(.01f,cutoffHz(64),v.octHz);
  TEST_ASSERT_FLOAT_WITHIN(.001f,resoQ(64),v.fltQ);
 }
 m.fltMode=uint8_t(FltMode::Lp);synth->controlFilter(v,m,60,la);
 TEST_ASSERT_FLOAT_WITHIN(.3f,5.06f,v.fltQ);
}
void test_reference_envelope_fade_is_still_short_and_linear() {
 auto& v=start(InstrType::Synth,0,0,127,96);v.env.next();v.env.next();
 Env env=v.env;env.fade(3);int n=0;
 while(!env.idle() && n<kSynthRate){env.next();++n;}
 TEST_ASSERT_INT_WITHIN(1,3*kSynthRate/1000,n);
 TEST_ASSERT_EQUAL(28,sizeof(Env));
}
void test_syn_exponential_tail_frees_voice_and_retrigger_is_continuous() {
 auto& v=start(InstrType::Synth,64,0,127,64);
 for(int i=0;i<kSynthRate/4;++i)v.env.next();
 v.env.gate(false);for(int i=0;i<kSynthRate/4;++i)v.env.next();
 const float before=v.env.level();v.env.gate(true);
 TEST_ASSERT_FLOAT_WITHIN(.001f,before,v.env.next());
 TEST_ASSERT_TRUE(v.env.level()>before);
 v.env.gate(false);int n=0;
 while(!v.env.idle() && n<kSynthRate*5){v.env.next();++n;}
 TEST_ASSERT_TRUE(v.env.idle());TEST_ASSERT_EQUAL_FLOAT(0,v.env.level());
}
void test_syn_zero_times_and_sustain_target() {
 auto& v=start(InstrType::Synth,0,0,64,0);
 v.env.next();v.env.next();
 TEST_ASSERT_FLOAT_WITHIN(.00001f,64.f/127,v.env.level());
 v.env.gate(false);TEST_ASSERT_TRUE(v.env.idle());
}
void test_reference_maps_are_bounded_and_monotonic() {
 float fc=0,q=0,gain=1;uint16_t a=0,d=0;
 for(int r=0;r<=127;++r) {
  const float next=20.f*std::exp2(synLpOctaves(r));
  TEST_ASSERT_TRUE(next>=fc && next<25000);fc=next;
  const float nq=synLpQ(r),ng=synLpGain(r);
  TEST_ASSERT_TRUE(nq>=q && nq<=240);TEST_ASSERT_TRUE(ng<=gain && ng>.1f);q=nq;gain=ng;
  TEST_ASSERT_TRUE(synAttackMs(r)>=a && synAttackMs(r)<22000);a=synAttackMs(r);
  TEST_ASSERT_TRUE(synDecayTauMs(r)>=d && synDecayTauMs(r)<6000);d=synDecayTauMs(r);
 }
 TEST_ASSERT_EQUAL_FLOAT(synLpOctaves(0),synLpOctaves(-10));
 TEST_ASSERT_EQUAL_FLOAT(synLpOctaves(127),synLpOctaves(200));
 TEST_ASSERT_EQUAL(synAttackMs(127),synAttackMs(255));
 TEST_ASSERT_EQUAL(synDecayTauMs(127),synDecayTauMs(255));
 TEST_ASSERT_FLOAT_WITHIN(3,767,synDecaySpanMs(64));
}
int main(){UNITY_BEGIN();RUN_TEST(test_reference_maps_are_bounded_and_monotonic);RUN_TEST(test_syn_lp_resonance_has_measured_level_compensation);RUN_TEST(test_syn_exponential_tail_frees_voice_and_retrigger_is_continuous);RUN_TEST(test_syn_zero_times_and_sustain_target);RUN_TEST(test_measured_classic_lp_cutoff_and_resonance);RUN_TEST(test_syn_attack_matches_recorded_time);RUN_TEST(test_syn_decay_matches_recorded_time);RUN_TEST(test_syn_release_matches_recorded_time);RUN_TEST(test_other_engines_retain_linear_envelope_and_filter);RUN_TEST(test_named_filter_models_retain_their_mapping);RUN_TEST(test_reference_envelope_fade_is_still_short_and_linear);return UNITY_END();}
