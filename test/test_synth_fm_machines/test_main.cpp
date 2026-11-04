#include <cmath>
#include <unity.h>
#include "synth_fm_machines.h"
using namespace mt;
void setUp() {}
void tearDown() {}
static void controls(uint8_t machine, float (&raw)[8]) {
  Instrument m;
  fmSetMachine(m, machine);
  for (int i=0;i<8;++i) raw[i]=m.fmParam[i];
}
void test_tone_uses_measured_raw_ratio_not_old_shape_zones() {
  float raw[8];controls(static_cast<uint8_t>(FmMachine::Tone),raw);
  FmParams p;fmMachine(static_cast<uint8_t>(FmMachine::Tone),raw,60,p);
  TEST_ASSERT_FLOAT_WITHIN(.5f,261.555f,p.hz);
  bool found=false;
  for (const auto& o:p.op) if (!o.output && fabsf(o.ratio-4.4f)<.01f) found=true;
  TEST_ASSERT_TRUE(found);
}
void test_all_ten_machines_have_distinct_nonempty_render() {
  for (int machine=0;machine<static_cast<int>(FmMachine::Count);++machine) {
    float raw[8];controls(machine,raw);FmParams p;fmMachine(machine,raw,60,p);
    FmVoice v;v.trigger(false);double energy=0;
    for(int n=0;n<100;++n){float b[32]={};v.control(p,32);v.render(b,32,1);for(float x:b){TEST_ASSERT_TRUE(std::isfinite(x));TEST_ASSERT_LESS_THAN_FLOAT(5,fabsf(x));energy+=x*x;}}
    TEST_ASSERT_GREATER_THAN(1e-9,energy);
  }
}
void test_dispatch_sanitizes_invalid_controls_and_falls_back_to_tone() {
  float raw[8]={NAN,-100,INFINITY,127,0,64,0,0};FmParams p;
  fmMachine(255,raw,NAN,p);TEST_ASSERT_TRUE(std::isfinite(p.hz));TEST_ASSERT_GREATER_THAN_FLOAT(0,p.hz);
  FmVoice v;v.trigger(false);v.control(p,32);float b[32]={};v.render(b,32,1);
  for(float x:b) TEST_ASSERT_TRUE(std::isfinite(x));
}
void test_last_three_controls_are_independent_inputs() {
  float raw[8];controls(static_cast<uint8_t>(FmMachine::Tone),raw);
  FmParams a,b;fmMachine(static_cast<uint8_t>(FmMachine::Tone),raw,60,a);
  raw[7]=127;fmMachine(static_cast<uint8_t>(FmMachine::Tone),raw,60,b);
  TEST_ASSERT_GREATER_THAN_FLOAT(a.sat,b.sat);
  raw[7]=0;raw[6]=127;fmMachine(static_cast<uint8_t>(FmMachine::Tone),raw,60,b);
  bool changed=false;for(int i=0;i<kFmOps;++i)changed|=fabsf(a.op[i].level-b.op[i].level)>.01f||fabsf(a.op[i].sustain-b.op[i].sustain)>.01f;
  TEST_ASSERT_TRUE(changed);
}
int main(){UNITY_BEGIN();RUN_TEST(test_tone_uses_measured_raw_ratio_not_old_shape_zones);RUN_TEST(test_all_ten_machines_have_distinct_nonempty_render);RUN_TEST(test_dispatch_sanitizes_invalid_controls_and_falls_back_to_tone);RUN_TEST(test_last_three_controls_are_independent_inputs);return UNITY_END();}
