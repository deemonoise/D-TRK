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
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, -1) == Fx::CON);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::PGM, 1) == Fx::SLD);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::CUT, 1) == Fx::DCY);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::CON, 1) == Fx::None);
}

void test_fm_lock_fx() {
  TEST_ASSERT_EQUAL(22, static_cast<int>(Fx::DCY));  // file values follow CUT
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DCY) + kMacCon, static_cast<int>(Fx::CON));
  TEST_ASSERT_EQUAL_STRING("DEC", fxName(Fx::DCY));
  TEST_ASSERT_EQUAL_STRING("COL", fxName(Fx::COL));
  TEST_ASSERT_EQUAL_STRING("SHP", fxName(Fx::SHP));
  TEST_ASSERT_EQUAL_STRING("SWP", fxName(Fx::SWP));
  TEST_ASSERT_EQUAL_STRING("CON", fxName(Fx::CON));
  TEST_ASSERT_EQUAL(64, fxDefault(Fx::COL));
  TEST_ASSERT_EQUAL(127, fxStep(Fx::SHP, 120, 100));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::SHP, 3, -10));
  TEST_ASSERT_EQUAL_STRING(" 99", fmt(Fx::DCY, 99));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::DCY));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::CON));
}

void test_synth_fx_names_ranges() {
  // File compatibility: the old commands keep their values.
  TEST_ASSERT_EQUAL(15, static_cast<int>(Fx::PGM));
  TEST_ASSERT_EQUAL(16, static_cast<int>(Fx::SLD));
  TEST_ASSERT_EQUAL_STRING("SLD", fxName(Fx::SLD));
  TEST_ASSERT_EQUAL_STRING("VIB", fxName(Fx::VIB));
  TEST_ASSERT_EQUAL_STRING("ARP", fxName(Fx::ARP));
  TEST_ASSERT_EQUAL_STRING("VSL", fxName(Fx::VSL));
  TEST_ASSERT_EQUAL_STRING("OFS", fxName(Fx::OFS));
  TEST_ASSERT_EQUAL_STRING("CUT", fxName(Fx::CUT));
  TEST_ASSERT_EQUAL(16, fxDefault(Fx::SLD));
  TEST_ASSERT_EQUAL(0x44, fxDefault(Fx::VIB));
  TEST_ASSERT_EQUAL(0x37, fxDefault(Fx::ARP));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(-8), fxDefault(Fx::VSL));
  TEST_ASSERT_EQUAL(0, fxDefault(Fx::OFS));
  TEST_ASSERT_EQUAL(6, fxDefault(Fx::CUT));
  TEST_ASSERT_EQUAL(1, fxStep(Fx::SLD, 5, -10));
  TEST_ASSERT_EQUAL(255, fxStep(Fx::VIB, 0xF0, 100));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(-64), fxStep(Fx::VSL, 0, -100));
  TEST_ASSERT_EQUAL(63, fxStep(Fx::VSL, 0, 100));
  TEST_ASSERT_EQUAL(96, fxStep(Fx::CUT, 90, 10));
  TEST_ASSERT_EQUAL(1, fxStep(Fx::CUT, 2, -10));
  TEST_ASSERT_EQUAL_STRING(" 4F", fmt(Fx::VIB, 0x4F));
  TEST_ASSERT_EQUAL_STRING(" 07", fmt(Fx::ARP, 0x07));
  TEST_ASSERT_EQUAL_STRING(" -8", fmt(Fx::VSL, static_cast<uint8_t>(-8)));
  TEST_ASSERT_EQUAL_STRING("  6", fmt(Fx::CUT, 6));
}

void test_synth_only() {
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::SLD));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::VIB));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::ARP));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::VSL));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::OFS));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::CUT));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::None));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::PGM));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::RAT));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::Count));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_and_defaults);
  RUN_TEST(test_format);
  RUN_TEST(test_step_clamps_and_signed);
  RUN_TEST(test_cnd_order);
  RUN_TEST(test_cmd_cycle);
  RUN_TEST(test_synth_fx_names_ranges);
  RUN_TEST(test_synth_only);
  RUN_TEST(test_fm_lock_fx);
  return UNITY_END();
}
