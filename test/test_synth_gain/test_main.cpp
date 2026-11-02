#include <unity.h>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "synth_syn.h"
using namespace mt;
void setUp() {}
void tearDown() {}
static SynParams isolated(bool noise, float gain) {
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Wt); // missing WT silences the body
  p.hz[0] = 440.f;
  p.subWave = static_cast<uint8_t>(SynSubWave::Sine);
  if (noise) p.noise = gain; else p.sub = gain;
  return p;
}
static void fade(bool noise, bool down) {
  SynVoice v, reference;
  auto start = isolated(noise, down ? 1.f : 0.f);
  auto full = isolated(noise, 1.f);
  v.control(start, 32);
  reference.control(full, 32);
  auto target = isolated(noise, down ? 0.f : 1.f);
  v.control(target, 32);
  float actual[32] = {}, source[32] = {};
  v.render(actual, 32, 1.f);
  reference.render(source, 32, 1.f);
  for (int i=0;i<32;++i) {
    const float g = down ? 1.f-i/32.f : i/32.f;
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, source[i]*g, actual[i]);
  }
  v.control(target, 32);
  float after[32] = {};
  v.render(after, 32, 1.f);
  if (down) for (float x : after) TEST_ASSERT_EQUAL_FLOAT(0.f, x);
}
void test_sub_fades_in_and_out() { fade(false, false); fade(false, true); }
void test_noise_fades_in_and_out() { fade(true, false); fade(true, true); }
void test_sub_noise_ramps_survive_render_split() {
  for (bool noise : {false,true}) {
    SynVoice a,b;
    auto p=isolated(noise,0.f); a.control(p,32); b.control(p,32);
    p=isolated(noise,.73f); a.control(p,32); b.control(p,32);
    float x[64]={},y[64]={};a.render(x,32,1.f);b.render(y,7,1.f);b.render(y+7,25,1.f);
    p=isolated(noise,.23f);a.control(p,32);b.control(p,32);
    a.render(x+32,32,1.f);b.render(y+32,13,1.f);b.render(y+45,19,1.f);
    for (int i=0;i<64;++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f,x[i],y[i]);
  }
}
void test_first_control_source_gain_is_immediate() {
  for(bool noise : {false,true}) {
    SynVoice a,b;auto pa=isolated(noise,.5f),pb=isolated(noise,1.f);
    a.control(pa,32);b.control(pb,32);float x[32]={},y[32]={};a.render(x,32,1.f);b.render(y,32,1.f);
    for(int i=0;i<32;++i)TEST_ASSERT_EQUAL_FLOAT(y[i]*.5f,x[i]);
  }
}
int main(){UNITY_BEGIN();RUN_TEST(test_sub_fades_in_and_out);RUN_TEST(test_noise_fades_in_and_out);RUN_TEST(test_sub_noise_ramps_survive_render_split);RUN_TEST(test_first_control_source_gain_is_immediate);return UNITY_END();}
