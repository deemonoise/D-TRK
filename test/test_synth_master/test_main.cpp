#include <unity.h>
#include <cmath>
#include <initializer_list>
#include "synth_master.h"
#include "synth.h"
#include <cstring>
#include <memory>
void setUp() {}
void tearDown() {}
void test_master_clean_range_and_protection() {
  float previous=-1;
  for(int i=-3000;i<=3000;++i) {
    const float x=i*.001f,y=mt::masterLimit(x);
    TEST_ASSERT_TRUE(y>=previous && y>=-1 && y<=1);
    TEST_ASSERT_EQUAL_FLOAT(-y,mt::masterLimit(-x));previous=y;
    if(std::fabs(x)<=.8f)TEST_ASSERT_EQUAL_FLOAT(x,y);
  }
}
void test_master_has_continuous_slope_at_knee() {
  for(float x:{.8f,1.2f}) {
    const float e=.0001f,left=(mt::masterLimit(x)-mt::masterLimit(x-e))/e,right=(mt::masterLimit(x+e)-mt::masterLimit(x))/e;
    TEST_ASSERT_FLOAT_WITHIN(.003f,left,right);
  }
}
void test_sustained_sample_volume_change_is_ramped() {
  struct Bank : mt::SampleSource {
    int16_t data[1024];
    Bank(){for(auto& x:data)x=1000;}
    const int16_t* find(const char*,uint32_t& frames,uint32_t& rate) const override {frames=1024;rate=mt::kSynthRate;return data;}
  } bank;
  auto p=std::make_unique<mt::Project>();
  auto& m=p->instruments[0];mt::instrSetType(m,mt::InstrType::Sample);
  strcpy(m.sample,"s");m.root=60;m.loop=1;m.vol=127;m.attack=0;m.decay=0;m.sustain=127;
  p->tracks[0].out=mt::TrackOut::Int;p->tracks[0].vol=127;
  auto synth=std::make_unique<mt::Synth>(*p);synth->setBank(&bank);
  int16_t out[mt::Synth::kBlock];const uint8_t note[]={0x90,60,127};synth->event(0,0,note,3);
  for(int i=0;i<8;++i)synth->render(out);
  const int before=out[mt::Synth::kBlock-1];m.vol=0;synth->render(out);
  TEST_ASSERT_TRUE(before>1000);TEST_ASSERT_INT_WITHIN(1,before,out[0]);
  for(int i=1;i<32;++i){TEST_ASSERT_TRUE(out[i]<out[i-1]);TEST_ASSERT_TRUE(out[i-1]-out[i]<60);}
  TEST_ASSERT_EQUAL_INT(0,out[32]);
}
int main(){UNITY_BEGIN();RUN_TEST(test_master_clean_range_and_protection);RUN_TEST(test_master_has_continuous_slope_at_knee);RUN_TEST(test_sustained_sample_volume_change_is_ramped);return UNITY_END();}
