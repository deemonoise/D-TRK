#include <unity.h>
#include <math.h>
#include "wt_audition.h"
#include "synth_osc.h"
void setUp() {} void tearDown() {}
int16_t wave[256], stage[256], input[256], output[512]; float scratch[256];
void test_session() {
  mt::WtAudition a; a.bind(wave,stage);
  for(int i=0;i<256;++i) input[i]=static_cast<int16_t>(18000*sin(6.28318530718*i/256));
  TEST_ASSERT_FALSE(a.part(7,1,input+128,0,scratch)); TEST_ASSERT_FALSE(a.playing());
  TEST_ASSERT_TRUE(a.part(7,0,input,0,scratch));
  TEST_ASSERT_FALSE(a.part(8,1,input+128,1,scratch)); TEST_ASSERT_FALSE(a.playing());
  TEST_ASSERT_TRUE(a.part(7,0,input,0,scratch));
  TEST_ASSERT_FALSE(a.part(7,1,input+128,1001,scratch));
  TEST_ASSERT_TRUE(a.part(7,0,input,2000,scratch));
  TEST_ASSERT_TRUE(a.part(7,1,input+128,2001,scratch)); TEST_ASSERT_TRUE(a.playing());
  unsigned total=a.render(output,512);
  TEST_ASSERT_FALSE(a.part(7,1,input+128,2002,scratch));  // cannot restart/retrigger
  while(a.playing()) total+=a.render(output,512);
  TEST_ASSERT_EQUAL(mt::kSynthRate * 3 / 5,total); TEST_ASSERT_EQUAL(0,a.render(output,512));
  a.part(8,0,input,3000,scratch); a.stop(); TEST_ASSERT_FALSE(a.staging());
  TEST_ASSERT_FALSE(a.part(8,1,input+128,3001,scratch));
}
void test_stop_and_wrapped_timeout() {
  mt::WtAudition a; a.bind(wave,stage);
  a.part(1,0,input,UINT32_MAX-500,scratch);
  TEST_ASSERT_FALSE(a.part(1,1,input+128,501,scratch));
  a.part(1,0,input,1000,scratch); a.part(1,1,input+128,1001,scratch);
  a.stop(); TEST_ASSERT_FALSE(a.playing()); TEST_ASSERT_EQUAL(0,a.render(output,512));
}
void test_bandlimit() {
  for(int i=0;i<256;++i) input[i]=static_cast<int16_t>(2000+10000*sin(6.28318530718*i/256)+5000*sin(6.28318530718*70*i/256)+(i%2 ? -4000 : 4000));
  mt::wtAuditionPrepare(input,wave,scratch);
  for(int i=0;i<256;++i) TEST_ASSERT_INT_WITHIN(3,static_cast<int>(10000*sin(6.28318530718*i/256)),wave[i]);
}
int main() { UNITY_BEGIN(); RUN_TEST(test_session); RUN_TEST(test_bandlimit); RUN_TEST(test_stop_and_wrapped_timeout); return UNITY_END(); }
