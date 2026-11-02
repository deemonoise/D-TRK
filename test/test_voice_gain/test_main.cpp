#include <cmath>
#include <cstring>
#include <initializer_list>
#include <unity.h>
#define private public
#include "synth.h"
#undef private
#include "synth_lines.h"
using namespace mt;
static SynthModel model;
static Synth* synth;
void setUp(){model=SynthModel();auto& m=model.instruments[0];instrSetType(m,InstrType::Chip);m.wave=uint8_t(Wave::Pulse);m.attack=m.decay=m.release=0;m.sustain=m.vol=127;m.mono=true;model.tracks[0].out=TrackOut::Int;model.tracks[0].instr=0;synth=new Synth(model);synth->noteOn(0,60,127);}
void tearDown(){delete synth;}
static Voice& playing(){for(auto& v:synth->voices_)if(v.on)return v;TEST_FAIL_MESSAGE("missing voice");return synth->voices_[0];}
static void volume(int vol,int left=32,int dt=0){model.instruments[0].vol=vol;synth->cacheInstrument(0);synth->ctlLeft_=left;synth->control(playing(),dt);}
void test_volume_change_keeps_first_sample_continuous(){auto& v=playing();float warm[8]={};synth->renderVoice(v,warm,8);volume(0,24);float out[1]={};synth->renderVoice(v,out,1);TEST_ASSERT_FLOAT_WITHIN(.00001f,1,out[0]);}
void test_volume_retarget_keeps_reached_level(){auto& v=playing();volume(0);float first[8]={};synth->renderVoice(v,first,8);volume(127,24);float next[1]={};synth->renderVoice(v,next,1);TEST_ASSERT_FLOAT_WITHIN(.00001f,.75f,next[0]);}
void test_first_note_has_no_extra_gain_attack(){auto& v=playing();float out[1]={};synth->renderVoice(v,out,1);TEST_ASSERT_FLOAT_WITHIN(.00001f,1,out[0]);}

struct Bank final:SampleSource {
 int16_t values[8]={1000,1000,1000,1000,1000,1000,1000,1000};
 bool find(const char*,SampleData& d)const override{d=SampleData();d.whole=values;d.frames=8;d.rate=kSynthRate;return true;}
};static Bank bank;
static Voice& startEngine(InstrType type,int machine=0){
 delete synth;model=SynthModel();auto& m=model.instruments[0];instrSetType(m,type);m.machine=machine;m.attack=m.decay=m.release=0;m.sustain=m.vol=127;m.mono=true;
 m.wave=uint8_t(Wave::Pulse);m.synOsc[0]=uint8_t(SynOsc::Saw);m.synOsc[1]=uint8_t(SynOsc::Square);m.macro[kMacMix]=64;
 m.loop=uint8_t(LoopMode::Forward);strcpy(m.sample,"s");m.root=60;
 if(type==InstrType::Fm)m.machine=uint8_t(FmMachine::Tone);
 model.tracks[0].out=TrackOut::Int;model.tracks[0].instr=0;synth=new Synth(model);synth->setBank(&bank);synth->noteOn(0,60,127);return playing();
}
void test_gain_reaches_zero_and_steady_fast_path(){auto& v=playing();volume(0);float out[32]={};synth->renderVoice(v,out,32);for(int i=0;i<32;i++)TEST_ASSERT_FLOAT_WITHIN(.00001f,1-i/32.f,out[i]);TEST_ASSERT_FLOAT_WITHIN(.000001f,0,v.ampNow);volume(0,32,32);TEST_ASSERT_EQUAL_FLOAT(0,v.ampStep);float silent[32]={};synth->renderVoice(v,silent,32);for(float x:silent)TEST_ASSERT_EQUAL_FLOAT(0,x);}
void test_gain_partition_invariance_all_engines(){
 for(auto type:{InstrType::Chip,InstrType::Sample,InstrType::Fm,InstrType::Synth,InstrType::String,InstrType::Lofi,InstrType::Drum})for(int variant=0;variant<(type==InstrType::Drum?2:1);++variant){
  auto& a=startEngine(type,variant?int(DrumMachine::Bda):0);float warm[32]={};synth->renderVoice(a,warm,32);volume(0);
  synth->voices_[1]=a;auto& b=synth->voices_[1];memcpy(voiceLine(1),voiceLine(0),kLineLen*sizeof(int16_t));float whole[32]={},parts[32]={};
  synth->renderVoice(a,whole,32);synth->renderVoice(b,parts,7);synth->renderVoice(b,parts+7,25);
  for(int i=0;i<32;i++)TEST_ASSERT_FLOAT_WITHIN(.000001f,whole[i],parts[i]);TEST_ASSERT_FLOAT_WITHIN(.000001f,a.ampNow,b.ampNow);
 }
}
void test_gain_matches_per_sample_reference_all_engines(){
 for(auto type:{InstrType::Chip,InstrType::Sample,InstrType::Fm,InstrType::Synth,InstrType::String,InstrType::Lofi,InstrType::Drum})for(int variant=0;variant<(type==InstrType::Drum?2:1);++variant){
  auto& a=startEngine(type,variant?int(DrumMachine::Bda):0);float warm[32]={};synth->renderVoice(a,warm,32);volume(0);
  synth->voices_[1]=a;auto& b=synth->voices_[1];b.amp=b.ampNow=1;b.ampStep=0;memcpy(voiceLine(1),voiceLine(0),kLineLen*sizeof(int16_t));float whole[32]={},flat[32]={};
  synth->renderVoice(a,whole,32);synth->renderVoice(b,flat,32);for(int i=0;i<32;i++)TEST_ASSERT_FLOAT_WITHIN(.000001f,flat[i]*(1-i/32.f),whole[i]);
 }
}
void test_vol_lfo_is_smoothed(){auto& v=playing();auto& m=model.instruments[0];m.lfoDest=uint8_t(LfoDest::Vol);m.lfoWave=uint8_t(LfoWave::Square);m.lfoDepth=-64;synth->cacheInstrument(0);synth->ctlLeft_=32;synth->control(v,0);TEST_ASSERT_EQUAL_FLOAT(0,v.amp);float out[32]={};synth->renderVoice(v,out,32);TEST_ASSERT_FLOAT_WITHIN(.00001f,1,out[0]);TEST_ASSERT_FLOAT_WITHIN(.00001f,1.f/32,out[31]);}
void test_legato_keeps_current_gain(){auto& v=playing();volume(0);float out[8]={};synth->renderVoice(v,out,8);model.instruments[0].vol=127;synth->ctlLeft_=24;synth->noteOn(0,62,127);TEST_ASSERT_EQUAL(62,v.note);TEST_ASSERT_FLOAT_WITHIN(.00001f,.75f,v.ampNow);}
void test_short_fade_roundoff_does_not_become_fresh_note(){auto& v=playing();volume(0,3);const float fmaReached=std::fma(v.ampStep,3.f,v.ampNow);float down[3]={};synth->renderVoice(v,down,3);v.ampNow=fmaReached;volume(127,32,32);float up[1]={};synth->renderVoice(v,up,1);TEST_ASSERT_FLOAT_WITHIN(.000001f,0,up[0]);}
void test_idle_mono_slot_starts_with_target_gain(){auto& v=playing();volume(0);float down[8]={};synth->renderVoice(v,down,8);v.env.kill();model.instruments[0].vol=127;synth->ctlLeft_=32;synth->noteOn(0,62,127);TEST_ASSERT_EQUAL(62,v.note);TEST_ASSERT_FLOAT_WITHIN(.000001f,1,v.ampNow);TEST_ASSERT_EQUAL_FLOAT(0,v.ampStep);}
void test_steady_filtered_gain_is_bit_exact(){auto& v=playing();model.instruments[0].fltMode=uint8_t(FltMode::Lp);model.instruments[0].cutoff=64;synth->cacheInstrument(0);synth->control(v,0);auto filter=v.flt;float input[32],expected[32],actual[32];for(int i=0;i<32;i++){input[i]=1;expected[i]=actual[i]=.125f;}int16_t line[kLineLen]={};filter.processAdd(input,expected,32,line);synth->renderVoice(v,actual,32);TEST_ASSERT_EQUAL_MEMORY(expected,actual,sizeof actual);TEST_ASSERT_EQUAL_FLOAT(1,v.ampNow);TEST_ASSERT_EQUAL_FLOAT(0,v.ampStep);}
void test_dynamic_gain_precedes_filter(){auto& v=playing();model.instruments[0].fltMode=uint8_t(FltMode::Lp);model.instruments[0].cutoff=64;synth->cacheInstrument(0);synth->control(v,0);auto filter=v.flt;volume(0);float input[32],expected[32]={},actual[32]={};for(int i=0;i<32;i++)input[i]=1-i/32.f;int16_t line[kLineLen]={};filter.processAdd(input,expected,32,line);synth->renderVoice(v,actual,32);for(int i=0;i<32;i++)TEST_ASSERT_FLOAT_WITHIN(.000001f,expected[i],actual[i]);}
int main(){UNITY_BEGIN();
 RUN_TEST(test_volume_change_keeps_first_sample_continuous);RUN_TEST(test_volume_retarget_keeps_reached_level);RUN_TEST(test_first_note_has_no_extra_gain_attack);
 RUN_TEST(test_gain_reaches_zero_and_steady_fast_path);RUN_TEST(test_gain_partition_invariance_all_engines);RUN_TEST(test_gain_matches_per_sample_reference_all_engines);RUN_TEST(test_vol_lfo_is_smoothed);RUN_TEST(test_legato_keeps_current_gain);RUN_TEST(test_short_fade_roundoff_does_not_become_fresh_note);RUN_TEST(test_idle_mono_slot_starts_with_target_gain);RUN_TEST(test_steady_filtered_gain_is_bit_exact);RUN_TEST(test_dynamic_gain_precedes_filter);
 return UNITY_END();}
