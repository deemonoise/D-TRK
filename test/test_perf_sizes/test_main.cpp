#include <unity.h>
#include <limits.h>
#include <initializer_list>
#include <string.h>
#include "perf_fx.h"
#include "perf_axis.h"
using namespace mt;
void setUp(){} void tearDown(){}
void test_no_triplets_in_any_raw_value_or_xy_position(){
 for(PerfFx fx:{PerfFx::Repeat,PerfFx::Gate}){
  for(int depth=-128;depth<=127;++depth){char text[20];perfValue(fx,depth,text,sizeof(text));TEST_ASSERT_NULL(strchr(text,'T'));}
  for(int pos=0;pos<480;++pos){char text[20];perfValue(fx,perfAxisDepth(fx,pos,480),text,sizeof(text));TEST_ASSERT_NULL(strchr(text,'T'));}
 }
}
void test_old_straight_indices_keep_exact_period(){
 const int repeat[]={0,1,2,4,6};const float repeatSteps[]={8,4,2,1,.5f};
 const int gate[]={0,1,3,5};const float gateSteps[]={4,2,1,.5f};
 for(int i=0;i<5;++i)TEST_ASSERT_EQUAL_FLOAT(repeatSteps[i],perfRepeatSteps(repeat[i]));
 for(int i=0;i<4;++i)TEST_ASSERT_EQUAL_FLOAT(gateSteps[i],perfGateSteps(gate[i]));
 TEST_ASSERT_EQUAL(4,perfRange(PerfFx::Repeat).def);TEST_ASSERT_EQUAL(3,perfRange(PerfFx::Gate).def);
}
void test_visible_steps_clamp_skip_triplets_and_emit_matching_messages(){
 for(PerfFx fx:{PerfFx::Repeat,PerfFx::Gate}){
  PerfStack ui,dsp;ui.press(fx);dsp.press(fx);
  for(int i=0;i<perfDepthCount(fx);++i){PerfMsg m[2];int raw=perfDepthAt(fx,i);
   int n=ui.setDepth(fx,raw,m);for(int j=0;j<n;++j){if(m[j].op==0)dsp.press((PerfFx)m[j].val);else dsp.turn((int8_t)m[j].val);}
   TEST_ASSERT_EQUAL(raw,ui.depth(fx));TEST_ASSERT_EQUAL(raw,dsp.depth(fx));
   if(i+1<perfDepthCount(fx))TEST_ASSERT_EQUAL(perfDepthAt(fx,i+1),perfDepthStep(fx,raw,1));
  }
  TEST_ASSERT_EQUAL(perfRange(fx).hi,perfDepthStep(fx,INT_MIN,INT_MAX));
  TEST_ASSERT_EQUAL(perfRange(fx).lo,perfDepthStep(fx,INT_MAX,INT_MIN));
  ui.turn(INT_MIN);TEST_ASSERT_EQUAL(perfRange(fx).lo,ui.depth(fx));
  ui.turn(INT_MAX);TEST_ASSERT_EQUAL(perfRange(fx).hi,ui.depth(fx));
  TEST_ASSERT_EQUAL(perfRange(fx).hi,perfDepthStep(fx,perfRange(fx).lo,10));
  for(int pixel=0;pixel<480;++pixel){int raw=perfAxisDepth(fx,pixel,480);TEST_ASSERT_EQUAL(raw,perfDepthNormalize(fx,raw));}
 }
 TEST_ASSERT_EQUAL_FLOAT(1,perfRepeatSteps(3));TEST_ASSERT_EQUAL_FLOAT(.5f,perfRepeatSteps(5));
 TEST_ASSERT_EQUAL_FLOAT(1,perfGateSteps(2));TEST_ASSERT_EQUAL_FLOAT(.5f,perfGateSteps(4));
 TEST_ASSERT_EQUAL(0,perfDepthNormalize(PerfFx::None,INT_MAX));
 TEST_ASSERT_EQUAL(-64,perfDepthStep(PerfFx::Filter,0,INT_MIN));
 TEST_ASSERT_EQUAL(63,perfDepthStep(PerfFx::Filter,0,INT_MAX));
 TEST_ASSERT_EQUAL(6,perfAxisDepth(PerfFx::Repeat,INT_MAX,INT_MAX));
 TEST_ASSERT_EQUAL(0,perfAxisEdge(PerfFx::Repeat,INT_MIN,INT_MAX));
 TEST_ASSERT_EQUAL(INT_MAX,perfAxisEdge(PerfFx::Repeat,INT_MAX,INT_MAX));
}
int main(){UNITY_BEGIN();RUN_TEST(test_visible_steps_clamp_skip_triplets_and_emit_matching_messages);RUN_TEST(test_no_triplets_in_any_raw_value_or_xy_position);RUN_TEST(test_old_straight_indices_keep_exact_period);return UNITY_END();}
