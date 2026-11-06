#include <unity.h>
#include <cmath>
#include <vector>
#include <cstring>
#include <memory>
#include "preset_io.h"
#include "synth_preview.h"
#include "synth_syn_sat.h"
#include "synth_vfilter.h"
#include "voice_cost.h"
#include "inst_codec.h"
#include "synth_syn_calibration.h"
using namespace mt;
static float samples[16384];
void setUp() {}
void tearDown() {}
static double harmonic(int h){double re=0,im=0;for(int n=0;n<16384;++n){double a=6.283185307179586*220*h*n/kSynthRate,w=.5-.5*cos(6.283185307179586*n/16383);re+=samples[n]*w*cos(a);im+=samples[n]*w*sin(a);}return hypot(re,im);}
static void tone(uint8_t type,uint8_t gain){SynSat s;s.set(type,gain);for(int n=0;n<16384;++n)samples[n]=sin(6.283185307179586*220*n/kSynthRate);s.processBlock(samples,16384);}
void test_off_is_bit_exact_at_every_level(){for(int g:{0,64,127}){SynSat s;s.set(0,g);float a[]={-.4f,0,.75f};s.processBlock(a,3);TEST_ASSERT_EQUAL_FLOAT(-.4f,a[0]);TEST_ASSERT_EQUAL_FLOAT(.75f,a[2]);}}
void test_rectifier_morph_matches_independent_virus_harmonics(){tone(7,64);TEST_ASSERT_LESS_THAN_FLOAT(.001,harmonic(2)/harmonic(1));tone(7,96);TEST_ASSERT_FLOAT_WITHIN(.025,.426,harmonic(2)/harmonic(1));tone(7,127);TEST_ASSERT_GREATER_THAN_FLOAT(100,harmonic(2)/harmonic(1));}
void test_shaper_is_sinusoidal_rather_than_triangle_fold(){tone(6,64);TEST_ASSERT_FLOAT_WITHIN(.1,2.06,harmonic(3)/harmonic(1));TEST_ASSERT_FLOAT_WITHIN(.08,.88,harmonic(5)/harmonic(1));}
void test_soft_and_hard_retain_distinct_asymmetric_character(){tone(2,96);TEST_ASSERT_FLOAT_WITHIN(.025,.257,harmonic(2)/harmonic(1));tone(4,96);TEST_ASSERT_FLOAT_WITHIN(.03,.054,harmonic(2)/harmonic(1));TEST_ASSERT_FLOAT_WITHIN(.04,.292,harmonic(3)/harmonic(1));}
void test_nonlinearities_are_finite_bounded_and_block_invariant(){for(int k=1;k<8;++k){SynSat a,b;a.set(k,127);b.set(k,127);float whole[512],parts[512];for(int n=0;n<512;++n)whole[n]=parts[n]=4*sin(n*.31);a.processBlock(whole,512);for(int n=0;n<512;n+=32)b.processBlock(parts+n,32);for(int n=0;n<512;++n){TEST_ASSERT_TRUE(std::isfinite(whole[n]));TEST_ASSERT_LESS_THAN_FLOAT(10,fabs(whole[n]));TEST_ASSERT_EQUAL_FLOAT(whole[n],parts[n]);}}}
void test_reserved_record_roundtrip_and_legacy_default(){Instrument m;m.fmSat=96;m.synSatType=7;m.chipStep=true;uint8_t b[kFmExRecSize];packFmEx(m,b);TEST_ASSERT_EQUAL(4,sizeof b);Instrument out;unpackFmEx(b,4,out);TEST_ASSERT_EQUAL(7,out.synSatType);TEST_ASSERT_EQUAL(96,out.fmSat);TEST_ASSERT_TRUE(out.chipStep);b[2]=0;unpackFmEx(b,4,out);TEST_ASSERT_EQUAL(0,out.synSatType);b[2]=255;unpackFmEx(b,4,out);TEST_ASSERT_EQUAL(0,out.synSatType);out.synSatType=7;unpackFmEx(b,2,out);TEST_ASSERT_EQUAL(0,out.synSatType);}
void test_model_size_does_not_grow(){TEST_ASSERT_EQUAL(30624,sizeof(SynthModel)-kInstruments*sizeof(Instrument::mod));TEST_ASSERT_LESS_OR_EQUAL(664,sizeof(SynVoice));}
void test_analog_cutoff_matches_measured_four_pole_range(){TEST_ASSERT_FLOAT_WITHIN(50,815,20*exp2(synAnalogOctaves(64,4)));TEST_ASSERT_FLOAT_WITHIN(120,1950,20*exp2(synAnalogOctaves(80,4)));}
void test_analog_poles_change_rolloff_without_voice_growth(){float in[4096],out[4096];double rms[2];for(int mode=0;mode<2;++mode){VoiceFilter f;f.set(mode?FltKind::Analog4:FltKind::Analog1,800,0,32);for(int n=0;n<4096;++n){in[n]=.02*sin(6.283185307179586*4000*n/kSynthRate);out[n]=0;}f.processAdd(in,out,4096,nullptr);double sum=0;for(int n=2048;n<4096;++n)sum+=out[n]*out[n];rms[mode]=sqrt(sum/2048);}TEST_ASSERT_TRUE(rms[0]>rms[1]*10);}
void test_analog_feedback_stays_finite_at_extremes(){for(int k=int(FltKind::Analog1);k<=int(FltKind::Analog4);++k)for(int c:{20,800,19000}){VoiceFilter f;f.set(FltKind(k),c,127,127);float x[128],y[128];for(int b=0;b<100;++b){for(int n=0;n<128;++n){x[n]=b<50?3*sin((n+b*128)*.3):0;y[n]=0;}f.processAdd(x,y,128,nullptr);for(float a:y){TEST_ASSERT_TRUE(std::isfinite(a));TEST_ASSERT_LESS_THAN_FLOAT(20,fabs(a));}}}}

struct Sink : ByteSink {
  std::vector<uint8_t> data;
  bool write(const void* p, size_t n) override { auto b = static_cast<const uint8_t*>(p); data.insert(data.end(), b, b + n); return true; }
};
struct Source : ByteSource {
  const std::vector<uint8_t>& data; size_t pos = 0;
  explicit Source(const Sink& s) : data(s.data) {}
  bool read(void* p, size_t n) override { if(n > data.size() - pos)return false; memcpy(p,data.data()+pos,n);pos+=n;return true; }
  bool skip(size_t n) override { if(n > data.size() - pos)return false; pos+=n;return true; }
};
void test_project_and_preset_keep_saturation_and_analog_controls() {
  auto project=std::make_unique<Project>();auto& m=project->instruments[3];instrSetType(m,InstrType::Synth);
  m.synSatType=7;m.fmSat=109;m.fltMode=uint8_t(FltMode::Analog3);
  Sink preset;TEST_ASSERT_TRUE(savePreset(m,preset));TEST_ASSERT_EQUAL(kPresetSize,preset.data.size());
  Source pi(preset);Instrument loaded;TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadPreset(pi,loaded)));
  TEST_ASSERT_EQUAL(7,loaded.synSatType);TEST_ASSERT_EQUAL(109,loaded.fmSat);TEST_ASSERT_EQUAL(m.fltMode,loaded.fltMode);
  Sink song;TEST_ASSERT_TRUE(saveProject(*project,song));*project=Project();Source si(song);
  TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(si,*project)));
  TEST_ASSERT_EQUAL(7,project->instruments[3].synSatType);TEST_ASSERT_EQUAL(109,project->instruments[3].fmSat);
  TEST_ASSERT_EQUAL(int(FltMode::Analog3),project->instruments[3].fltMode);
}
void test_wave_preview_saturates_mod_only_and_matches_real_voice() {
  SynParams p;p.mode[0]=uint8_t(SynOsc::Sine);p.satType=7;p.satLevel=127;
  SynPreviewModel model;SynPreviewFrame frame;model.render(p,{},false,frame);
  SynVoice voice;voice.trigger();int point=0;float dryDifference=0;
  for(int off=0;off<kSynPreviewPoints*kSynPreviewStride;off+=32){
    float x[32]={};voice.control(p,32);voice.render(x,32,1);
    for(int i=3;i<32;i+=4){TEST_ASSERT_FLOAT_WITHIN(1e-6f,x[i],frame.mod[point]);dryDifference+=fabs(frame.mod[point]-frame.mix[point]);++point;}
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(10,dryDifference);
}
void test_analog_does_not_double_charge_ignored_saturation() {
  Instrument m;instrSetType(m,InstrType::Synth);m.fltMode=uint8_t(FltMode::Analog4);const int before=voiceCost(m,false);
  m.synSatType=6;TEST_ASSERT_EQUAL(before,voiceCost(m,false));m.fltMode=uint8_t(FltMode::Lp);const int saturated=voiceCost(m,false);
  m.synSatType=0;TEST_ASSERT_GREATER_THAN(voiceCost(m,false),saturated);
}

void test_soft_preserves_measured_fourth_harmonic(){tone(2,96);TEST_ASSERT_FLOAT_WITHIN(.012f,.053f,harmonic(4)/harmonic(1));TEST_ASSERT_FLOAT_WITHIN(.012f,.027f,harmonic(5)/harmonic(1));}
int main(){UNITY_BEGIN();RUN_TEST(test_soft_preserves_measured_fourth_harmonic);RUN_TEST(test_project_and_preset_keep_saturation_and_analog_controls);RUN_TEST(test_wave_preview_saturates_mod_only_and_matches_real_voice);RUN_TEST(test_analog_does_not_double_charge_ignored_saturation);RUN_TEST(test_off_is_bit_exact_at_every_level);RUN_TEST(test_rectifier_morph_matches_independent_virus_harmonics);RUN_TEST(test_shaper_is_sinusoidal_rather_than_triangle_fold);RUN_TEST(test_soft_and_hard_retain_distinct_asymmetric_character);RUN_TEST(test_nonlinearities_are_finite_bounded_and_block_invariant);RUN_TEST(test_reserved_record_roundtrip_and_legacy_default);RUN_TEST(test_model_size_does_not_grow);RUN_TEST(test_analog_cutoff_matches_measured_four_pole_range);RUN_TEST(test_analog_poles_change_rolloff_without_voice_growth);RUN_TEST(test_analog_feedback_stays_finite_at_extremes);return UNITY_END();}
