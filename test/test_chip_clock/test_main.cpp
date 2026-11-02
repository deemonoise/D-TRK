#include <unity.h>
#include <initializer_list>
#include <limits>
#include <cmath>
#include "synth_osc.h"
using namespace mt;
void setUp() {}
void tearDown() {}
static uint32_t ayStep(uint32_t s) {
  const uint32_t f=(s^(s>>3))&1;
  s=((s>>1)|(f<<16))&0x1ffff;
  return s?s:1;
}
static uint32_t reference(uint32_t s,uint32_t clocks) {
  if (clocks) { s=ayStep(s); --clocks; }
  clocks%=131071;
  while(clocks--)s=ayStep(s);
  return s;
}
void test_jump_matches_every_small_count_and_seed_family() {
  for(uint32_t seed:{0u,1u,0x7fffu,0x1ffffu,0xffffffu,0x800000u})
    for(uint32_t n=0;n<1024;++n) TEST_ASSERT_EQUAL_UINT32(reference(seed,n),chipAyAdvance(seed,n));
}
void test_jump_large_count_period_and_composition() {
  for(uint32_t seed:{0u,1u,0x7fffu,0x1ffffu,0xffffffu,0x800000u})
    for(uint32_t n:{1024u,16384u,131070u,131071u,131072u,262143u,0xffffffffu})
      TEST_ASSERT_EQUAL_UINT32(reference(seed,n),chipAyAdvance(seed,n));
  TEST_ASSERT_EQUAL_UINT32(chipAyAdvance(0x7fff,77777),chipAyAdvance(chipAyAdvance(0x7fff,12345),65432));
}
void test_ay_tone_noise_matches_scalar_clock_at_high_and_fractional_rates() {
  for(float inc:{0.001f,.125f,.999f,1.f,2.f,32.f,4096.f}) {
    ChipOsc actual;
    float ph=actual.phase,np=actual.nphase,nv=actual.noiseVal;
    uint32_t state=actual.lfsr;
    for(int i=0;i<1024;++i) {
      const float want=ph<.37f && nv>0 ? ChipOsc::kAyTnHigh : ChipOsc::kAyTnLow;
      TEST_ASSERT_EQUAL_FLOAT(want,actual.next(Wave::AyToneNoise,inc,.37f));
      ph+=inc;while(ph>=1)ph-=1;
      np+=inc*8;
      const uint32_t ticks=static_cast<uint32_t>(np);
      np-=ticks;state=reference(state,ticks);if(ticks)nv=(state&1)?1.f:-1.f;
      TEST_ASSERT_EQUAL_FLOAT(ph,actual.phase);
      TEST_ASSERT_EQUAL_FLOAT(np,actual.nphase);
      TEST_ASSERT_EQUAL_UINT32(state,actual.lfsr);
    }
  }
}
void test_tonal_wrap_retains_fractional_phase() {
  for(Wave wave:{Wave::Pulse,Wave::Triangle,Wave::Saw,Wave::Wt1,Wave::SidTs,Wave::AySaw,Wave::AyTri}) {
    ChipOsc osc;float phase=.12345f;osc.phase=phase;
    for(float inc:{0.01f,.5f,1.f,2.125f,32.375f,4096.25f}) {
      osc.next(wave,inc,.5f);phase+=inc;while(phase>=1)phase-=1;
      TEST_ASSERT_EQUAL_FLOAT(phase,osc.phase);
    }
  }
}
void test_nonrepresentable_clock_counts_recover_without_cast_overflow() {
  for(int w=0;w<kWaveCount;++w) for(float inc:{std::numeric_limits<float>::infinity(),1e30f,std::numeric_limits<float>::max()}) {
    ChipOsc osc;
    TEST_ASSERT_TRUE(std::isfinite(osc.next(static_cast<Wave>(w),inc,.5f)));
    TEST_ASSERT_EQUAL_FLOAT(0,osc.phase);
    TEST_ASSERT_EQUAL_FLOAT(0,osc.nphase);
    TEST_ASSERT_TRUE(std::isfinite(osc.next(static_cast<Wave>(w),.125f,.5f)));
    TEST_ASSERT_EQUAL_FLOAT(.125f,osc.phase);
  }
  // AY's 8x noise clock can overflow the integer range before the tone does.
  ChipOsc ay;
  TEST_ASSERT_TRUE(std::isfinite(ay.next(Wave::AyToneNoise,536870912.f,.5f)));
  TEST_ASSERT_EQUAL_FLOAT(0,ay.nphase);
}

int main(){UNITY_BEGIN();RUN_TEST(test_nonrepresentable_clock_counts_recover_without_cast_overflow);RUN_TEST(test_jump_matches_every_small_count_and_seed_family);RUN_TEST(test_jump_large_count_period_and_composition);RUN_TEST(test_ay_tone_noise_matches_scalar_clock_at_high_and_fractional_rates);RUN_TEST(test_tonal_wrap_retains_fractional_phase);return UNITY_END();}
