#include <cmath>
#include <algorithm>
#include <cstring>
#include <memory>
#include <unity.h>
#include "synth.h"
#include "synth_gain.h"
#include "synth_master.h"
using namespace mt;
namespace {
struct Bank final : SampleSource {
  int16_t data[44100] = {};
  bool find(const char*, SampleData& o) const override {
    o=SampleData{};o.key=1;o.frames=44100;o.rate=kSynthRate;o.whole=data;return true;
  }
};
std::unique_ptr<Project> p;
std::unique_ptr<Bank> bank;
std::unique_ptr<Synth> synth;
int16_t l[Synth::kBlock],r[Synth::kBlock];
void start() { const uint8_t m[]={0x90,60,127};TEST_ASSERT_TRUE(synth->event(0,0,m,3)); }
void settle() { for(int k=0;k<100;++k)synth->render(l,r); }
}
void setUp(){
 p=std::make_unique<Project>();bank=std::make_unique<Bank>();
 auto&m=p->instruments[0];instrSetType(m,InstrType::Sample);strcpy(m.sample,"s");m.root=60;m.loop=1;m.vol=127;m.attack=0;m.decay=0;m.sustain=127;m.release=0;
 p->tracks[0].out=TrackOut::Int;p->tracks[0].vol=127;p->masterVol=100;
 synth=std::make_unique<Synth>(*p);synth->setBank(bank.get());
}
void tearDown(){synth.reset();bank.reset();p.reset();}
void test_master_has_clean_linear_range(){
 for(int i=0;i<44100;++i)bank->data[i]=static_cast<int16_t>(8000*std::sin(6.283185307179586*1000*i/kSynthRate));
 start();settle();double a1=0,b1=0,a3=0,b3=0;int at=0;
 while(at<44100){synth->render(l,r);for(int i=0;i<Synth::kBlock&&at<44100;++i,++at){double ph=6.283185307179586*1000*at/kSynthRate;a1+=l[i]*sin(ph);b1+=l[i]*cos(ph);a3+=l[i]*sin(3*ph);b3+=l[i]*cos(3*ph);}}
 TEST_ASSERT_TRUE(std::sqrt((a3*a3+b3*b3)/(a1*a1+b1*b1))<0.0002);
}
void test_main_fades_to_exact_silence(){
 for(auto&x:bank->data)x=5000;start();settle();const int before=l[Synth::kBlock-1];p->masterVol=0;int previous=before,maxStep=0;
 for(int k=0;k<3;++k){synth->render(l,r);for(int i=0;i<Synth::kBlock;++i){maxStep=std::max(maxStep,std::abs(l[i]-previous));TEST_ASSERT_EQUAL(l[i],r[i]);previous=l[i];}}
 TEST_ASSERT_TRUE(maxStep<before/100);TEST_ASSERT_EQUAL(0,previous);
}
void test_main_retarget_starts_at_current_level(){
 for(auto&x:bank->data)x=5000;start();settle();p->masterVol=0;synth->render(l,r);int before=l[127];p->masterVol=100;synth->render(l,r);
 TEST_ASSERT_TRUE(std::abs(l[0]-before)<100);TEST_ASSERT_TRUE(l[127]>before);
}
void test_taps_include_actual_linear_master_gain(){
 for(auto&x:bank->data)x=1000;
 float tl[128],tr[128];RenderTaps taps;taps.mask=1;taps.l[0]=tl;taps.r[0]=tr;synth->setTaps(&taps);
 start();settle();
 TEST_ASSERT_FLOAT_WITHIN(0.1f,tl[127]*taps.gain*32767.f,synth->masterL()[127]);
}
void test_gain_ramp_exact_endpoint_and_repeated_target(){
 GainRamp g;g.set(1.f);TEST_ASSERT_EQUAL_FLOAT(1.f,g.next());g.set(0.f);
 for(int i=0;i<GainRamp::kSamples-1;++i){g.set(0.f);TEST_ASSERT_TRUE(g.next()>0.f);}
 TEST_ASSERT_EQUAL_FLOAT(0.f,g.next());TEST_ASSERT_FALSE(g.moving());
 g.set(1.f);for(int i=0;i<70;++i)g.next();const float old=g.current();g.set(.5f);
 TEST_ASSERT_TRUE(std::fabs(g.next()-old)<.01f);
 for(int i=1;i<GainRamp::kSamples;++i)g.next();TEST_ASSERT_EQUAL_FLOAT(.5f,g.current());
 g.reset();g.set(.25f);TEST_ASSERT_EQUAL_FLOAT(.25f,g.next());
}
void test_master_limit_is_odd_monotonic_bounded_and_c1(){
 float previous=-1;
 for(int i=-3000;i<=3000;++i){float x=i*.001f,y=masterLimit(x);TEST_ASSERT_TRUE(y>=previous && y>=-1 && y<=1);TEST_ASSERT_EQUAL_FLOAT(-y,masterLimit(-x));previous=y;if(std::fabs(x)<=.8f)TEST_ASSERT_EQUAL_FLOAT(x,y);}
 for(float x:{.8f,1.2f}){const float e=.0001f;const float left=(masterLimit(x)-masterLimit(x-e))/e,right=(masterLimit(x+e)-masterLimit(x))/e;TEST_ASSERT_FLOAT_WITHIN(.003f,left,right);}
}
void test_taps_follow_main_ramp_per_sample(){
 for(auto&x:bank->data)x=1000;float tl[128],tr[128];RenderTaps taps;taps.mask=1;taps.l[0]=tl;taps.r[0]=tr;synth->setTaps(&taps);start();settle();p->masterVol=0;synth->render(l,r);
 TEST_ASSERT_TRUE(taps.sampleGain[0]>taps.sampleGain[127]);
 for(int i=0;i<128;++i)TEST_ASSERT_FLOAT_WITHIN(.001f,tl[i]*taps.sampleGain[i]*32767.f,synth->masterL()[i]);
}
void test_track_meter_uses_same_linear_main100_calibration(){
 for(auto&x:bank->data)x=1000;synth->setMeters(true);start();settle();float peaks[kTracks];synth->takeTrackPeaks(peaks);
 TEST_ASSERT_FLOAT_WITHIN(.001f,synth->masterL()[127]/32767.f,peaks[0]);
}
void test_sample_budget_prices_the_initial_resampling_rate(){
 for(auto&x:bank->data)x=1000;synth->setVoiceBudget(10000);const uint8_t msg[]={0x90,72,127};synth->event(0,0,msg,3);synth->render(l,r);
 const Voice& v=synth->voice(synth->trackVoice(0));TEST_ASSERT_EQUAL(samplePlaybackCost(v.inc),v.cost);
}
void test_sample_budget_prices_the_current_last_sample_pitch_change(){
 for(auto&x:bank->data)x=1000;synth->setVoiceBudget(10000);start();settle();
 const uint8_t bend[]={0xe0,127,127};synth->event(127,0,bend,3);synth->render(l,r);
 const Voice& v=synth->voice(synth->trackVoice(0));TEST_ASSERT_TRUE(v.inc>1.f);TEST_ASSERT_EQUAL(samplePlaybackCost(v.inc),v.cost);
}
int main(){UNITY_BEGIN();RUN_TEST(test_master_has_clean_linear_range);RUN_TEST(test_main_fades_to_exact_silence);RUN_TEST(test_main_retarget_starts_at_current_level);RUN_TEST(test_taps_include_actual_linear_master_gain);RUN_TEST(test_gain_ramp_exact_endpoint_and_repeated_target);RUN_TEST(test_master_limit_is_odd_monotonic_bounded_and_c1);RUN_TEST(test_taps_follow_main_ramp_per_sample);RUN_TEST(test_track_meter_uses_same_linear_main100_calibration);RUN_TEST(test_sample_budget_prices_the_initial_resampling_rate);RUN_TEST(test_sample_budget_prices_the_current_last_sample_pitch_change);return UNITY_END();}
