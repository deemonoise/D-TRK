#include <cmath>
#include <cstdint>
#include <vector>
#include <unity.h>
#include "synth_resample.h"
#include "model.h"
using namespace mt;
void setUp() {}
void tearDown() {}
static constexpr double kPi = 3.14159265358979323846;
static double toneGain(double sourceHz, double step) {
  const auto p = sampleFirPlan(static_cast<int64_t>(step * 4294967296.));
  constexpr int n = 44100;
  double si = 0, co = 0;
  for (int i = 0; i < n; ++i) {
    const double pos = 64 + i * step;
    const int64_t frame = static_cast<int64_t>(pos);
    const uint32_t frac = static_cast<uint32_t>((pos - frame) * 4294967296.);
    float y = 0;
    const auto read = [&](int64_t f, float& out) { out = static_cast<float>(int16_t(16000*std::sin(2*kPi*sourceHz*f/44100))); return true; };
    TEST_ASSERT_TRUE(sampleFirRead(p, frac, frame, read, y));
    const double ph = 2*kPi*sourceHz*step*i/44100;
    si += y*std::sin(ph); co += y*std::cos(ph);
  }
  return 2*std::sqrt(si*si+co*co)/(n*16000.);
}
void test_octave_alias_rejected() {
  TEST_ASSERT_TRUE(20*std::log10(toneGain(15000,2)/toneGain(1000,2)) < -35);
}
void test_fractional_and_integer_rates_preserve_passband() {
  for(double step : {1.25,1.4983071,2.,3.,4.})
    TEST_ASSERT_FLOAT_WITHIN(.3f, 0.f, 20*std::log10(toneGain(1000,step)));
}
void test_stopbands_for_multiple_rates() {
  const double rate[] = {1.25,2.,3.,4.};
  const double hz[] = {21000,15000,13000,10000};
  for(int i=0;i<4;i++) TEST_ASSERT_TRUE(20*std::log10(toneGain(hz[i],rate[i])) < -35);
}
void test_unity_and_downpitch_keep_linear_path() {
  TEST_ASSERT_FALSE(sampleFirPlan(int64_t{1}<<32).active());
  TEST_ASSERT_FALSE(sampleFirPlan(int64_t{1}<<31).active());
  TEST_ASSERT_FALSE(sampleFirPlan(0).active());
  TEST_ASSERT_TRUE(sampleFirPlan((int64_t{1}<<32)+1).active());
}
void test_reverse_plan_matches_forward() {
  const auto a = sampleFirPlan(int64_t{3}<<32), b = sampleFirPlan(-(int64_t{3}<<32));
  std::vector<int16_t> data(a.taps()); for(int i=0;i<a.taps();i++)data[i]=i*i-100;
  for(uint32_t f : {0u,1u,0x12345678u,0xffffffffu})
    TEST_ASSERT_EQUAL_FLOAT(sampleFirContiguous(a,f,data.data()),sampleFirContiguous(b,f,data.data()));
}
void test_reader_matches_contiguous_and_preserves_dc() {
  const auto p = sampleFirPlan(int64_t{2}<<32);
  int16_t data[kSampleFirTaps]; for(int i=0;i<kSampleFirTaps;i++)data[i]=i*i-100;
  for(uint32_t f : {0u,1u,0x12345678u,0xffffffffu}) {
    float y=0;auto read=[&](int64_t i,float& out){out=data[i-kSampleFirFirst];return true;};
    TEST_ASSERT_TRUE(sampleFirRead(p,f,0,read,y));
    TEST_ASSERT_EQUAL_FLOAT(sampleFirContiguous(p,f,data),y);
  }
  for(double rate:{1.001,1.25,2.,3.,4.,16.,256.}) {
    float y=0;const auto q=sampleFirPlan(static_cast<int64_t>(rate*4294967296.));
    auto read=[](int64_t,float& out){out=1234;return true;};
    TEST_ASSERT_TRUE(sampleFirRead(q,0x76543210,0,read,y));
    TEST_ASSERT_FLOAT_WITHIN(.002f,1234.f,y);
  }
}
void test_missing_reader_does_not_publish_partial_result() {
  auto p=sampleFirPlan(int64_t{2}<<32);float y=987;
  auto read=[](int64_t f,float& out){out=1;return f!=0;};
  TEST_ASSERT_FALSE(sampleFirRead(p,0,0,read,y));TEST_ASSERT_EQUAL_FLOAT(987,y);
}
static uint32_t mapped(int64_t frame,uint8_t mode,bool looped=true,int64_t step=int64_t{2}<<32) {
  return sampleFirFrame(frame,2,18,6,12,mode,looped,step);
}
void test_region_bounds_and_loop_wraps() {
  const auto off=static_cast<uint8_t>(LoopMode::Off), loop=static_cast<uint8_t>(LoopMode::Forward);
  TEST_ASSERT_EQUAL(2,mapped(-100,off));TEST_ASSERT_EQUAL(17,mapped(100,off));
  TEST_ASSERT_EQUAL(11,mapped(5,loop));TEST_ASSERT_EQUAL(6,mapped(12,loop));
  TEST_ASSERT_EQUAL(3,mapped(3,loop,false));
  TEST_ASSERT_EQUAL(15,mapped(15,loop,false,-(int64_t{2}<<32)));
  TEST_ASSERT_EQUAL(11,mapped(5,loop,false,-(int64_t{2}<<32)));
  for(int i=-100;i<100;i++) {
    const auto f=mapped(i,loop);TEST_ASSERT_TRUE(f>=6&&f<12);
  }
}
void test_pingpong_mapping_reflects_without_duplicate_endpoint() {
  const auto ping=static_cast<uint8_t>(LoopMode::PingPong);
  TEST_ASSERT_EQUAL(7,mapped(5,ping));TEST_ASSERT_EQUAL(10,mapped(12,ping));
  TEST_ASSERT_EQUAL(6,mapped(6,ping));TEST_ASSERT_EQUAL(11,mapped(11,ping));
  for(int i=-100;i<100;i++) {const auto f=mapped(i,ping);TEST_ASSERT_TRUE(f>=6&&f<12);}
  for(int i=-100;i<100;i++)TEST_ASSERT_EQUAL(7,sampleFirFrame(i,7,8,7,8,ping,true,1));
}
void test_plan_band_transition_is_continuous() {
  int16_t data[512];for(int i=0;i<512;i++)data[i]=int16_t(10000*std::sin(i*.9));
  const double edge=std::pow(2.,9./8.);float y[2];
  for(int i=0;i<2;i++) {auto p=sampleFirPlan(static_cast<int64_t>((edge+(i?1:-1)*1e-6)*4294967296.));y[i]=sampleFirContiguous(p,0x12345678,data+256+p.first());}
  TEST_ASSERT_FLOAT_WITHIN(.05f,y[0],y[1]);
}
void test_interior_rejects_every_window_needing_edge_mapping() {
  for(int mode=0;mode<3;mode++)for(bool looped:{false,true})for(int sign:{-1,1})
    for(uint32_t center=3;center<60;center++) {
      bool ordinary=true;
      for(int j=kSampleFirFirst;j<=kSampleFirLast;j++)
        ordinary=ordinary&&sampleFirFrame(int64_t(center)+j,3,60,20,40,mode,looped,sign)==int64_t(center)+j;
      TEST_ASSERT_EQUAL(ordinary,sampleFirInterior(center,3,60,20,40,mode,looped,sign));
    }
}
static double response(double hz, double rate, uint32_t fraction=0) {
  const auto p=sampleFirPlan(static_cast<int64_t>(rate*4294967296.));
  float a=0,b=0;
  auto sine=[&](int64_t frame,float& x){x=int16_t(16000*std::sin(2*kPi*hz*frame/44100));return true;};
  auto cosine=[&](int64_t frame,float& x){x=int16_t(16000*std::cos(2*kPi*hz*frame/44100));return true;};
  TEST_ASSERT_TRUE(sampleFirRead(p,fraction,100,sine,a));
  TEST_ASSERT_TRUE(sampleFirRead(p,fraction,100,cosine,b));
  return std::hypot(a,b)/16000.;
}
void test_stopband_scales_through_maximum_supported_step() {
  for(double rate:{1.25,2.,3.,4.,8.,16.,16.0001,32.,64.,256.})
    for(uint32_t phase:{0u,0x12345678u,0xffffffffu}) {
      TEST_ASSERT_TRUE(20*std::log10(response(.675*44100/rate,rate,phase)) < -35);
      TEST_ASSERT_FLOAT_WITHIN(.3f,0.f,20*std::log10(response(2205/rate,rate,phase)));
    }
}
void test_dynamic_windows_and_cache_eviction_remain_equivalent() {
  int16_t data[4096];for(int i=0;i<4096;i++)data[i]=i%73-36;
  for(double rate:{1.25,2.,2.1189,3.,4.,8.,16.,17.,32.,256.}) {
    const auto p=sampleFirPlan(static_cast<int64_t>(rate*4294967296.));
    const auto read=[&](int64_t frame,float& x){x=data[2048+frame];return true;};
    for(uint32_t phase:{0u,0x12345678u,0xffffffffu}) {
      float y=0;TEST_ASSERT_TRUE(sampleFirRead(p,phase,0,read,y));
      TEST_ASSERT_EQUAL_FLOAT(y,sampleFirContiguous(p,phase,data+2048+p.first()));
    }
  }
  const auto p=sampleFirPlan((int64_t{3}<<32)+12345);
  const float want=sampleFirContiguous(p,0x23456789,data+2048+p.first());
  for(double rate:{1.01,1.1,1.2,1.3,1.4,1.5,1.6,1.7,1.8,1.9,2.1,2.3,2.6,2.9})
    (void)sampleFirPlan(static_cast<int64_t>(rate*4294967296.));
  for(int i=1;i<64;i++) (void)sampleFirPlan(static_cast<int64_t>((1.+i/64.)*4294967296.));
  const auto again=sampleFirPlan((int64_t{3}<<32)+12345);
  TEST_ASSERT_EQUAL_FLOAT(want,sampleFirContiguous(again,0x23456789,data+2048+again.first()));
}
void test_adaptive_and_polyphase_boundary_is_continuous() {
  int16_t data[512];for(int i=0;i<512;i++)data[i]=int16_t(10000*std::sin(i*.07));
  float y[2];
  for(int i=0;i<2;i++) {
    const auto p=sampleFirPlan(static_cast<int64_t>((16.+(i?1:-1)*1e-6)*4294967296.));
    y[i]=sampleFirContiguous(p,0x23456789,data+256+p.first());
  }
  TEST_ASSERT_FLOAT_WITHIN(1.f,y[0],y[1]);  // Q15 coefficient quantization at the bank/kernel boundary
}
void test_unity_rate_crossing_is_continuous_at_high_source_frequency() {
  int16_t data[96];for(int i=0;i<96;i++)data[i]=int16_t(24000*std::sin(2*kPi*20000*i/44100));
  constexpr uint32_t frac=0x23456789;
  const float linear=data[48]+(data[49]-data[48])*(frac*(1.f/4294967296.f));
  const auto p=sampleFirPlan(static_cast<int64_t>((1.+1e-6)*4294967296.));
  TEST_ASSERT_FLOAT_WITHIN(2.f,linear,sampleFirContiguous(p,frac,data+48+p.first()));
  float y[2];
  for(int i=0;i<2;i++) {
    const auto q=sampleFirPlan(static_cast<int64_t>((1.02+(i?1:-1)*1e-6)*4294967296.));
    y[i]=sampleFirContiguous(q,frac,data+48+q.first());
  }
  TEST_ASSERT_FLOAT_WITHIN(2.f,y[0],y[1]);
}
void test_quantized_banks_preserve_dc_without_signed_dot_overflow() {
  for(int k=1;k<=384;k++) {
    const auto p=sampleFirPlan(static_cast<int64_t>((1.+k/256.)*4294967296.));
    TEST_ASSERT_NOT_NULL(p.quantized);
    int16_t data[32];
    for(int phase=0;phase<=(1<<p.phaseBits);phase++) {
      int sum=0,absolute=0;
      for(int j=0;j<p.taps();j++) data[j]=0;
      for(int j=0;j<p.aTaps;j++) {
        const int coefficient=p.quantized[phase*p.aTaps+j];
        sum+=coefficient;absolute+=std::abs(coefficient);
        data[j+p.radius-p.aTaps/2]=coefficient<0?-32768:32767;
      }
      TEST_ASSERT_EQUAL(32768,sum);
      TEST_ASSERT_TRUE(absolute<65536);
      if(phase<(1<<p.phaseBits)) {
        const float y=sampleFirContiguous(p,uint32_t(phase)<<(32-p.phaseBits),data);
        TEST_ASSERT_TRUE(std::isfinite(y));
      }
    }
  }
}
void test_whole_q15_block_matches_individual_samples() {
  std::vector<int16_t> data(20000);for(int i=0;i<20000;i++)data[i]=int16_t(20000*std::sin(i*.13));
  for(double rate:{1.001,1.25,2.,2.1189,3.,4.,8.,16.,16.1,17.,32.,128.,192.,256.})for(int sign:{-1,1}) {
    const int64_t step=static_cast<int64_t>(rate*4294967296.)*sign;
    const auto p=sampleFirPlan(step);
    const int64_t initial=(int64_t{10000}<<32)|0x23456789;
    float got[32];sampleFirQ15Block(p,initial,step,data.data(),500,got,32);
    int64_t pos=initial;
    for(int i=0;i<32;i++,pos+=step) {
      const auto expected=sampleFirContiguous(p,static_cast<uint32_t>(pos),data.data()+(pos>>32)-500+p.first());
      TEST_ASSERT_EQUAL_FLOAT(expected,got[i]);
    }
  }
}
void test_high_rate_kernel_cache_stays_within_stream_window_and_dot_bounds() {
  for(int key=257;key<=512;key++) {
    const double rate=std::pow(2.,key/64.);
    const auto p=sampleFirPlan(static_cast<int64_t>(rate*4294967296.));
    TEST_ASSERT_TRUE(p.kernelShift);
    TEST_ASSERT_TRUE(p.taps()<=2049);
    TEST_ASSERT_EQUAL(p.last()-p.first()+1,p.taps());
    int sum=0,absolute=0;
    for(int j=0;j<p.aTaps;j++) {sum+=p.quantized[j];absolute+=std::abs(int(p.quantized[j]));}
    TEST_ASSERT_EQUAL(32768,sum);
    TEST_ASSERT_TRUE(absolute<65536);
    for(int alignment=0;alignment<512;alignment++)
      TEST_ASSERT_TRUE((alignment+p.taps()+511)/512<=5);
  }
}
void test_dense_rate_scan_keeps_required_stopband_margin() {
  for(int key=21;key<=512;key++) {
    const double rate=std::pow(2.,key/64.);
    const double hz=std::fmin(.49*44100,.675*44100/rate);
    for(uint32_t phase:{0u,0x12345678u,0x9abcdef0u,0xffffffffu}) {
      TEST_ASSERT_TRUE(20*std::log10(response(hz,rate,phase)) < -35);
      TEST_ASSERT_FLOAT_WITHIN(.3f,0.f,20*std::log10(response(2205/rate,rate,phase)));
    }
  }
}
void test_common_plan_history_cannot_change_filter_output() {
  int16_t data[96];for(int i=0;i<96;i++)data[i]=int16_t(24000*std::sin(i*.81));
  const int64_t step=static_cast<int64_t>(2.01337*4294967296.);
  const auto p=sampleFirPlan(step);
  const float want=sampleFirContiguous(p,0x23456789,data+48+p.first());
  for(int i=0;i<100;i++) (void)sampleFirPlan(static_cast<int64_t>((1.01+i*.037)*4294967296.));
  const auto again=sampleFirPlan(step);
  TEST_ASSERT_EQUAL(5,again.phaseBits);
  TEST_ASSERT_EQUAL_FLOAT(want,sampleFirContiguous(again,0x23456789,data+48+again.first()));
}
static void checkRamStaging(const std::vector<int16_t>& pcm,uint32_t from,uint32_t to,
                            uint32_t lo,uint32_t hi,uint8_t mode,bool looped,int sign,
                            uint32_t center,uint32_t fraction,double rate) {
  const int64_t step=static_cast<int64_t>(rate*4294967296.)*sign;
  const auto p=sampleFirPlan(step);
  const auto read=[&](int64_t frame,float& value) {
    const uint32_t index=sampleFirFrame(frame,from,to,lo,hi,mode,looped,step);
    TEST_ASSERT_TRUE(index>=from&&index<to);
    value=pcm[index];return true;
  };
  float reference=0;
  TEST_ASSERT_TRUE(sampleFirRead(p,fraction,center,read,reference));
  const float got=sampleFirRam(p,fraction,center,pcm.data(),from,to,lo,hi,mode,looped,step);
  TEST_ASSERT_EQUAL_MEMORY(&reference,&got,sizeof got);
}
void test_ram_staging_matches_generic_for_random_regions_and_loops() {
  uint32_t random=0xc73d182a;
  auto next=[&](){random=random*1664525u+1013904223u;return random;};
  std::vector<int16_t> pcm(256);for(auto& sample:pcm)sample=static_cast<int16_t>(next()>>16);
  for(int trial=0;trial<128;trial++) {
    const uint32_t from=next()%64,to=from+1+next()%192;
    const uint32_t lo=from+next()%(to-from),hi=lo+1+next()%(to-lo);
    const uint8_t mode=trial%4==3?255:trial%3;
    const bool looped=next()&1;
    const int sign=(next()&1)?1:-1;
    const uint32_t center=from+next()%(to-from);
    for(double rate:{1.001,2.1189,4.,17.,256.})
      for(uint32_t fraction:{0u,next(),0xffffffffu})
        checkRamStaging(pcm,from,to,lo,hi,mode,looped,sign,center,fraction,rate);
  }
}
void test_ram_staging_handles_single_frame_and_repeated_reflections() {
  std::vector<int16_t> pcm(40);for(int i=0;i<40;i++)pcm[i]=static_cast<int16_t>(i*i*37-12000);
  for(uint8_t mode:{0,1,2})for(bool looped:{false,true})for(int sign:{-1,1})
    for(int length:{1,2,3,7})for(uint32_t center:{5u,11u,18u,32u})
      for(uint32_t fraction:{0u,0x23456789u,0xffffffffu})
        checkRamStaging(pcm,5,34,11,11+length,mode,looped,sign,center,fraction,256.);
}
int main(){UNITY_BEGIN();
 RUN_TEST(test_octave_alias_rejected);RUN_TEST(test_fractional_and_integer_rates_preserve_passband);
 RUN_TEST(test_stopbands_for_multiple_rates);RUN_TEST(test_unity_and_downpitch_keep_linear_path);
 RUN_TEST(test_reverse_plan_matches_forward);RUN_TEST(test_reader_matches_contiguous_and_preserves_dc);
 RUN_TEST(test_missing_reader_does_not_publish_partial_result);RUN_TEST(test_region_bounds_and_loop_wraps);
 RUN_TEST(test_pingpong_mapping_reflects_without_duplicate_endpoint);RUN_TEST(test_plan_band_transition_is_continuous);
 RUN_TEST(test_interior_rejects_every_window_needing_edge_mapping);
 RUN_TEST(test_stopband_scales_through_maximum_supported_step);
 RUN_TEST(test_dynamic_windows_and_cache_eviction_remain_equivalent);
 RUN_TEST(test_adaptive_and_polyphase_boundary_is_continuous);
 RUN_TEST(test_unity_rate_crossing_is_continuous_at_high_source_frequency);
 RUN_TEST(test_quantized_banks_preserve_dc_without_signed_dot_overflow);
 RUN_TEST(test_whole_q15_block_matches_individual_samples);
 RUN_TEST(test_common_plan_history_cannot_change_filter_output);
 RUN_TEST(test_high_rate_kernel_cache_stays_within_stream_window_and_dot_bounds);
 RUN_TEST(test_dense_rate_scan_keeps_required_stopband_margin);
 RUN_TEST(test_ram_staging_matches_generic_for_random_regions_and_loops);
 RUN_TEST(test_ram_staging_handles_single_frame_and_repeated_reflections);
 return UNITY_END();}
