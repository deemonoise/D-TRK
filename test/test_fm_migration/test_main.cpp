#include <unity.h>
#include <vector>
#include <cstring>
#include <cmath>
#include "inst_codec.h"
#include "preset_io.h"
#include "project_io.h"
#include "synth_fm_machines.h"
#include "synth_fm_tonal.h"
using namespace mt;
struct Sink : ByteSink {
  std::vector<uint8_t> bytes;
  bool write(const void* p,size_t n) override { auto b=static_cast<const uint8_t*>(p);bytes.insert(bytes.end(),b,b+n);return true; }
};
struct Source : ByteSource {
  const std::vector<uint8_t>& bytes;size_t at=0;
  explicit Source(const std::vector<uint8_t>& b):bytes(b){}
  bool read(void* p,size_t n) override {if(n>bytes.size()-at)return false;memcpy(p,bytes.data()+at,n);at+=n;return true;}
  bool skip(size_t n) override {if(n>bytes.size()-at)return false;at+=n;return true;}
};
static Project loaded;
static std::vector<Instrument> old;
void setUp(){old.clear();loaded.reset();}
void tearDown(){}
static void u32(std::vector<uint8_t>& b,uint32_t n){for(int i=0;i<4;++i)b.push_back(n>>(i*8));}
static void chunk(std::vector<uint8_t>& b,const char* id,const std::vector<uint8_t>& data){b.insert(b.end(),id,id+4);u32(b,data.size());b.insert(b.end(),data.begin(),data.end());}
static void finish(std::vector<uint8_t>& b){const auto crc=crc32(b.data(),b.size());chunk(b,"CRC ",{uint8_t(crc),uint8_t(crc>>8),uint8_t(crc>>16),uint8_t(crc>>24)});}
static Instrument legacy(int machine){
 Instrument m;m.type=InstrType::Fm;m.machine=machine;strcpy(m.name,"OLD FM");
 const uint8_t knobs[]={85,40,32,70,50};memcpy(m.macro,knobs,5);m.fmSat=53;
 m.vol=87;m.transpose=-3;m.fine=17;m.attack=4;m.decay=9;m.sustain=98;m.release=18;m.mono=true;m.glide=12;
 m.fltMode=1;m.cutoff=93;m.reso=61;m.fenv=-19;m.fAtk=15;m.fDec=77;m.keytrack=34;m.velCut=-7;m.velMac=13;
 m.lfoWave=1;m.lfoDepth=22;m.lfoDest=uint8_t(LfoDest::Col);m.lfoRate=41;m.send=23;m.rsend=36;
 return m;
}
// Frozen pre-FMPA records; no inference from names or from the numeric machine id.
static std::vector<uint8_t> file(bool reverse=false,bool modern=false,bool fmex=true,bool fmin=true){
 std::vector<uint8_t> b={'M','T','R','K',1,0,0,0},inst={uint8_t(old.size())},fm={uint8_t(old.size())},ext={uint8_t(old.size()),4},flt={uint8_t(old.size())};
 for(auto& m:old){uint8_t i[kInstRecSize],f[kFmRecSize],e[kFmExRecSize],l[kFltRecSize];packInst(m,i);packFm(m,f);packFmEx(m,e);packFlt(m,l);inst.insert(inst.end(),i,i+sizeof i);fm.insert(fm.end(),f,f+sizeof f);ext.insert(ext.end(),e,e+sizeof e);flt.insert(flt.end(),l,l+sizeof l);}
 if(modern)chunk(b,"FMPA",{1,2,71,82}); // Short modern records still identify the new generation.
 if(reverse&&fmin)chunk(b,"FMIN",fm);
 chunk(b,"INST",inst);chunk(b,"FLTR",flt);
 if(!reverse&&fmin)chunk(b,"FMIN",fm);
 if(fmex)chunk(b,"FMEX",ext);
 finish(b);return b;
}
static LoadErr load(const std::vector<uint8_t>& b){Source s(b);return loadProject(s,loaded);}
static void expectOk(const std::vector<uint8_t>& b){TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(load(b)));}
void test_eight_old_machine_ids_map_by_instrument_role(){
 for(int i=0;i<8;++i)old.push_back(legacy(i));
 const FmMachine want[]={FmMachine::BdModern,FmMachine::SdBasic,FmMachine::CyAlloy,FmMachine::PcCarbon,FmMachine::Tone,FmMachine::Chord,FmMachine::CpVintage,FmMachine::HhBasic};
 for(bool reverse:{false,true}){expectOk(file(reverse));for(int i=0;i<8;++i){TEST_ASSERT_EQUAL(int(want[i]),loaded.instruments[i].machine);TEST_ASSERT_EQUAL(53,loaded.instruments[i].fmParam[7]);}}
}
void test_migration_keeps_common_instrument_controls(){
 old.push_back(legacy(4));expectOk(file());const auto& a=old[0];const auto& b=loaded.instruments[0];
 TEST_ASSERT_EQUAL_STRING(a.name,b.name);TEST_ASSERT_EQUAL(a.type,b.type);TEST_ASSERT_EQUAL(a.vol,b.vol);TEST_ASSERT_EQUAL(a.transpose,b.transpose);TEST_ASSERT_EQUAL(a.fine,b.fine);
 TEST_ASSERT_EQUAL(a.attack,b.attack);TEST_ASSERT_EQUAL(a.decay,b.decay);TEST_ASSERT_EQUAL(a.sustain,b.sustain);TEST_ASSERT_EQUAL(a.release,b.release);TEST_ASSERT_EQUAL(a.mono,b.mono);TEST_ASSERT_EQUAL(a.glide,b.glide);
 TEST_ASSERT_EQUAL(a.fltMode,b.fltMode);TEST_ASSERT_EQUAL(a.cutoff,b.cutoff);TEST_ASSERT_EQUAL(a.reso,b.reso);TEST_ASSERT_EQUAL(a.fenv,b.fenv);TEST_ASSERT_EQUAL(a.fAtk,b.fAtk);TEST_ASSERT_EQUAL(a.fDec,b.fDec);TEST_ASSERT_EQUAL(a.keytrack,b.keytrack);
 TEST_ASSERT_EQUAL(a.lfoDest,b.lfoDest);TEST_ASSERT_EQUAL(a.lfoDepth,b.lfoDepth);TEST_ASSERT_EQUAL(a.lfoWave,b.lfoWave);TEST_ASSERT_EQUAL(a.lfoRate,b.lfoRate);TEST_ASSERT_EQUAL(a.send,b.send);TEST_ASSERT_EQUAL(a.rsend,b.rsend);TEST_ASSERT_EQUAL(a.velCut,b.velCut);TEST_ASSERT_EQUAL(a.velMac,b.velMac);
 TEST_ASSERT_EQUAL_UINT8_ARRAY(a.macro,b.macro,5);
}
static FmParams params(const Instrument& m){float a[8];for(int i=0;i<8;++i)a[i]=m.fmParam[i];FmParams p;fmMachine(m.machine,a,60,p);return p;}
void test_percussion_decay_and_base_pitch_follow_old_scale(){
 const int ids[]={0,1,2,3,6,7};const float bases[]={55,180,400,200,0,0};
 for(int n=0;n<6;++n){old={legacy(ids[n])};old[0].macro[0]=90;old[0].macro[4]=0;expectOk(file());const auto p=params(loaded.instruments[0]);const float target=fmDecayMs(90);
 TEST_ASSERT_FLOAT_WITHIN(target*.09f,target,p.ampMs);if(bases[n]>0)TEST_ASSERT_FLOAT_WITHIN(bases[n]*.04f,bases[n],p.hz);}
}
void test_tone_shape_selects_modulator_ratio_and_held_modulation(){
 for(int shape:{0,55,110}){old={legacy(4)};old[0].macro[2]=shape;old[0].macro[3]=0;expectOk(file());const auto& m=loaded.instruments[0];const float ratio=shape==0?1.f:shape==55?2.f:3.5f;
 TEST_ASSERT_EQUAL(int(FmMachine::Tone),m.machine);TEST_ASSERT_FLOAT_WITHIN(.001f,ratio,fmToneRatio(m.fmParam[1]));TEST_ASSERT_EQUAL(127,m.fmParam[3]);TEST_ASSERT_EQUAL(0,m.fmParam[6]);TEST_ASSERT_GREATER_THAN(0,m.fmParam[5]);}
}
void test_old_chord_quality_and_root_are_mapped_explicitly(){
 const uint8_t types[]={4,3,5,6,8,7,10,12,21,36,0,25};const uint8_t tune[]={76,76,76,76,76,76,76,73,72,71,64,76};
 for(int i=0;i<12;++i){old={legacy(5)};old[0].macro[2]=(i*127+11)/12;expectOk(file());auto& m=loaded.instruments[0];TEST_ASSERT_EQUAL(int(FmMachine::Chord),m.machine);TEST_ASSERT_EQUAL(types[i],m.fmParam[4]);TEST_ASSERT_EQUAL(tune[i],m.fmParam[0]);}
}
void test_modern_fmpa_suppresses_conversion_for_the_entire_file(){
 old={legacy(4),legacy(5)};expectOk(file(true,true));TEST_ASSERT_EQUAL(4,loaded.instruments[0].machine);TEST_ASSERT_EQUAL(5,loaded.instruments[1].machine);TEST_ASSERT_EQUAL(71,loaded.instruments[0].fmParam[0]);TEST_ASSERT_EQUAL(82,loaded.instruments[0].fmParam[1]);
}
void test_no_fmin_no_conversion_and_non_fm_instruments_are_preserved(){
 old={legacy(4)};expectOk(file(false,false,true,false));TEST_ASSERT_EQUAL(int(FmMachine::Tone),loaded.instruments[0].machine);
 old[0].type=InstrType::Drum;expectOk(file());TEST_ASSERT_EQUAL(4,loaded.instruments[0].machine);TEST_ASSERT_EQUAL(0,loaded.instruments[0].fmParam[7]);
}
void test_without_fmex_over_defaults_off(){old={legacy(4)};expectOk(file(false,false,false));TEST_ASSERT_EQUAL(0,loaded.instruments[0].fmParam[7]);}
void test_saving_converted_project_makes_next_load_idempotent(){
 for(int i=0;i<8;++i)old.push_back(legacy(i));expectOk(file());std::vector<Instrument> first(loaded.instruments,loaded.instruments+8);Sink save;TEST_ASSERT_TRUE(saveProject(loaded,save));expectOk(save.bytes);
 for(int i=0;i<8;++i){TEST_ASSERT_EQUAL(first[i].machine,loaded.instruments[i].machine);TEST_ASSERT_EQUAL_UINT8_ARRAY(first[i].fmParam,loaded.instruments[i].fmParam,8);}
}
void test_old_preset_versions_use_the_same_conversion(){
 const size_t sizes[]={0,kPresetSizeV1,kPresetSizeV2,kPresetSizeV3,kPresetSizeV3,kPresetSizeV5,kPresetSizeV6,kPresetSizeV7,kPresetSizeV8};
 for(int ver=1;ver<=8;++ver){Sink save;auto m=legacy(4);TEST_ASSERT_TRUE(savePreset(m,save));auto b=save.bytes;b.resize(sizes[ver]);b[4]=ver;const auto c=crc32(b.data(),b.size()-4);for(int i=0;i<4;++i)b[b.size()-4+i]=c>>(i*8);Source s(b);Instrument out;TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadPreset(s,out)));TEST_ASSERT_EQUAL(int(FmMachine::Tone),out.machine);TEST_ASSERT_EQUAL(ver<8?0:53,out.fmParam[7]);}
}
void test_unknown_old_machine_is_safe_tone_fallback(){old={legacy(255)};expectOk(file());TEST_ASSERT_EQUAL(int(FmMachine::Tone),loaded.instruments[0].machine);TEST_ASSERT_EQUAL(64,loaded.instruments[0].fmParam[0]);}
void test_conversion_notes_are_reported_only_after_crc_and_reset_on_reload(){
 old={legacy(4),legacy(5)};auto b=file();Source src(b);LoadNotes notes;
 TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(src,loaded,&notes)));TEST_ASSERT_EQUAL(2,notes.fmConverted);
 Sink saved;TEST_ASSERT_TRUE(saveProject(loaded,saved));Source newer(saved.bytes);
 TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(newer,loaded,&notes)));TEST_ASSERT_EQUAL(0,notes.fmConverted);
 b[b.size()-1]^=0x80;Source broken(b);notes.fmConverted=99;
 TEST_ASSERT_EQUAL(int(LoadErr::BadCrc),int(loadProject(broken,loaded,&notes)));TEST_ASSERT_EQUAL(0,notes.fmConverted);
 TEST_ASSERT_EQUAL(4,loaded.instruments[0].machine); // no conversion on a partial/failed load
}
void test_empty_modern_marker_prevents_legacy_id_reinterpretation(){
 old={legacy(4)};auto b=file();b.resize(b.size()-12);chunk(b,"FMPA",{0,8});finish(b);expectOk(b);
 TEST_ASSERT_EQUAL(4,loaded.instruments[0].machine);
}
void test_every_modern_machine_and_control_survives_roundtrip(){
 for(int id=0;id<int(FmMachine::Count);++id){auto& m=loaded.instruments[id];m=legacy(id);for(int k=0;k<8;++k)m.fmParam[k]=(id*17+k*13)%128;}
 std::vector<Instrument> expected(loaded.instruments,loaded.instruments+int(FmMachine::Count));Sink saved;TEST_ASSERT_TRUE(saveProject(loaded,saved));expectOk(saved.bytes);
 for(int i=0;i<int(FmMachine::Count);++i){TEST_ASSERT_EQUAL(expected[i].machine,loaded.instruments[i].machine);TEST_ASSERT_EQUAL_UINT8_ARRAY(expected[i].fmParam,loaded.instruments[i].fmParam,8);}
}
void test_full_old_instrument_prefix_converts_only_fm_slots(){
 for(int i=0;i<kInstruments;++i){old.push_back(legacy(i%8));if(i%3==0)old.back().type=InstrType::Chip;}
 auto b=file(true);Source src(b);LoadNotes notes;TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(src,loaded,&notes)));
 TEST_ASSERT_EQUAL(21,notes.fmConverted);TEST_ASSERT_EQUAL(int(FmMachine::Tone),loaded.instruments[28].machine);
 for(int i=0;i<kInstruments;i+=3){TEST_ASSERT_EQUAL(int(InstrType::Chip),loaded.instruments[i].type);TEST_ASSERT_EQUAL(i%8,loaded.instruments[i].machine);}
}
void test_each_converted_voice_renders_finite_non_silent_audio(){
 for(int id=0;id<8;++id){old={legacy(id)};expectOk(file());const auto p=params(loaded.instruments[0]);FmVoice voice;voice.trigger(false);double energy=0;
  for(int b=0;b<100;++b){float out[64]={};voice.control(p,64);voice.render(out,64,1);for(float x:out){TEST_ASSERT_TRUE(std::isfinite(x));TEST_ASSERT_LESS_THAN_FLOAT(20.f,fabsf(x));energy+=x*x;}}
  TEST_ASSERT_GREATER_THAN_FLOAT(.001f,energy);
 }
}
void test_truncated_and_invalid_fmpa_files_never_report_conversion(){
 old={legacy(4)};auto b=file();b.pop_back();Source shortFile(b);LoadNotes notes;notes.fmConverted=99;
 TEST_ASSERT_EQUAL(int(LoadErr::Truncated),int(loadProject(shortFile,loaded,&notes)));TEST_ASSERT_EQUAL(0,notes.fmConverted);
 b=file();b.resize(b.size()-12);chunk(b,"FMPA",{1,0});finish(b);Source broken(b);notes.fmConverted=99;
 TEST_ASSERT_EQUAL(int(LoadErr::BadValue),int(loadProject(broken,loaded,&notes)));TEST_ASSERT_EQUAL(0,notes.fmConverted);TEST_ASSERT_EQUAL(4,loaded.instruments[0].machine);
}
void test_old_clap_sweep_does_not_become_a_slow_repeat_rate(){
 old={legacy(6)};old[0].macro[kMacSwp]=68;expectOk(file());const auto p=params(loaded.instruments[0]);
 TEST_ASSERT_TRUE(p.bursts==0 || p.burstMs<=20.f);
}
void test_modern_v9_and_v10_presets_keep_their_machine_and_raw_controls(){
 for(int id=0;id<int(FmMachine::Count);++id)for(int ver:{9,10}){
  auto m=legacy(id);for(int k=0;k<8;++k)m.fmParam[k]=(id*17+k*13)%128;
  Sink save;TEST_ASSERT_TRUE(savePreset(m,save));auto b=save.bytes;
  if(ver==9){b.resize(kPresetSizeV9);b[4]=9;auto crc=crc32(b.data(),b.size()-4);for(int k=0;k<4;++k)b[b.size()-4+k]=crc>>(8*k);}
  Source src(b);Instrument out;TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadPreset(src,out)));
  TEST_ASSERT_EQUAL(m.machine,out.machine);TEST_ASSERT_EQUAL_UINT8_ARRAY(m.fmParam,out.fmParam,8);
 }
}
void test_tone_feedback_and_zero_colour_pluck_remain_audible(){
 old={legacy(4)};old[0].macro[kMacShp]=32;old[0].macro[kMacCol]=96;old[0].macro[kMacSwp]=0;expectOk(file());
 auto p=params(loaded.instruments[0]);TEST_ASSERT_GREATER_THAN_FLOAT(0,p.op[4].fb);TEST_ASSERT_GREATER_THAN_FLOAT(0,p.op[4].level);
 old[0].macro[kMacShp]=0;old[0].macro[kMacCol]=0;old[0].macro[kMacSwp]=100;expectOk(file());
 p=params(loaded.instruments[0]);TEST_ASSERT_GREATER_THAN_FLOAT(0,p.op[4].level);
 old[0].macro[kMacSwp]=0;expectOk(file());p=params(loaded.instruments[0]);TEST_ASSERT_EQUAL_FLOAT(0,p.op[4].level);
}
int main(){UNITY_BEGIN();RUN_TEST(test_tone_feedback_and_zero_colour_pluck_remain_audible);RUN_TEST(test_modern_v9_and_v10_presets_keep_their_machine_and_raw_controls);RUN_TEST(test_old_clap_sweep_does_not_become_a_slow_repeat_rate);RUN_TEST(test_each_converted_voice_renders_finite_non_silent_audio);RUN_TEST(test_truncated_and_invalid_fmpa_files_never_report_conversion);RUN_TEST(test_conversion_notes_are_reported_only_after_crc_and_reset_on_reload);RUN_TEST(test_empty_modern_marker_prevents_legacy_id_reinterpretation);RUN_TEST(test_every_modern_machine_and_control_survives_roundtrip);RUN_TEST(test_full_old_instrument_prefix_converts_only_fm_slots);RUN_TEST(test_eight_old_machine_ids_map_by_instrument_role);RUN_TEST(test_migration_keeps_common_instrument_controls);RUN_TEST(test_percussion_decay_and_base_pitch_follow_old_scale);RUN_TEST(test_tone_shape_selects_modulator_ratio_and_held_modulation);RUN_TEST(test_old_chord_quality_and_root_are_mapped_explicitly);RUN_TEST(test_modern_fmpa_suppresses_conversion_for_the_entire_file);RUN_TEST(test_no_fmin_no_conversion_and_non_fm_instruments_are_preserved);RUN_TEST(test_without_fmex_over_defaults_off);RUN_TEST(test_saving_converted_project_makes_next_load_idempotent);RUN_TEST(test_old_preset_versions_use_the_same_conversion);RUN_TEST(test_unknown_old_machine_is_safe_tone_fallback);return UNITY_END();}
