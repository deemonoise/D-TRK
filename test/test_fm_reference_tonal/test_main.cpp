#include <cmath>
#include <cstring>
#include <unity.h>
#include "synth_fm.h"
#include "synth_osc.h"

using namespace mt;
void setUp() {}
void tearDown() {}
static float a[kSynthRate * 2], b[kSynthRate * 2];
static void render(FmVoice& voice, const FmParams& p, float* dst, int n, bool split=false) {
  memset(dst,0,sizeof(float)*n);
  for (int i=0;i<n;i+=32) {
    const int len=n-i<32?n-i:32;
    voice.control(p,32);
    if(split && len>7){voice.render(dst+i,7,1);voice.render(dst+i+7,len-7,1);}
    else voice.render(dst+i,len,1);
  }
}
static float rms(const float* x,int start,int end) {
  double sum=0;for(int i=start;i<end;++i)sum+=x[i]*x[i];return sqrt(sum/(end-start));
}
static float at(const float* x,int n,float hz) {
  double re=0,im=0;
  for(int i=0;i<n;++i){const double t=6.283185307179586*hz*i/kSynthRate;re+=x[i]*cos(t);im-=x[i]*sin(t);}
  return static_cast<float>(sqrt(re*re+im*im)*2/n);
}
static FmParams tone(float mod=64) {
  float raw[8]={64,64,0,110,0,mod,0,0}; FmParams p;
  TEST_ASSERT_TRUE(fmTonalMachine(8,raw,60,p));return p;
}
void test_tone_ratio_and_pitch_follow_measurements() {
  auto p=tone();TEST_ASSERT_FLOAT_WITHIN(.2f,261.555f,p.hz);
  TEST_ASSERT_EQUAL(4,p.op[0].mod);TEST_ASSERT_FALSE(p.op[4].output);
  TEST_ASSERT_FLOAT_WITHIN(.002f,4.4f,p.op[4].ratio);
  float raw[8]={64,127,0,110,0,64,0,0};fmTonalMachine(8,raw,72,p);
  TEST_ASSERT_FLOAT_WITHIN(.4f,523.292f,p.hz);TEST_ASSERT_FLOAT_WITHIN(.003f,15.f,p.op[4].ratio);
}
void test_tune_has_measured_end_plateaus() {
  float raw[8]={0,64,0,110,0,64,0,0};FmParams p,q;
  fmTonalMachine(8,raw,60,p);raw[0]=32;fmTonalMachine(8,raw,60,q);
  TEST_ASSERT_FLOAT_WITHIN(.0001f,p.hz,q.hz);TEST_ASSERT_FLOAT_WITHIN(.2f,65.386f,p.hz);
  raw[0]=96;fmTonalMachine(8,raw,60,p);raw[0]=127;fmTonalMachine(8,raw,60,q);
  TEST_ASSERT_FLOAT_WITHIN(.0001f,p.hz,q.hz);
}
void test_pm_renderer_emits_selected_sidebands() {
  auto p=tone(127);p.ampMs=0;p.outputGain=1;p.attackMs=0;
  FmVoice v;v.trigger(false);render(v,p,a,kSynthRate);
  const float carrier=at(a,kSynthRate,p.hz);
  const float upper=at(a,kSynthRate,p.hz*(1+p.op[4].ratio));
  const float wrong=at(a,kSynthRate,1000);
  TEST_ASSERT_TRUE(carrier>.3f);TEST_ASSERT_TRUE(upper>.25f);TEST_ASSERT_TRUE(upper>wrong*20);
}
void test_feedback_with_zero_modulation_does_not_change_carrier() {
  auto p=tone(0),q=p;q.op[4].fb=2.5f;
  FmVoice v,w;v.trigger(false);w.trigger(false);render(v,p,a,4096);render(w,q,b,4096);
  for(int i=0;i<4096;++i)TEST_ASSERT_FLOAT_WITHIN(1e-6f,a[i],b[i]);
}
void test_control_block_partition_does_not_change_samples() {
  auto p=tone(127);p.op[4].fb=1.2f;
  FmVoice v,w;v.trigger(false);w.trigger(false);render(v,p,a,4096);render(w,p,b,4096,true);
  for(int i=0;i<4096;++i)TEST_ASSERT_FLOAT_WITHIN(1e-6f,a[i],b[i]);
}
void test_tone_decays_while_chord_keeps_sustain() {
  float raw[8]={64,32,0,32,16,64,0,0};FmParams chord;
  TEST_ASSERT_TRUE(fmTonalMachine(6,raw,60,chord));
  FmVoice c;c.trigger(false);render(c,chord,a,2*kSynthRate);
  TEST_ASSERT_TRUE(rms(a,kSynthRate,2*kSynthRate)>.01f);TEST_ASSERT_FALSE(c.done());
  raw[1]=64;FmParams t;fmTonalMachine(8,raw,60,t);FmVoice v;v.trigger(false);render(v,t,b,2*kSynthRate);
  TEST_ASSERT_TRUE(rms(b,kSynthRate,2*kSynthRate)<rms(a,kSynthRate,2*kSynthRate)*.01f);
}
void test_chord_type_upper_cc_range_uses_observed_last_signature() {
  float raw[8]={64,32,0,110,37,64,0,0};FmParams p,q;
  fmTonalMachine(6,raw,60,p);raw[4]=127;fmTonalMachine(6,raw,60,q);
  for(int i=0;i<kFmOps;++i){TEST_ASSERT_FLOAT_WITHIN(1e-6f,p.op[i].ratio,q.op[i].ratio);TEST_ASSERT_FLOAT_WITHIN(1e-6f,p.op[i].level,q.op[i].level);}
}
void test_toy_brightness_controls_interact() {
  float raw[8]={64,32,0,80,64,127,0,0};FmParams p,q,r;
  fmTonalMachine(7,raw,60,p);raw[5]=0;raw[6]=127;fmTonalMachine(7,raw,60,q);raw[5]=127;fmTonalMachine(7,raw,60,r);
  TEST_ASSERT_TRUE(r.op[1].level>p.op[1].level+q.op[1].level);
}
void test_extreme_pitch_and_feedback_render_finite() {
  auto p=tone(127);p.hz=20000;p.op[4].ratio=15;p.op[4].fb=8;p.op[4].level=40;p.sat=1;
  FmVoice v;v.trigger(false);render(v,p,a,4096);
  for(int i=0;i<4096;++i){TEST_ASSERT_TRUE(std::isfinite(a[i]));TEST_ASSERT_TRUE(fabsf(a[i])<8);}
}
void test_reset_phase_controls_repeatability() {
  auto p=tone(127);p.ampMs=0;p.attackMs=0;
  FmVoice v;v.trigger(false);render(v,p,a,4096);v.trigger(false);render(v,p,b,4096);
  for(int i=0;i<4096;++i)TEST_ASSERT_FLOAT_WITHIN(1e-6f,a[i],b[i]);
  p.resetPhase=false;v.trigger(false);render(v,p,b,4096);
  TEST_ASSERT_TRUE(fabsf(a[0]-b[0])>.001f);
}
void test_chord_balance_changes_voicing_instead_of_adding_generic_harmonics() {
 float raw[8]={64,32,0,110,16,32,0,0};FmParams p;
 fmTonalMachine(6,raw,60,p);
 TEST_ASSERT_FLOAT_WITHIN(.002f,1.259921f,p.op[0].ratio); // -8 + 12 = +4 semitones
 TEST_ASSERT_FLOAT_WITHIN(.002f,1.498307f,p.op[1].ratio); // -5 + 12 = +7
 TEST_ASSERT_FLOAT_WITHIN(.002f,1.f,p.op[3].ratio);
 raw[5]=0;fmTonalMachine(6,raw,60,p);
 TEST_ASSERT_EQUAL_FLOAT(0,p.op[0].level);TEST_ASSERT_TRUE(p.op[3].level>0);
}
void test_toy_uses_observed_inharmonic_modal_frequencies() {
 float raw[8]={64,32,0,80,64,64,64,0};FmParams p;fmTonalMachine(7,raw,60,p);
 TEST_ASSERT_FLOAT_WITHIN(.01f,621.235f/p.hz,p.op[1].ratio);
 TEST_ASSERT_FLOAT_WITHIN(.01f,1096.364f/p.hz,p.op[2].ratio);
 TEST_ASSERT_TRUE(p.op[1].output);TEST_ASSERT_EQUAL(-1,p.op[0].mod);
}
void test_short_decay_transient_matches_reference_energy() {
 float raw[8]={64,64,0,0,0,64,0,0};FmParams p;
 fmTonalMachine(8,raw,60,p);FmVoice t;t.trigger(false);render(t,p,a,4410);
 const float toneDb=20*log10f(rms(a,264,3351)*.0857848567f);
 TEST_ASSERT_FLOAT_WITHIN(3.f,-78.701f,toneDb);
 float toyRaw[8]={64,32,0,0,64,64,64,0};fmTonalMachine(7,toyRaw,60,p);
 FmVoice toy;toy.trigger(false);render(toy,p,a,4410);
 const float toyDb=20*log10f(rms(a,264,3351)*.0857848567f);
 TEST_ASSERT_FLOAT_WITHIN(3.f,-60.361f,toyDb);
}
void test_static_tone_pair_preserves_arbitrary_splits_and_control_span() {
 FmParams p=tone();FmVoice one,split;one.trigger(false);split.trigger(false);
 memset(a,0,4096*sizeof(float));memset(b,0,4096*sizeof(float));
 for(int pos=0;pos<4096;pos+=32) {
   one.control(p,17);split.control(p,17);
   one.render(a+pos,32,1);
   split.render(b+pos,3,1);split.render(b+pos+3,5,1);
   split.render(b+pos+8,11,1);split.render(b+pos+19,13,1);
 }
 TEST_ASSERT_EQUAL_MEMORY(a,b,4096*sizeof(float));
}
int main(){UNITY_BEGIN();
 RUN_TEST(test_static_tone_pair_preserves_arbitrary_splits_and_control_span);RUN_TEST(test_short_decay_transient_matches_reference_energy);RUN_TEST(test_toy_uses_observed_inharmonic_modal_frequencies);RUN_TEST(test_chord_balance_changes_voicing_instead_of_adding_generic_harmonics);RUN_TEST(test_tone_ratio_and_pitch_follow_measurements);RUN_TEST(test_tune_has_measured_end_plateaus);
 RUN_TEST(test_pm_renderer_emits_selected_sidebands);RUN_TEST(test_feedback_with_zero_modulation_does_not_change_carrier);
 RUN_TEST(test_control_block_partition_does_not_change_samples);RUN_TEST(test_tone_decays_while_chord_keeps_sustain);
 RUN_TEST(test_chord_type_upper_cc_range_uses_observed_last_signature);RUN_TEST(test_toy_brightness_controls_interact);
 RUN_TEST(test_extreme_pitch_and_feedback_render_finite);RUN_TEST(test_reset_phase_controls_repeatability);
 return UNITY_END();}
