#include <cmath>
#include <unity.h>
#include "synth_comp.h"
#include "synth_delay.h"
#include "synth_reverb.h"

void setUp() {}
void tearDown() {}

void test_compressor_does_not_boost_below_threshold() {
  mt::Compressor c;
  float l[128], r[128];
  for (int b = 0; b < 128; ++b) {
    for (int i=0;i<128;++i) l[i] = r[i] = .001f;
    c.process(l,r,nullptr,nullptr,128,100,50,64);
  }
  TEST_ASSERT_FLOAT_WITHIN(.000001f,.001f,l[127]);
}

void test_silent_external_key_does_not_compress_program() {
  mt::Compressor c;
  float l[128], r[128], key[128] = {};
  for (int b = 0; b < 128; ++b) {
    for (int i=0;i<128;++i) l[i] = r[i] = .9f;
    c.process(l,r,key,key,128,100,50,127);
  }
  TEST_ASSERT_FLOAT_WITHIN(.000001f,.9f,l[127]);
}

void test_feedback_reaches_exact_silence() {
  int16_t line[256] = {};
  mt::Delay delay; delay.setBuffer(line,256);
  float send[64] = {}, l[64] = {}, r[64] = {};
  send[0] = .7f;
  for (int b=0;b<3000;++b) {
    for(int i=0;i<64;++i) l[i]=r[i]=0;
    delay.process(send,l,r,64,50,70,40,127);
    send[0]=0;
  }
  for (int i=0;i<64;++i) TEST_ASSERT_FLOAT_WITHIN(1e-10f,0,l[i]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_compressor_does_not_boost_below_threshold);
  RUN_TEST(test_silent_external_key_does_not_compress_program);
  RUN_TEST(test_feedback_reaches_exact_silence);
  return UNITY_END();
}
