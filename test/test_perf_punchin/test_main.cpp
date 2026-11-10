#include <unity.h>
#include <cmath>
#include "synth_perf.h"
using namespace mt;
static int16_t ring[64000*2];
static float out[10000];
void setUp() {}
void tearDown() {}
static void render(PerfBus& bus,int from,int to,bool dc=false){
 float l[100],r[100];
 for(int at=from;at<to;at+=100){int n=to-at<100?to-at:100;
  for(int i=0;i<n;++i)l[i]=r[i]=dc?.5f:(at+i)*.0001f;
  bus.process(l,r,n,1000,true);for(int i=0;i<n;++i)out[at+i]=l[i];
 }
}
void test_repeat_punches_an_already_recorded_complete_slice(){
 PerfBus bus;bus.setBuffer(ring,64000);render(bus,0,3500);bus.press(PerfFx::Repeat);render(bus,3500,6500);
 TEST_ASSERT_FLOAT_WITHIN(.001f,.21f,out[3600]);
 TEST_ASSERT_FLOAT_WITHIN(.001f,out[3600],out[4600]);
 bus.releaseAll();render(bus,6500,6800);TEST_ASSERT_FLOAT_WITHIN(.00001f,.67f,out[6700]);
}
void test_roll_starts_with_a_complete_recorded_slice(){
 PerfBus bus;bus.setBuffer(ring,64000);render(bus,0,3500);bus.press(PerfFx::Roll);render(bus,3500,3800);
 TEST_ASSERT_FLOAT_WITHIN(.001f,.11f,out[3600]);
}
void test_scratch_has_rhythmic_crossfader_strokes(){
 PerfBus bus;bus.setBuffer(ring,64000);render(bus,0,3500,true);bus.press(PerfFx::Scratch);render(bus,3500,8500,true);
 // Default 1/8 stroke lasts 2000 samples. Quick backwards stroke ends at T/4.
 TEST_ASSERT_TRUE(out[4000]<.3f);
 TEST_ASSERT_TRUE(out[3750]>.4f);
 TEST_ASSERT_TRUE(out[5500]<.3f);
 for(int i=3500;i<8500;++i){TEST_ASSERT_TRUE(std::isfinite(out[i]));TEST_ASSERT_TRUE(std::fabs(out[i])<=.501f);}
 bus.releaseAll();render(bus,8500,8800,true);TEST_ASSERT_EQUAL_FLOAT(.5f,out[8700]);
}
void test_six_bit_crush_keeps_quiet_transients(){
 PerfBus bus;bus.press(PerfFx::Crush);float l[128],r[128];
 for(int i=0;i<128;++i)l[i]=r[i]=.1f;
 bus.process(l,r,128,1000,true);
 TEST_ASSERT_TRUE(l[100]>0.f);TEST_ASSERT_FLOAT_WITHIN(.063f,.1f,l[100]);
}
void test_repeat_waits_for_a_complete_fresh_slice(){
 PerfBus bus;bus.setBuffer(ring,64000);bus.press(PerfFx::Repeat);render(bus,0,1600);
 for(int i=0;i<1000;++i)TEST_ASSERT_EQUAL_FLOAT(i*.0001f,out[i]);
 TEST_ASSERT_FLOAT_WITHIN(.001f,.01f,out[1100]);
}
void test_repeat_does_not_latch_a_shorter_size_during_startup(){
 PerfBus bus;bus.setBuffer(ring,64000);bus.press(PerfFx::Repeat);bus.turn(-2); // 1/4 = four steps
 render(bus,0,4700);
 for(int i=0;i<4000;++i)TEST_ASSERT_EQUAL_FLOAT(i*.0001f,out[i]);
 TEST_ASSERT_FLOAT_WITHIN(.001f,.01f,out[4100]);
}
int main(){UNITY_BEGIN();RUN_TEST(test_repeat_punches_an_already_recorded_complete_slice);RUN_TEST(test_roll_starts_with_a_complete_recorded_slice);RUN_TEST(test_scratch_has_rhythmic_crossfader_strokes);RUN_TEST(test_six_bit_crush_keeps_quiet_transients);RUN_TEST(test_repeat_waits_for_a_complete_fresh_slice);RUN_TEST(test_repeat_does_not_latch_a_shorter_size_during_startup);return UNITY_END();}
