#include "synth.h"
#include <cstdio>
#include <unity.h>
#include <cmath>
#include <cstring>
#include <memory>
using namespace mt;
struct Bank final:SampleSource{int16_t data[44100];bool find(const char*,SampleData& o)const override{o=SampleData{};o.key=1;o.frames=44100;o.rate=44100;o.whole=data;return true;}};
static double run(float hz,float measure,uint8_t note=72){
 auto p=std::make_unique<Project>();auto bank=std::make_unique<Bank>();for(int i=0;i<44100;i++)bank->data[i]=int16_t(1000*sin(6.283185307179586*hz*i/44100));
 auto& m=p->instruments[0];instrSetType(m,InstrType::Sample);strcpy(m.sample,"s");m.root=60;m.loop=1;m.vol=127;m.attack=0;m.decay=0;m.sustain=127;m.release=0;p->tracks[0].out=TrackOut::Int;p->tracks[0].vol=127;
 auto s=std::make_unique<Synth>(*p);s->setBank(bank.get());float tl[128],tr[128];RenderTaps taps;taps.mask=1;taps.l[0]=tl;taps.r[0]=tr;s->setTaps(&taps);int16_t lr[256];const uint8_t msg[]={0x90,note,127};s->event(0,0,msg,3);
 double a=0,b=0;int at=0;for(int k=0;at<44100;k++){s->renderStereo(lr);if(k<100)continue;for(int i=0;i<128&&at<44100;i++,at++){double ph=6.283185307179586*measure*at/44100;a+=tl[i]*sin(ph);b+=tl[i]*cos(ph);}}
 return 2*sqrt(a*a+b*b)/44100;
}

void setUp(){} void tearDown(){}
void test_public_sampler_rejects_octave_alias(){
 double base=run(1000,2000),alias=run(15000,14100);
 TEST_ASSERT_TRUE(base>.1);TEST_ASSERT_TRUE(20*log10(alias/base)<-35);
}
void test_public_sampler_rejects_alias_above_two_octaves(){
 for(int octave:{2,3,4}){
  const int ratio=1<<octave;
  const uint8_t note=static_cast<uint8_t>(60+12*octave);
  const double base=run(100,100*ratio,note),alias=run(40000.f/ratio,4100,note);
  TEST_ASSERT_TRUE(base>.1);TEST_ASSERT_TRUE(20*log10(alias/base)<-35);
 }
}
void test_sampler_reset_render_is_bit_exact_independent_of_cache_history(){
 auto p=std::make_unique<Project>();auto bank=std::make_unique<Bank>();
 for(int i=0;i<44100;++i)bank->data[i]=static_cast<int16_t>(12000*std::sin(i*1.7));
 auto&m=p->instruments[0];instrSetType(m,InstrType::Sample);strcpy(m.sample,"s");m.root=60;m.fine=7;m.loop=1;m.vol=127;m.attack=0;m.decay=0;m.sustain=127;
 p->tracks[0].out=TrackOut::Int;p->tracks[0].vol=127;
 auto s=std::make_unique<Synth>(*p);s->setBank(bank.get());uint32_t hashes[2];
 for(int pass=0;pass<2;++pass){s->reset();const uint8_t msg[]={0x90,67,127};s->event(0,0,msg,3);uint32_t h=2166136261u;int16_t l[128],r[128];
  for(int k=0;k<32;++k){s->render(l,r);for(int i=0;i<128;++i){h=(h^uint16_t(l[i]))*16777619u;h=(h^uint16_t(r[i]))*16777619u;}}hashes[pass]=h;
 }
 TEST_ASSERT_EQUAL_HEX32(hashes[0],hashes[1]);
}
int main(){UNITY_BEGIN();RUN_TEST(test_sampler_reset_render_is_bit_exact_independent_of_cache_history);RUN_TEST(test_public_sampler_rejects_octave_alias);RUN_TEST(test_public_sampler_rejects_alias_above_two_octaves);return UNITY_END();}
