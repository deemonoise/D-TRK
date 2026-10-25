#include <string.h>
#include <unity.h>
#include "rec_ui.h"

using namespace mt;
using mt::link::RecResult;

void setUp() {}
void tearDown() {}

static void test_result_text() {
  TEST_ASSERT_EQUAL_STRING("NO SD", recResultText(RecResult::NoSd));
  TEST_ASSERT_EQUAL_STRING("BUSY", recResultText(RecResult::Busy));
  TEST_ASSERT_EQUAL_STRING("NO SHIELD", recResultText(RecResult::NoCodec));
  TEST_ASSERT_EQUAL_STRING("NO PSRAM", recResultText(RecResult::NoPsram));
  TEST_ASSERT_EQUAL_STRING("TOO SHORT", recResultText(RecResult::TooShort));
  TEST_ASSERT_EQUAL_STRING("CARD TOO SLOW", recResultText(RecResult::CardSlow));
  TEST_ASSERT_EQUAL_STRING("WRITE FAIL", recResultText(RecResult::WriteFail));
  TEST_ASSERT_EQUAL_STRING("NOT RECORDING", recResultText(RecResult::NotRec));
  TEST_ASSERT_EQUAL_STRING("REC FAILED", recResultText(static_cast<RecResult>(200)));
}

static void test_source_step() {
  TEST_ASSERT_EQUAL(RecSource::Line, recSourceStep(RecSource::Mic, 1, true));
  TEST_ASSERT_EQUAL(RecSource::Resample, recSourceStep(RecSource::Line, 5, true));
  TEST_ASSERT_EQUAL(RecSource::Mic, recSourceStep(RecSource::Line, -3, true));
  TEST_ASSERT_EQUAL(RecSource::Resample, recSourceStep(RecSource::Resample, 1, true));
  // No codec: RESAMPLE whatever the step.
  TEST_ASSERT_EQUAL(RecSource::Resample, recSourceStep(RecSource::Mic, -1, false));
  TEST_ASSERT_EQUAL(RecSource::Resample, recSourceStep(RecSource::Resample, -1, false));
}

static void test_thresh_step() {
  TEST_ASSERT_EQUAL_INT8(-60, recThreshStep(kRecThreshOff, 1));
  TEST_ASSERT_EQUAL_INT8(kRecThreshOff, recThreshStep(kRecThreshOff, -1));
  TEST_ASSERT_EQUAL_INT8(kRecThreshOff, recThreshStep(-60, -1));
  TEST_ASSERT_EQUAL_INT8(-58, recThreshStep(-60, 1));
  TEST_ASSERT_EQUAL_INT8(-40, recThreshStep(-60, 10));
  TEST_ASSERT_EQUAL_INT8(-6, recThreshStep(-8, 5));
  TEST_ASSERT_EQUAL_INT8(kRecThreshOff, recThreshStep(-50, -10));
  TEST_ASSERT_EQUAL_INT8(-40, recThreshStep(-41, 0));  // odd values snap up to the grid
}

static void test_gain_default() {
  TEST_ASSERT_EQUAL(32, recGainDefault(RecSource::Mic));
  TEST_ASSERT_EQUAL(5, recGainDefault(RecSource::Line));
  TEST_ASSERT_EQUAL(0, recGainDefault(RecSource::Resample));
}

static void test_fmt_time() {
  char b[16];
  recFmtTime(0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("00:00.0", b);
  recFmtTime(kRecRate * 12 + kRecRate / 2 - 1, b, sizeof b);  // 12.49.. s: tenths truncate
  TEST_ASSERT_EQUAL_STRING("00:12.4", b);
  recFmtTime(kRecMaxFrames, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("05:00.0", b);
}

static void test_peak_db() {
  TEST_ASSERT_EQUAL(-96, recPeakDb(0));
  TEST_ASSERT_EQUAL(0, recPeakDb(32767));
  TEST_ASSERT_EQUAL(-6, recPeakDb(16422));
  TEST_ASSERT_EQUAL(-1, recPeakDb(kRecPeakRed));
  TEST_ASSERT_TRUE(recPeakRed(kRecPeakRed));
  TEST_ASSERT_FALSE(recPeakRed(kRecPeakRed - 1));
}

static void test_meter_px() {
  TEST_ASSERT_EQUAL(0, recMeterPx(0, 200));
  TEST_ASSERT_EQUAL(0, recMeterPx(32, 200));  // under -60 dB
  TEST_ASSERT_EQUAL(200, recMeterPx(32767, 200));
  TEST_ASSERT_EQUAL(100, recMeterPx(1036, 200));  // -30 dB: half way
}

static void test_peak_hold() {
  RecPeakHold h;
  h.feed(20000, 100);
  TEST_ASSERT_EQUAL_UINT16(20000, h.value(100));
  h.feed(500, 300);  // lower: the held peak stays for a second
  TEST_ASSERT_EQUAL_UINT16(20000, h.value(1099));
  TEST_ASSERT_EQUAL_UINT16(500, h.value(1100));
  h.feed(30000, 1200);  // higher: taken at once
  TEST_ASSERT_EQUAL_UINT16(30000, h.value(1200));
  h.clear();
  TEST_ASSERT_EQUAL_UINT16(0, h.value(1300));
}

static void test_toast() {
  char b[48];
  recToast(1, kRecRate * 12 + kRecRate / 2 - 100, 3, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC1 12.4s, 3 CLIPS", b);
  recToast(7, kRecRate, 0, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC7 1.0s", b);
  recToast(12, kRecRate * 2, 1, true, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC12 2.0s, 1 CLIP, CARD TOO SLOW", b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_result_text);
  RUN_TEST(test_source_step);
  RUN_TEST(test_thresh_step);
  RUN_TEST(test_gain_default);
  RUN_TEST(test_fmt_time);
  RUN_TEST(test_peak_db);
  RUN_TEST(test_meter_px);
  RUN_TEST(test_peak_hold);
  RUN_TEST(test_toast);
  return UNITY_END();
}
