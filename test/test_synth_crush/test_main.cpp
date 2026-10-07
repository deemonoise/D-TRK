#include <unity.h>
#include <math.h>
#include "synth_crush.h"
#include "fx_info.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_off_is_transparent() {
  Crush c;
  c.set(0, 0);
  TEST_ASSERT_FALSE(c.on());
  for (int i = 0; i < 64; ++i) {
    const float x = sinf(i * 0.3f) * 0.7f;
    TEST_ASSERT_EQUAL_FLOAT(x, c.process(x));
  }
}

void test_bits_quantize() {
  Crush c;
  c.set(127, 0);  // 2 bits: steps of 1.0 on +-1
  TEST_ASSERT_TRUE(c.on());
  TEST_ASSERT_EQUAL_FLOAT(0.f, c.process(0.3f));
  TEST_ASSERT_EQUAL_FLOAT(1.f, c.process(0.7f));
  TEST_ASSERT_EQUAL_FLOAT(-1.f, c.process(-0.8f));
}

void test_rate_holds() {
  Crush c;
  c.set(0, 127);  // ~64 samples per held value
  c.reset();
  const float first = c.process(0.5f);
  int same = 1;
  for (int i = 1; i < 100; ++i) {
    if (c.process(0.5f + i * 0.001f) == first) ++same;
    else break;
  }
  TEST_ASSERT_EQUAL_FLOAT(0.5f, first);
  TEST_ASSERT_TRUE(same > 30 && same < 80);
}

void test_fx_entries() {
  TEST_ASSERT_EQUAL_STRING("BIT", fxName(Fx::BIT));
  TEST_ASSERT_EQUAL_STRING("SRR", fxName(Fx::SRR));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::BIT));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::SRR));
  TEST_ASSERT_TRUE(fxNextCmd(Fx::DRV, 1) == Fx::BIT);
  TEST_ASSERT_EQUAL_STRING("CRUSH", perfFxName(PerfFx::Crush));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_off_is_transparent);
  RUN_TEST(test_bits_quantize);
  RUN_TEST(test_rate_holds);
  RUN_TEST(test_fx_entries);
  return UNITY_END();
}
