#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_fm.h"
#include "synth_osc.h"
using namespace mt;
void setUp(){}
void tearDown(){}
static float first[kSynthRate],second[kSynthRate];
static void run(FmVoice& v,const FmParams& p,float* out,int count) {
 memset(out,0,count*sizeof(float));
 for(int i=0;i<count;i+=32){const int n=count-i<32?count-i:32;v.control(p,32);v.render(out+i,n,1);}
}
static float energy(const float* x,int begin,int end){double s=0;for(int i=begin;i<end;++i)s+=x[i]*x[i];return static_cast<float>(s/(end-begin));}
void test_unconfigured_voice_is_silent() {
 FmVoice v;FmParams p;v.trigger(false);run(v,p,first,1024);
 for(int i=0;i<1024;++i)TEST_ASSERT_EQUAL_FLOAT(0,first[i]);
}
void test_output_flags_prevent_modulator_leak() {
 FmParams p;p.hz=440;p.op[4].level=2;p.op[4].output=false;p.oneShot=false;
 FmVoice v;v.trigger(false);run(v,p,first,2048);
 TEST_ASSERT_EQUAL_FLOAT(0,energy(first,0,2048));
}
void test_invalid_cycle_route_is_ignored() {
 FmParams p;p.hz=440;p.op[0].level=1;p.op[0].mod=0;p.oneShot=false;
 FmVoice v,w;v.trigger(false);w.trigger(false);run(v,p,first,2048);p.op[0].mod=-1;run(w,p,second,2048);
 for(int i=0;i<2048;++i)TEST_ASSERT_FLOAT_WITHIN(1e-6f,first[i],second[i]);
}
void test_independent_second_noise_branch_can_sound_alone() {
 FmParams p;p.noise2=1;p.noise2Ms=100;p.noise2FilterHz=5000;p.noise2FilterMode=Svf::Mode::Hp;
 FmVoice v;v.trigger(false);run(v,p,first,kSynthRate);
 TEST_ASSERT_TRUE(energy(first,100,2000)>.001f);
 TEST_ASSERT_TRUE(energy(first,20000,40000)<1e-12f);
}
void test_second_noise_envelope_does_not_share_first_decay() {
 FmParams p;p.noise2=1;p.noise2Ms=3000;p.noise2FilterHz=6000;p.noise2FilterMode=Svf::Mode::Hp;p.noiseMs=1;
 FmVoice v;v.trigger(false);run(v,p,first,kSynthRate);
 TEST_ASSERT_TRUE(energy(first,20000,25000)>1e-4f);
}
void test_one_shot_completion_and_sustain() {
 FmParams p;p.hz=440;p.op[0].level=1;p.ampMs=100;
 FmVoice v;v.trigger(false);run(v,p,first,kSynthRate);TEST_ASSERT_TRUE(v.done());
 p.ampSustain=.2f;FmVoice w;w.trigger(false);run(w,p,first,kSynthRate);TEST_ASSERT_FALSE(w.done());
 TEST_ASSERT_TRUE(energy(first,30000,40000)>.01f);
}
void test_over_increases_level_and_remains_bounded() {
 FmParams p;p.hz=440;p.op[0].level=1;p.oneShot=false;p.outputGain=.05f;
 FmVoice v,w;v.trigger(false);w.trigger(false);run(v,p,first,4096);p.sat=1;run(w,p,second,4096);
 TEST_ASSERT_TRUE(energy(second,100,4096)>energy(first,100,4096)*4);
 for(int i=0;i<4096;++i)TEST_ASSERT_TRUE(fabsf(second[i])<1);
}
void test_nonfinite_control_values_do_not_poison_output() {
 FmParams p;p.hz=NAN;p.op[0].level=INFINITY;p.op[1].ratio=NAN;p.noise=NAN;p.sat=INFINITY;
 FmVoice v;v.trigger(false);run(v,p,first,4096);
 for(int i=0;i<4096;++i)TEST_ASSERT_TRUE(isfinite(first[i]));
}
void test_wave_morph_keeps_the_carrier_fundamental() {
 FmParams p;p.hz=441;p.op[0].level=1;p.op[0].wave=1.5f;p.oneShot=false;p.attackMs=0;
 FmVoice v;v.trigger(false);run(v,p,first,44100);double correlation=0;
 for(int i=0;i<44100;++i)correlation+=first[i]*sin(6.283185307179586*441*i/kSynthRate);
 TEST_ASSERT_TRUE(correlation*2/44100>.5);
}
void test_phase_preserving_retrigger_does_not_restart_attack_at_zero() {
 FmParams p;p.hz=1000;p.op[0].level=1;p.attackMs=5;p.oneShot=false;
 FmVoice v;v.trigger(false);run(v,p,first,1000);const float before=first[999];
 v.trigger(true);v.control(p,32);float after=0;v.render(&after,1,1);
 TEST_ASSERT_FLOAT_WITHIN(.25f,before,after);
}
void test_post_gain_preserves_the_overdrive_operating_point() {
 FmParams p;p.hz=440;p.op[0].level=1;p.outputGain=.3f;p.sat=1;p.oneShot=false;
 FmVoice v,w;v.trigger(false);w.trigger(false);run(v,p,first,4096);p.postGain=.5f;run(w,p,second,4096);
 for(int i=0;i<4096;++i)TEST_ASSERT_FLOAT_WITHIN(1e-6f,first[i]*.5f,second[i]);
}
void test_over_compensation_changes_level_after_distortion() {
 FmParams p;p.hz=440;p.op[0].level=1;p.outputGain=.3f;p.sat=1;p.oneShot=false;
 FmVoice v,w;v.trigger(false);w.trigger(false);run(v,p,first,4096);p.overComp=1;run(w,p,second,4096);
 for(int i=0;i<4096;++i)TEST_ASSERT_FLOAT_WITHIN(1e-6f,first[i]*2,second[i]);
}
void test_retrigger_reset_phase_remains_effective_while_choking() {
 FmParams p;p.hz=440;p.op[0].level=1;p.oneShot=false;p.resetPhase=false;
 FmVoice warm;warm.trigger(false);run(warm,p,first,1000);FmVoice keep=warm,reset=warm;
 keep.trigger(true);reset.trigger(true);keep.control(p,32);p.resetPhase=true;reset.control(p,32);
 float a[64]={},b[64]={};keep.render(a,64,1);reset.render(b,64,1);float difference=0;
 for(int i=32;i<64;++i)difference+=fabsf(a[i]-b[i]);
 TEST_ASSERT_TRUE(difference>1);
}
void test_feedback_control_starts_at_previous_value() {
 FmParams p;p.hz=440;p.op[0].level=1;p.op[0].mod=4;p.op[4].ratio=4.4f;
 p.op[4].level=1.2f;p.op[4].output=false;p.oneShot=false;
 FmVoice warm;warm.trigger(false);run(warm,p,first,1000);FmVoice old=warm,changed=warm;
 old.control(p,32);p.op[4].fb=2;changed.control(p,32);
 float a=0,b=0;old.render(&a,1,1);changed.render(&b,1,1);TEST_ASSERT_FLOAT_WITHIN(1e-6f,a,b);
}
void test_wave_control_starts_at_previous_value() {
 FmParams p;p.hz=440;p.op[0].level=1;p.oneShot=false;
 FmVoice warm;warm.trigger(false);run(warm,p,first,1000);FmVoice old=warm,changed=warm;
 old.control(p,32);p.op[0].wave=.5f;changed.control(p,32);
 float a=0,b=0;old.render(&a,1,1);changed.render(&b,1,1);TEST_ASSERT_FLOAT_WITHIN(1e-6f,a,b);
}
void test_additive_kernel_preserves_arbitrary_block_partition() {
 FmParams p;p.hz=130;p.oneShot=false;p.sat=.4f;p.outputGain=.2f;
 p.noise=.01f;p.noise2=.02f;p.noise2FilterHz=6000;p.noise2FilterMode=Svf::Mode::Hp;
 for(int i=0;i<kFmOps;++i){p.op[i].ratio=1+i*.37f;p.op[i].level=.1f;
   p.op[i].wave=i%3==0?0:i%3==1?3:4.25f;}
 FmVoice a,b;a.trigger(false);b.trigger(false);
 memset(first,0,4096*sizeof(float));memset(second,0,4096*sizeof(float));
 for(int pos=0;pos<4096;pos+=32) {
   if(pos==1024){p.op[2].wave=4.7f;p.op[4].level=.02f;}
   a.control(p,17);b.control(p,17);
   a.render(first+pos,32,1);b.render(second+pos,7,1);b.render(second+pos+7,25,1);
 }
 TEST_ASSERT_EQUAL_MEMORY(first,second,4096*sizeof(float));
}
void test_pm_pair_preserves_partition_feedback_wave_noise_and_reset() {
 FmParams p;p.hz=261.55f;p.oneShot=false;p.sat=.4f;p.outputGain=.2f;
 p.noise=.01f;p.noise2=.02f;p.noise2FilterHz=6000;p.noise2FilterMode=Svf::Mode::Hp;
 p.op[0].level=1;p.op[0].mod=4;p.op[0].wave=4;
 p.op[4].level=1.2f;p.op[4].ratio=4.4f;p.op[4].output=false;
 FmVoice a,b;a.trigger(false);b.trigger(false);
 memset(first,0,4096*sizeof(float));memset(second,0,4096*sizeof(float));
 for(int pos=0;pos<4096;pos+=32) {
   if(pos==1024){p.op[0].wave=4.7f;p.op[0].fb=.2f;p.op[4].fb=2;}
   if(pos==2048){a.trigger(true);b.trigger(true);}
   if(pos==2560)p.op[4].wave=.1f; // unsupported M shape must stay on the generic graph
   if(pos==2816)p.op[4].wave=0;
   if(pos==3072){p.resetPhase=false;a.trigger(true);b.trigger(true);}
   a.control(p,17);b.control(p,17);
   a.render(first+pos,32,1);b.render(second+pos,7,1);b.render(second+pos+7,25,1);
 }
 TEST_ASSERT_EQUAL_MEMORY(first,second,4096*sizeof(float));
}
void test_noise_pulse_decay_does_not_change_with_repeat_interval() {
 FmParams p;p.noise=1;p.noiseMs=10;p.bursts=3;p.burstMs=20;p.attackMs=0;
 FmVoice a,b;a.trigger(false);b.trigger(false);
 run(a,p,first,512);p.burstMs=100;run(b,p,second,512);
 // Both fragments are inside the first pulse: spacing must not stretch that pulse.
 TEST_ASSERT_EQUAL_MEMORY(first,second,512*sizeof(float));
}
int main(){UNITY_BEGIN();RUN_TEST(test_noise_pulse_decay_does_not_change_with_repeat_interval);RUN_TEST(test_pm_pair_preserves_partition_feedback_wave_noise_and_reset);RUN_TEST(test_additive_kernel_preserves_arbitrary_block_partition);RUN_TEST(test_retrigger_reset_phase_remains_effective_while_choking);RUN_TEST(test_feedback_control_starts_at_previous_value);RUN_TEST(test_wave_control_starts_at_previous_value);RUN_TEST(test_over_compensation_changes_level_after_distortion);RUN_TEST(test_post_gain_preserves_the_overdrive_operating_point);RUN_TEST(test_phase_preserving_retrigger_does_not_restart_attack_at_zero);RUN_TEST(test_wave_morph_keeps_the_carrier_fundamental);RUN_TEST(test_unconfigured_voice_is_silent);RUN_TEST(test_output_flags_prevent_modulator_leak);
RUN_TEST(test_invalid_cycle_route_is_ignored);RUN_TEST(test_independent_second_noise_branch_can_sound_alone);
RUN_TEST(test_second_noise_envelope_does_not_share_first_decay);RUN_TEST(test_one_shot_completion_and_sustain);
RUN_TEST(test_over_increases_level_and_remains_bounded);RUN_TEST(test_nonfinite_control_values_do_not_poison_output);return UNITY_END();}
