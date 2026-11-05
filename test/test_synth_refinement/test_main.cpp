#include <unity.h>
#include <cmath>
#include <algorithm>
#define private public
#include "synth.h"
#include "synth_syn_calibration.h"
#undef private
using namespace mt;
static SynthModel model;
static Synth* synth;
void setUp(){model=SynthModel();synth=nullptr;}
void tearDown(){delete synth;}
static Voice& voice(){
 auto& m=model.instruments[0];instrSetType(m,InstrType::Synth);m.synOsc[0]=0;m.macro[kMacMix]=0;
 m.attack=m.decay=m.release=0;m.sustain=127;m.fltMode=uint8_t(FltMode::Lp);m.cutoff=48;m.reso=0;m.fenv=0;m.keytrack=0;m.velCut=0;
 model.tracks[0].out=TrackOut::Int;model.tracks[0].instr=0;synth=new Synth(model);synth->noteOn(0,60,100);
 for(auto& v:synth->voices_)if(v.on)return v;
 TEST_FAIL_MESSAGE("no voice");return synth->voices_[0];
}
static float cutoffAt(Voice& v,uint32_t t){
 float la[static_cast<int>(LfoDest::Count)]={};v.fenvT=t;v.fenvDone=false;synth->controlFilter(v,model.instruments[0],60,la);return v.octHz;
}
void test_filter_envelope_matches_measured_depth_and_decay(){
 auto& v=voice();auto& m=model.instruments[0];const float base=v.octHz;m.fenv=16;m.fDec=64;
 const float peak=cutoffAt(v,0);
 TEST_ASSERT_FLOAT_WITHIN(.04f,1.237f,std::log2(peak/base));
 const float after=cutoffAt(v,uint32_t(.3492f*kSynthRate));
 TEST_ASSERT_FLOAT_WITHIN(.025f,std::exp(-1.f),std::log(after/base)/std::log(peak/base));
}
void test_filter_attack_has_measured_seven_hundred_ms_scale(){
 auto& v=voice();auto& m=model.instruments[0];const float base=v.octHz;m.fenv=16;m.fAtk=64;m.fDec=64;
 const float half=cutoffAt(v,uint32_t(.35f*kSynthRate));
 TEST_ASSERT_FLOAT_WITHIN(.04f,.5f,std::log2(half/base)/1.237f);
}
void test_zero_decay_remains_explicit_hold(){
 auto& v=voice();auto& m=model.instruments[0];m.fenv=16;m.fDec=0;
 const float peak=cutoffAt(v,0);TEST_ASSERT_FLOAT_WITHIN(.001f,peak,cutoffAt(v,kSynthRate*2));
}
static float samples[32768];
static double amplitude(int harmonic){
 double re=0,im=0;
 for(int n=0;n<32768;++n){const double a=6.283185307179586*220*harmonic*n/kSynthRate,w=.5-.5*cos(6.283185307179586*n/32767);re+=samples[n]*w*cos(a);im+=samples[n]*w*sin(a);}
 return sqrt(re*re+im*im);
}
void test_pulse_width_matches_measured_three_quarter_duty(){
 SynVoice voice;SynParams p;p.mode[0]=uint8_t(SynOsc::Square);p.hz[0]=220;p.shape[0]=64.f/127;
 std::fill(samples,samples+32768,0.f);voice.trigger();voice.control(p,32);voice.render(samples,32768,1);
 TEST_ASSERT_FLOAT_WITHIN(.008f,.713f,amplitude(2)/amplitude(1));
}
void test_pulse_endpoint_becomes_silent_after_dc_removal(){
 SynVoice voice;SynParams p;p.mode[0]=uint8_t(SynOsc::Square);p.hz[0]=220;p.shape[0]=1;
 std::fill(samples,samples+32768,0.f);voice.trigger();voice.control(p,32);voice.render(samples,32768,1);
 double rms=0;for(int n=1024;n<32768;++n)rms+=samples[n]*samples[n];
 TEST_ASSERT_LESS_THAN_FLOAT(.0001f,sqrt(rms/(32768-1024)));
}
void test_lp_high_resonance_matches_measured_ring_damping(){
 TEST_ASSERT_FLOAT_WITHIN(5.f,101.f,synLpQ(112));
 TEST_ASSERT_FLOAT_WITHIN(10.f,233.f,synLpQ(127));
}
static float heldTone(bool filter,uint8_t velocity=100){
 model=SynthModel();auto& m=model.instruments[0];instrSetType(m,InstrType::Synth);
 m.synOsc[0]=uint8_t(SynOsc::Sine);m.macro[kMacMix]=0;m.attack=m.decay=m.release=0;m.sustain=127;
 m.fltMode=filter?uint8_t(FltMode::Lp):0;m.cutoff=64;m.reso=112;m.fenv=m.keytrack=m.velCut=0;
 m.vol=40;model.masterVol=50;model.compAmt=0;model.dlyLevel=model.rvbLevel=model.modLevel=0;
 model.tracks[0].out=TrackOut::Int;model.tracks[0].instr=0;
 Synth s(model);const uint8_t note[]={0x90,79,velocity};s.event(0,0,note,3);
 int16_t l[Synth::kBlock],r[Synth::kBlock];double energy=0;int count=0;
 for(int b=0;b<kSynthRate/Synth::kBlock;++b){s.render(l,r);if(b>kSynthRate/(2*Synth::kBlock))for(int n=0;n<Synth::kBlock;++n){float v=(l[n]+r[n])*(.5f/32768.f);energy+=v*v;++count;}}
 return sqrt(energy/count);
}
void test_resonant_sine_has_measured_bounded_gain(){
 const float gain=heldTone(true)/heldTone(false);
 TEST_ASSERT_TRUE(gain>5.f && gain<6.2f);
}
void test_amplitude_velocity_follows_resonant_filter(){
 const float quiet=heldTone(true,50),loud=heldTone(true,100);
 TEST_ASSERT_FLOAT_WITHIN(.06f,2.f,loud/quiet);
}
void test_nonlinear_lp_reserves_additional_cpu_only_when_reachable(){
 Instrument m;instrSetType(m,InstrType::Synth);m.synOsc[0]=uint8_t(SynOsc::HyperSaw);m.fltMode=uint8_t(FltMode::Lp);m.reso=64;
 const int plain=voiceCost(m,false);TEST_ASSERT_TRUE(16*plain<=kVoiceBudgetTeensy);
 m.reso=112;const int high=voiceCost(m,false);TEST_ASSERT_TRUE(high>plain);
 m.reso=0;m.lfoDest=uint8_t(LfoDest::Reso);m.lfoDepth=127;TEST_ASSERT_EQUAL(high,voiceCost(m,false));
 m.lfoDest=uint8_t(LfoDest::Cutoff);TEST_ASSERT_EQUAL(plain,voiceCost(m,false));
 m.type=InstrType::Chip;m.reso=112;TEST_ASSERT_EQUAL(kCostChip,voiceCost(m,false));
}
int main(){UNITY_BEGIN();RUN_TEST(test_nonlinear_lp_reserves_additional_cpu_only_when_reachable);RUN_TEST(test_lp_high_resonance_matches_measured_ring_damping);RUN_TEST(test_resonant_sine_has_measured_bounded_gain);RUN_TEST(test_amplitude_velocity_follows_resonant_filter);RUN_TEST(test_filter_envelope_matches_measured_depth_and_decay);RUN_TEST(test_filter_attack_has_measured_seven_hundred_ms_scale);RUN_TEST(test_zero_decay_remains_explicit_hold);RUN_TEST(test_pulse_width_matches_measured_three_quarter_duty);RUN_TEST(test_pulse_endpoint_becomes_silent_after_dc_removal);return UNITY_END();}
