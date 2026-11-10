#include <cmath>
#include <cstring>
#include <unity.h>
#define private public
#include "synth.h"
#undef private
using namespace mt;
static SynthModel model;
static Synth* synth;
constexpr int64_t one=int64_t(1)<<32;
static int16_t data[]={1000,1000,1000,-1000,5000,6000,7000,8000};
void setUp(){synth=new Synth(model);}
void tearDown(){delete synth;}
static Voice voice(LoopMode mode,int lo,int hi,double pos,double step) {
  Voice v;v.smp=data;v.smpLen=8;v.from=0;v.to=8;v.loopLo=lo;v.loopHi=hi;v.loopMode=uint8_t(mode);
  v.pos=int64_t(pos*one);v.step=int64_t(fabs(step)*one);v.dir=step>=0?1:-1;v.amp=8192;
  v.env.set(0,0,1,0);v.env.gate(true);return v;
}
void test_forward_seam_interpolates_loop_end_to_start(){auto v=voice(LoopMode::Forward,1,3,2.5,.5);float out=0;synth->renderSample(v,&out,1);TEST_ASSERT_FLOAT_WITHIN(.001f,1000,out);}
void test_pingpong_retains_multiple_reflections(){auto v=voice(LoopMode::PingPong,0,3,0,5);float out=0;synth->renderSample(v,&out,1);TEST_ASSERT_EQUAL_INT64(one,v.pos);TEST_ASSERT_EQUAL_INT(1,v.dir);}
void test_pingpong_reflects_before_fractional_read_outside_region(){auto v=voice(LoopMode::PingPong,0,3,1.75,.5);float out=0;synth->renderSample(v,&out,1);TEST_ASSERT_EQUAL_INT64(int64_t(1.75*one),v.pos);TEST_ASSERT_EQUAL_INT(-1,v.dir);}
int main(){UNITY_BEGIN();RUN_TEST(test_forward_seam_interpolates_loop_end_to_start);RUN_TEST(test_pingpong_retains_multiple_reflections);RUN_TEST(test_pingpong_reflects_before_fractional_read_outside_region);return UNITY_END();}
