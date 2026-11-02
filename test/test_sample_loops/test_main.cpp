#include <cmath>
#include <cstring>
#include <initializer_list>
#include <unity.h>
#define private public
#include "synth.h"
#undef private
using namespace mt;
static SynthModel model;
static Synth* synth;
static StreamPool pool;
static constexpr int64_t one=int64_t(1)<<32;
static int16_t data[]={1000,1000,1000,-1000,5000,6000,7000,8000};
static SampleHead heads[2];
void setUp(){synth=new Synth(model);synth->setPool(&pool);}
void tearDown(){delete synth;}
static Voice voice(LoopMode mode,int lo,int hi,double pos,double step){
 Voice v;v.smp=data;v.smpLen=8;v.from=0;v.to=8;v.loopLo=lo;v.loopHi=hi;v.loopMode=uint8_t(mode);
 v.pos=int64_t(pos*one);v.step=int64_t(fabs(step)*one);v.dir=step>=0?1:-1;v.amp=8192;v.env.hold(1);v.ug=1;
 heads[0]={0,3,data};heads[1]={3,5,data+3};v.heads=heads;v.nHeads=2;return v;
}
static float render(Voice& v,bool stream=false){float out=0;if(stream)synth->renderStream(v,&out,1);else synth->renderSample(v,&out,1);return out;}
void test_forward_loop_interpolates_last_to_first(){for(bool stream:{false,true}){auto v=voice(LoopMode::Forward,1,3,2.5,.5);TEST_ASSERT_FLOAT_WITHIN(.001f,1000,render(v,stream));}}
void test_pingpong_retains_multiple_reflections(){for(bool stream:{false,true}){auto v=voice(LoopMode::PingPong,0,3,0,5);render(v,stream);TEST_ASSERT_EQUAL_INT64(one,v.pos);TEST_ASSERT_EQUAL_INT(1,v.dir);}}
void test_pingpong_reflects_fractional_upper_edge(){for(bool stream:{false,true}){auto v=voice(LoopMode::PingPong,0,3,1.75,.5);render(v,stream);TEST_ASSERT_EQUAL_INT64(int64_t(1.75*one),v.pos);TEST_ASSERT_EQUAL_INT(-1,v.dir);}}

void test_reverse_suffix_and_loop_seam_are_distinct() {
 for (bool stream : {false,true}) {
  auto v=voice(LoopMode::Forward,1,3,3.5,-.5);
  TEST_ASSERT_FLOAT_WITHIN(.001f,2000,render(v,stream)); // ordinary suffix: (-1000+5000)/2
  TEST_ASSERT_FLOAT_WITHIN(.001f,-1000,render(v,stream));
  TEST_ASSERT_FLOAT_WITHIN(.001f,0,render(v,stream)); // entering loop: (1000-1000)/2
  for(int n=0;n<4;++n)render(v,stream);
  TEST_ASSERT_TRUE(v.smpLooped);
  v.pos=int64_t(2.5*one);
  TEST_ASSERT_FLOAT_WITHIN(.001f,1000,render(v,stream)); // wrapped seam: (1000+1000)/2
 }
}
void test_initial_prefix_and_off_keep_file_interpolation() {
 for(bool stream:{false,true}) {
  auto prefix=voice(LoopMode::Forward,4,7,2.5,.5);
  TEST_ASSERT_FLOAT_WITHIN(.001f,0,render(prefix,stream));
  TEST_ASSERT_FALSE(prefix.smpLooped);
  auto off=voice(LoopMode::Off,1,3,2.5,.5);
  TEST_ASSERT_FLOAT_WITHIN(.001f,0,render(off,stream));
  TEST_ASSERT_TRUE(off.env.idle());
 }
}
static void reflectReference(int64_t& p,int64_t& d,int64_t lo,int64_t top) {
 if(top==lo){p=lo;if(d<0)d=-d;return;}
 while(p<lo||p>top){p=p>top?2*top-p:2*lo-p;d=-d;}
 if((p==lo&&d<0)||(p==top&&d>0))d=-d;
}
void test_pingpong_matches_reference_grid() {
 for(int length=1;length<=6;++length)for(double speed:{.25,1.,5.,16.75,256.})for(int dir:{-1,1}) {
  auto v=voice(LoopMode::PingPong,1,1+length,1,dir*speed);
  int64_t p=v.pos,d=dir*v.step;
  for(int i=0;i<32;++i){render(v);p+=d;reflectReference(p,d,one,int64_t(length)*one);TEST_ASSERT_EQUAL_INT64(p,v.pos);TEST_ASSERT_EQUAL_INT(d>=0?1:-1,v.dir);}
 }
}
void test_ram_stream_partition_and_single_frame_parity() {
 for(auto mode:{LoopMode::Forward,LoopMode::PingPong,LoopMode::Off})for(int dir:{-1,1})for(double speed:{.25,5.,256.})for(int length:{1,4}) {
  auto a=voice(mode,1,1+length,dir>0?0:7,dir*speed),b=a,c=a;
  float ram[32]={},stream[32]={},parts[32]={};
  synth->renderSample(a,ram,32);synth->renderStream(b,stream,32);
  synth->renderSample(c,parts,7);if(!c.env.idle())synth->renderSample(c,parts+7,25);
  TEST_ASSERT_EQUAL_MEMORY(ram,stream,sizeof ram);TEST_ASSERT_EQUAL_MEMORY(ram,parts,sizeof ram);
  TEST_ASSERT_EQUAL_INT64(a.pos,b.pos);TEST_ASSERT_EQUAL_INT64(a.pos,c.pos);TEST_ASSERT_EQUAL_INT(a.dir,b.dir);TEST_ASSERT_EQUAL_INT(a.dir,c.dir);
 }
}
void test_stream_underrun_counts_once_and_fades() {
 auto v=voice(LoopMode::Forward,1,3,1,.25);v.nHeads=0;v.hold=1000;pool.underruns=0;
 float out[32]={};synth->renderStream(v,out,32);TEST_ASSERT_EQUAL(1,pool.underruns);TEST_ASSERT_TRUE(v.under);TEST_ASSERT_TRUE(v.ug<1);
 synth->renderStream(v,out,32);TEST_ASSERT_EQUAL(1,pool.underruns);
 v.heads=heads;v.nHeads=2;synth->renderStream(v,out,32);TEST_ASSERT_FALSE(v.under);
}
int main(){UNITY_BEGIN();
 RUN_TEST(test_forward_loop_interpolates_last_to_first);
 RUN_TEST(test_pingpong_retains_multiple_reflections);
 RUN_TEST(test_pingpong_reflects_fractional_upper_edge);
 RUN_TEST(test_reverse_suffix_and_loop_seam_are_distinct);
 RUN_TEST(test_initial_prefix_and_off_keep_file_interpolation);
 RUN_TEST(test_pingpong_matches_reference_grid);
 RUN_TEST(test_ram_stream_partition_and_single_frame_parity);
 RUN_TEST(test_stream_underrun_counts_once_and_fades);
 return UNITY_END();}
