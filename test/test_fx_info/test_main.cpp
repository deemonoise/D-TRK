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
  TEST_ASSERT_EQUAL(kCndNoFill, fxStep(Fx::CND, 0x78, 100));  // NFL is last
}

void test_cnd_order_has_fill() {
  uint8_t v = 0x88;  // 8:8, the last A:B
  v = fxStep(Fx::CND, v, 1);
  TEST_ASSERT_EQUAL_HEX8(kCndFill, v);
  v = fxStep(Fx::CND, v, 1);
  TEST_ASSERT_EQUAL_HEX8(kCndNoFill, v);
  v = fxStep(Fx::CND, v, 1);
  TEST_ASSERT_EQUAL_HEX8(kCndNoFill, v);  // clamped
  TEST_ASSERT_EQUAL_HEX8(kCndFill, fxStep(Fx::CND, kCndNoFill, -1));
  TEST_ASSERT_EQUAL_HEX8(0x88, fxStep(Fx::CND, kCndFill, -1));
  char out[5];
  fxFormat(Fx::CND, kCndFill, out);
  TEST_ASSERT_EQUAL_STRING("FIL", out);
  fxFormat(Fx::CND, kCndNoFill, out);
  TEST_ASSERT_EQUAL_STRING("NFL", out);
}

void test_cmd_cycle() {
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, 1) == Fx::CHN);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, -1) == Fx::ARM);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::PGM, 1) == Fx::SLD);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::CUT, 1) == Fx::DCY);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::CON, 1) == Fx::FLT);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::RES, 1) == Fx::SLC);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::SLC, 1) == Fx::OFF);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::OFF, 1) == Fx::DLY);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::DLY, 1) == Fx::ACC);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::ACC, 1) == Fx::DRV);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::RVB, 1) == Fx::ARM);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::ARM, 1) == Fx::None);
}

void test_new_sound_fx() {
  TEST_ASSERT_EQUAL_STRING("DRV", fxName(Fx::DRV));
  TEST_ASSERT_EQUAL_STRING("RVB", fxName(Fx::RVB));
  TEST_ASSERT_EQUAL_STRING("ARM", fxName(Fx::ARM));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::DRV) && fxSynthOnly(Fx::RVB) && fxSynthOnly(Fx::ARM));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::ACC));
  TEST_ASSERT_EQUAL(64, fxDefault(Fx::DRV));
  TEST_ASSERT_EQUAL(0x03, fxDefault(Fx::ARM));
  TEST_ASSERT_EQUAL(127, fxStep(Fx::RVB, 120, 50));
}

void test_arm_format_and_step() {
  char o[5];
  fxFormat(Fx::ARM, 0x03, o);
  TEST_ASSERT_EQUAL_STRING(" U3", o);
  fxFormat(Fx::ARM, 0x14, o);
  TEST_ASSERT_EQUAL_STRING(" D4", o);
  fxFormat(Fx::ARM, 0x22, o);
  TEST_ASSERT_EQUAL_STRING(" B2", o);
  fxFormat(Fx::ARM, 0x38, o);
  TEST_ASSERT_EQUAL_STRING(" R8", o);
  TEST_ASSERT_EQUAL_HEX8(0x04, fxStep(Fx::ARM, 0x03, 1));   // rate first
  TEST_ASSERT_EQUAL_HEX8(0x11, fxStep(Fx::ARM, 0x08, 1));   // rate 8 -> next mode, rate 1
  TEST_ASSERT_EQUAL_HEX8(0x38, fxStep(Fx::ARM, 0x38, 1));   // clamps at R8
  TEST_ASSERT_EQUAL_HEX8(0x01, fxStep(Fx::ARM, 0x01, -1));  // clamps at U1
  TEST_ASSERT_EQUAL_HEX8(0x38, fxStep(Fx::ARM, 0x01, 100));
}

void test_filter_fx() {
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DCY) + kLockFlt, static_cast<int>(Fx::FLT));
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DCY) + kLockRes, static_cast<int>(Fx::RES));
  TEST_ASSERT_EQUAL_STRING("FLT", fxName(Fx::FLT));
  TEST_ASSERT_EQUAL_STRING("RES", fxName(Fx::RES));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::FLT));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::RES));
  TEST_ASSERT_EQUAL(64, fxDefault(Fx::FLT));
  TEST_ASSERT_EQUAL(0, fxDefault(Fx::RES));
  TEST_ASSERT_EQUAL(127, fxStep(Fx::FLT, 120, 100));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::RES, 3, -10));
  TEST_ASSERT_EQUAL_STRING(" 99", fmt(Fx::FLT, 99));
}

void test_dly_fx() {
  TEST_ASSERT_EQUAL_STRING("DLY", fxName(Fx::DLY));
  TEST_ASSERT_EQUAL_STRING("DELAY SEND", fxLongName(Fx::DLY));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::DLY));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::OFF));
  TEST_ASSERT_EQUAL(64, fxDefault(Fx::DLY));
  TEST_ASSERT_EQUAL(127, fxStep(Fx::DLY, 120, 100));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::DLY, 3, -10));
  TEST_ASSERT_EQUAL_STRING(" 99", fmt(Fx::DLY, 99));
}

void test_slc_fx() {
  TEST_ASSERT_EQUAL_STRING("SLC", fxName(Fx::SLC));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::SLC));
  TEST_ASSERT_EQUAL(0, fxDefault(Fx::SLC));
  TEST_ASSERT_EQUAL(31, fxStep(Fx::SLC, 30, 5));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::SLC, 2, -5));
  TEST_ASSERT_EQUAL_STRING(" 12", fmt(Fx::SLC, 12));
  // OFF: ticks 0..96, MIDI too.
  TEST_ASSERT_EQUAL_STRING("OFF", fxName(Fx::OFF));
  TEST_ASSERT_EQUAL_STRING("NOTE OFF", fxLongName(Fx::OFF));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::OFF));
  TEST_ASSERT_EQUAL(0, fxDefault(Fx::OFF));
  TEST_ASSERT_EQUAL(96, fxStep(Fx::OFF, 90, 10));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::OFF, 3, -5));
  TEST_ASSERT_EQUAL_STRING("  6", fmt(Fx::OFF, 6));
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

void test_long_names() {
  TEST_ASSERT_EQUAL_STRING("", fxLongName(Fx::None));
  TEST_ASSERT_EQUAL_STRING("VIBRATO", fxLongName(Fx::VIB));
  // DCY..CON: FM / DRUM macro and the SYNTH one.
  TEST_ASSERT_EQUAL_STRING("MACRO DECAY / SHP1", fxLongName(Fx::DCY));
  TEST_ASSERT_EQUAL_STRING("MACRO CONTOUR / SENV", fxLongName(Fx::CON));
  for (int i = 1; i < static_cast<int>(Fx::Count); ++i) {
    const size_t n = strlen(fxLongName(static_cast<Fx>(i)));
    TEST_ASSERT_TRUE(n > 0 && n <= 24);
  }
}

// ACC: drum tracks only, lane mask 0..255 shown as two hex digits (like VIB / ARP).
void test_acc_lane_mask() {
  TEST_ASSERT_EQUAL_STRING("ACC", fxName(Fx::ACC));
  TEST_ASSERT_EQUAL_STRING("ACCENT", fxLongName(Fx::ACC));
  TEST_ASSERT_EQUAL(0xFF, fxDefault(Fx::ACC));
  TEST_ASSERT_EQUAL(0xFF, fxStep(Fx::ACC, 0xFE, 5));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::ACC, 1, -5));
  TEST_ASSERT_EQUAL_STRING(" A5", fmt(Fx::ACC, 0xA5));
  TEST_ASSERT_TRUE(fxDrumOnly(Fx::ACC));
  TEST_ASSERT_FALSE(fxDrumOnly(Fx::DLY));
  TEST_ASSERT_FALSE(fxDrumOnly(Fx::None));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::ACC));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_long_names);
  RUN_TEST(test_names_and_defaults);
  RUN_TEST(test_format);
  RUN_TEST(test_step_clamps_and_signed);
  RUN_TEST(test_cnd_order);
  RUN_TEST(test_cmd_cycle);
  RUN_TEST(test_synth_fx_names_ranges);
  RUN_TEST(test_synth_only);
  RUN_TEST(test_fm_lock_fx);
  RUN_TEST(test_filter_fx);
  RUN_TEST(test_slc_fx);
  RUN_TEST(test_dly_fx);
  RUN_TEST(test_acc_lane_mask);
  RUN_TEST(test_cnd_order_has_fill);
  RUN_TEST(test_new_sound_fx);
  RUN_TEST(test_arm_format_and_step);
  return UNITY_END();
}
