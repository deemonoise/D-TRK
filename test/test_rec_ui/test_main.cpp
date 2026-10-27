#include <string.h>
#include <unity.h>
#include "rec_ui.h"

using namespace mt;
using mt::link::RecResult;

using mt::link::RecEnd;

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
  TEST_ASSERT_EQUAL_STRING("CARD FULL", recResultText(RecResult::CardFull));
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

static void test_fmt_clock() {
  char b[16];
  recFmtClock(0, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("0:00", b);
  recFmtClock(221, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("3:41", b);
  recFmtClock(221, true, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("03:41", b);
  recFmtClock(3599, true, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("59:59", b);
  recFmtClock(3723, true, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("1:02:03", b);
  recFmtClock(6 * 3600 + 45 * 60 + 12, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("6:45:12", b);
}

static void test_sd_toast() {
  char b[48];
  recSdToast(12, kRecRate * 221 + kRecRate / 2, 0, RecResult::Ok, RecEnd::Stop, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC12.WAV 3:41 SAVED", b);
  recSdToast(3, kRecRate * 5, 1, RecResult::Ok, RecEnd::Stop, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC3.WAV 0:05 SAVED, 1 CLIP", b);
  recSdToast(4, kRecRate * 61, 7, RecResult::Ok, RecEnd::Stop, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC4.WAV 1:01 SAVED, 7 CLIPS", b);
  recSdToast(5, kRecRate * 3600, 0, RecResult::Ok, RecEnd::Space, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC5.WAV 1:00:00 CARD FULL", b);
  recSdToast(6, kRecRate * 60, 0, RecResult::Ok, RecEnd::Length, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC6.WAV 1:00 4GB LIMIT", b);
  recSdToast(7, kRecRate * 2, 2, RecResult::CardSlow, RecEnd::Stop, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("REC7.WAV 0:02 SAVED, 2 CLIPS, CARD TOO SLOW", b);
  recSdToast(8, 10, 0, RecResult::TooShort, RecEnd::Stop, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("TOO SHORT", b);
  recSdToast(8, 0, 0, RecResult::WriteFail, RecEnd::Stop, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("WRITE FAIL", b);
  // The longest fits the status bar's toast buffer (48) cut, never past it.
  recSdToast(9999, 0xFFFFFFFFu, 999999, RecResult::CardSlow, RecEnd::Length, b, sizeof b);
  TEST_ASSERT_TRUE(strlen(b) < sizeof b);
}

static void test_prefs() {
  // Nothing stored: the bytes the loader falls back to give the defaults.
  const RecPrefs d;
  TEST_ASSERT_TRUE(recPrefsFrom(0, 32, 5, static_cast<uint8_t>(kRecThreshOff), 0) == d);
  TEST_ASSERT_EQUAL(RecSource::Mic, d.src);
  TEST_ASSERT_EQUAL(recGainDefault(RecSource::Mic), d.micGain);
  TEST_ASSERT_EQUAL(recGainDefault(RecSource::Line), d.lineGain);
  TEST_ASSERT_EQUAL(kRecThreshOff, d.thresh);
  TEST_ASSERT_FALSE(d.mon);
  // Stored values come back.
  RecPrefs p = recPrefsFrom(1, 40, 9, static_cast<uint8_t>(static_cast<int8_t>(-30)), 1);
  TEST_ASSERT_EQUAL(RecSource::Line, p.src);
  TEST_ASSERT_EQUAL(40, p.micGain);
  TEST_ASSERT_EQUAL(9, p.lineGain);
  TEST_ASSERT_EQUAL(-30, p.thresh);
  TEST_ASSERT_TRUE(p.mon);
  TEST_ASSERT_TRUE(p != d);
  TEST_ASSERT_EQUAL(RecSource::Resample, recPrefsFrom(2, 0, 0, 0, 0).src);
  // Out of range: a source = MIC, gains clamped, THRESH on its steps, OFF for a positive byte.
  p = recPrefsFrom(7, 200, 99, static_cast<uint8_t>(static_cast<int8_t>(-59)), 5);
  TEST_ASSERT_EQUAL(RecSource::Mic, p.src);
  TEST_ASSERT_EQUAL(63, p.micGain);
  TEST_ASSERT_EQUAL(15, p.lineGain);
  TEST_ASSERT_EQUAL(-58, p.thresh);
  TEST_ASSERT_TRUE(p.mon);
  TEST_ASSERT_EQUAL(-60, recPrefsFrom(0, 0, 0, static_cast<uint8_t>(static_cast<int8_t>(-100)), 0).thresh);
  TEST_ASSERT_EQUAL(kRecThreshOff, recPrefsFrom(0, 0, 0, 5, 0).thresh);
  TEST_ASSERT_EQUAL(-6, recPrefsFrom(0, 0, 0, static_cast<uint8_t>(static_cast<int8_t>(-2)), 0).thresh);
}

static void test_btn_rect() {
  // 480 wide, 5 rows of 24 from y 92 (bottom 212), the work area down to 296, margin 8.
  const RecRect r = recBtnRect(480, 212, 296, 8);
  TEST_ASSERT_EQUAL(8, r.x);
  TEST_ASSERT_EQUAL(220, r.y);
  TEST_ASSERT_EQUAL(464, r.w);
  TEST_ASSERT_EQUAL(68, r.h);
  TEST_ASSERT_TRUE(r.contains(8, 220));
  TEST_ASSERT_TRUE(r.contains(471, 287));
  TEST_ASSERT_FALSE(r.contains(472, 250));
  TEST_ASSERT_FALSE(r.contains(100, 288));
  TEST_ASSERT_FALSE(r.contains(100, 211));  // the last row
  TEST_ASSERT_FALSE(r.contains(7, 250));
  const RecRect z = recBtnRect(480, 290, 296, 8);  // no room
  TEST_ASSERT_EQUAL(0, z.h);
  TEST_ASSERT_FALSE(z.contains(100, 298));
}

static void test_btn_state() {
  TEST_ASSERT_EQUAL(RecBtn::Start, recBtnState(false, false, RecState::Idle, false, false));
  TEST_ASSERT_EQUAL(RecBtn::Arm, recBtnState(false, false, RecState::Idle, true, false));
  TEST_ASSERT_EQUAL(RecBtn::WaitPlay, recBtnState(false, false, RecState::Idle, false, true));
  TEST_ASSERT_EQUAL(RecBtn::WaitPlay, recBtnState(false, false, RecState::Idle, true, true));
  TEST_ASSERT_EQUAL(RecBtn::Starting, recBtnState(false, true, RecState::Idle, false, false));
  TEST_ASSERT_EQUAL(RecBtn::Armed, recBtnState(true, false, RecState::Armed, true, false));
  TEST_ASSERT_EQUAL(RecBtn::Stop, recBtnState(true, false, RecState::Rec, true, false));
  TEST_ASSERT_EQUAL(RecBtn::Stop, recBtnState(true, false, RecState::Idle, false, false));  // before the first poll
  TEST_ASSERT_EQUAL(RecBtn::Saving, recBtnState(true, false, RecState::Saving, false, false));
  TEST_ASSERT_TRUE(recBtnLive(RecBtn::WaitPlay));
  TEST_ASSERT_TRUE(recBtnBlinks(RecBtn::WaitPlay));
  TEST_ASSERT_TRUE(recBtnBlinks(RecBtn::Armed));
  TEST_ASSERT_FALSE(recBtnBlinks(RecBtn::Stop));
  TEST_ASSERT_TRUE(recBtnLive(RecBtn::Start));
  TEST_ASSERT_TRUE(recBtnLive(RecBtn::Armed));
  TEST_ASSERT_TRUE(recBtnLive(RecBtn::Stop));
  TEST_ASSERT_FALSE(recBtnLive(RecBtn::Starting));
  TEST_ASSERT_FALSE(recBtnLive(RecBtn::Saving));
}

static void test_btn_text() {
  char b[24];
  recBtnText(RecBtn::Start, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("START", b);
  recBtnText(RecBtn::Arm, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("ARM", b);
  recBtnText(RecBtn::Armed, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("ARMED", b);
  recBtnText(RecBtn::WaitPlay, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("ARMED: PLAY", b);
  recBtnText(RecBtn::Stop, 221u * kRecRate + 100, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("STOP 03:41", b);
  recBtnText(RecBtn::Stop, 3723u * kRecRate, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("STOP 1:02:03", b);
  recBtnText(RecBtn::Starting, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("STARTING", b);
  recBtnText(RecBtn::Saving, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("SAVING...", b);
}

static void test_btn_scale() {
  // 8x16 glyphs in a 448 x 56 inside: "STOP 03:41" + the icon (12 cols) fits x3.
  TEST_ASSERT_EQUAL(3, recBtnScale(12, 8, 16, 448, 56, 3));
  TEST_ASSERT_EQUAL(3, recBtnScale(14, 8, 16, 448, 56, 4));   // 4 is 64 tall
  TEST_ASSERT_EQUAL(2, recBtnScale(20, 8, 16, 448, 56, 3));   // 480 wide at x3
  TEST_ASSERT_EQUAL(1, recBtnScale(80, 8, 16, 448, 56, 3));   // never below 1
  TEST_ASSERT_EQUAL(1, recBtnScale(5, 8, 16, 448, 20, 3));
}

static void test_tap() {
  // RESAMPLE with the transport stopped waits for Play; playing, or MIC / LINE, starts now.
  TEST_ASSERT_EQUAL(RecTap::WaitPlay, recTap(RecBtn::Start, RecSource::Resample, false));
  TEST_ASSERT_EQUAL(RecTap::WaitPlay, recTap(RecBtn::Arm, RecSource::Resample, false));
  TEST_ASSERT_EQUAL(RecTap::Start, recTap(RecBtn::Start, RecSource::Resample, true));
  TEST_ASSERT_EQUAL(RecTap::Start, recTap(RecBtn::Start, RecSource::Mic, false));
  TEST_ASSERT_EQUAL(RecTap::Start, recTap(RecBtn::Arm, RecSource::Line, false));
  TEST_ASSERT_EQUAL(RecTap::Unwait, recTap(RecBtn::WaitPlay, RecSource::Resample, false));
  TEST_ASSERT_EQUAL(RecTap::Unwait, recTap(RecBtn::WaitPlay, RecSource::Resample, true));
  TEST_ASSERT_EQUAL(RecTap::Stop, recTap(RecBtn::Armed, RecSource::Mic, false));
  TEST_ASSERT_EQUAL(RecTap::Stop, recTap(RecBtn::Stop, RecSource::Resample, false));
  TEST_ASSERT_EQUAL(RecTap::None, recTap(RecBtn::Starting, RecSource::Mic, true));
  TEST_ASSERT_EQUAL(RecTap::None, recTap(RecBtn::Saving, RecSource::Resample, false));
}

void test_cancel() {
  TEST_ASSERT_EQUAL(RecCancel::Close, recCancelAct(RecBtn::Start));
  TEST_ASSERT_EQUAL(RecCancel::Close, recCancelAct(RecBtn::Arm));
  TEST_ASSERT_EQUAL(RecCancel::Unwait, recCancelAct(RecBtn::WaitPlay));
  TEST_ASSERT_EQUAL(RecCancel::Drop, recCancelAct(RecBtn::Armed));
  TEST_ASSERT_EQUAL(RecCancel::Drop, recCancelAct(RecBtn::Stop));
  TEST_ASSERT_EQUAL(RecCancel::Drop, recCancelAct(RecBtn::Starting));
  TEST_ASSERT_EQUAL(RecCancel::None, recCancelAct(RecBtn::Saving));
  TEST_ASSERT_EQUAL_STRING("CLOSE", recCancelText(RecBtn::Start));
  TEST_ASSERT_EQUAL_STRING("CLOSE", recCancelText(RecBtn::Arm));
  TEST_ASSERT_EQUAL_STRING("CANCEL WAIT", recCancelText(RecBtn::WaitPlay));
  TEST_ASSERT_EQUAL_STRING("CANCEL ARM", recCancelText(RecBtn::Armed));
  TEST_ASSERT_EQUAL_STRING("DROP THE TAKE", recCancelText(RecBtn::Stop));
  TEST_ASSERT_EQUAL_STRING("DROP THE TAKE", recCancelText(RecBtn::Starting));
  // the same state as the button's: a take wins over a wait
  TEST_ASSERT_EQUAL(RecCancel::Drop, recCancelAct(recBtnState(true, false, RecState::Rec, false, true)));
  TEST_ASSERT_EQUAL(RecCancel::Close, recCancelAct(recBtnState(false, false, RecState::Idle, true, false)));
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
  RUN_TEST(test_fmt_clock);
  RUN_TEST(test_sd_toast);
  RUN_TEST(test_prefs);
  RUN_TEST(test_btn_rect);
  RUN_TEST(test_btn_state);
  RUN_TEST(test_btn_text);
  RUN_TEST(test_btn_scale);
  RUN_TEST(test_tap);
  RUN_TEST(test_cancel);
  return UNITY_END();
}
