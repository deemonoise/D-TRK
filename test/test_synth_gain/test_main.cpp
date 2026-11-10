#include <unity.h>
#include <cmath>
#include "synth_gain.h"
using namespace mt;
void setUp() {}
void tearDown() {}
void test_gain_ramp_lands_exactly_and_does_not_restart() {
  GainRamp g; g.set(1.f); TEST_ASSERT_EQUAL_FLOAT(1.f,g.next()); g.set(0.f);
  for(int i=0;i<GainRamp::kSamples-1;++i) {g.set(0.f);TEST_ASSERT_TRUE(g.next()>0);}
  TEST_ASSERT_EQUAL_FLOAT(0.f,g.next()); TEST_ASSERT_FALSE(g.moving());
}
void test_gain_retarget_preserves_reached_level() {
  GainRamp g;g.set(1);g.set(0);for(int i=0;i<70;++i)g.next();
  float old=g.current();g.set(.5f);TEST_ASSERT_TRUE(std::fabs(g.next()-old)<.01f);
  for(int i=1;i<GainRamp::kSamples;++i)g.next();TEST_ASSERT_EQUAL_FLOAT(.5f,g.current());
  g.reset();g.set(.25f);TEST_ASSERT_EQUAL_FLOAT(.25f,g.next());
}
int main(){UNITY_BEGIN();RUN_TEST(test_gain_ramp_lands_exactly_and_does_not_restart);RUN_TEST(test_gain_retarget_preserves_reached_level);return UNITY_END();}
