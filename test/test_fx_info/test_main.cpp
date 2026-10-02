#include <unity.h>
#include <string.h>
#include "fx_info.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static const char* fmt(Fx f, uint8_t v) {
  static char b[5];
  fxFormat(f, v, b);
  return b;
}

void test_names_and_defaults() {
  TEST_ASSERT_EQUAL_STRING("RAT", fxName(Fx::RAT));
  TEST_ASSERT_EQUAL_STRING("...", fxName(Fx::None));
  TEST_ASSERT_EQUAL(2, fxDefault(Fx::RAT));
  TEST_ASSERT_EQUAL(0x12, fxDefault(Fx::CND));
}

void test_format() {
  TEST_ASSERT_EQUAL_STRING("  4", fmt(Fx::RAT, 4));
  TEST_ASSERT_EQUAL_STRING("-25", fmt(Fx::NDG, static_cast<uint8_t>(-25)));
  TEST_ASSERT_EQUAL_STRING("+10", fmt(Fx::NDG, 10));
  TEST_ASSERT_EQUAL_STRING("800", fmt(Fx::GAT, 200));
  TEST_ASSERT_EQUAL_STRING("3:4", fmt(Fx::CND, 0x34));
  TEST_ASSERT_EQUAL_STRING("FST", fmt(Fx::CND, 0));
  TEST_ASSERT_EQUAL_STRING("7th", fmt(Fx::CHD, 1));
  TEST_ASSERT_EQUAL_STRING(" --", fmt(Fx::TIE, 0));
  TEST_ASSERT_EQUAL_STRING("   ", fmt(Fx::None, 0));
}

void test_step_clamps_and_signed() {
  TEST_ASSERT_EQUAL(8, fxStep(Fx::RAT, 7, 5));
  TEST_ASSERT_EQUAL(2, fxStep(Fx::RAT, 3, -9));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(-50), fxStep(Fx::NDG, 0, -80));
  TEST_ASSERT_EQUAL(50, fxStep(Fx::NDG, static_cast<uint8_t>(-1), 100));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(-64), fxStep(Fx::PBN, 0, -100));
}

void test_cnd_order() {
  TEST_ASSERT_EQUAL(0x12, fxStep(Fx::CND, 0, 1));
  TEST_ASSERT_EQUAL(0x22, fxStep(Fx::CND, 0x12, 1));
  TEST_ASSERT_EQUAL(0x13, fxStep(Fx::CND, 0x22, 1));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::CND, 0x12, -1));
  TEST_ASSERT_EQUAL(0x88, fxStep(Fx::CND, 0x78, 100));
}

void test_cmd_cycle() {
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, 1) == Fx::CHN);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, -1) == Fx::PGM);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::PGM, 1) == Fx::None);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_and_defaults);
  RUN_TEST(test_format);
  RUN_TEST(test_step_clamps_and_signed);
  RUN_TEST(test_cnd_order);
  RUN_TEST(test_cmd_cycle);
  return UNITY_END();
}
