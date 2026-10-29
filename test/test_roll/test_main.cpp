#include <unity.h>
#include "roll.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Step note(uint8_t n) {
  Step s;
  s.note = n;
  return s;
}

static void test_gate_val() {
  Step s = note(60);
  TEST_ASSERT_EQUAL_UINT8(50, rollGateVal(s, 50));    // Def gate
  TEST_ASSERT_EQUAL_UINT8(129, rollGateVal(s, 129));
  s.fx[2] = {Fx::GAT, 20};
  TEST_ASSERT_EQUAL_UINT8(20, rollGateVal(s, 50));    // the step's GAT wins
  s.fx[2].val = 200;
  TEST_ASSERT_EQUAL_UINT8(200, rollGateVal(s, 50));
}

static void test_set_gate() {
  Step s = note(60);
  TEST_ASSERT_EQUAL(RollLen::Ok, rollSetGate(s, 129, 50));
  TEST_ASSERT_EQUAL(Fx::GAT, s.fx[0].cmd);
  TEST_ASSERT_EQUAL_UINT8(129, s.fx[0].val);
  TEST_ASSERT_EQUAL(RollLen::Ok, rollSetGate(s, 40, 50));
  TEST_ASSERT_EQUAL_UINT8(40, s.fx[0].val);
  // An existing GAT slot is reused, the others stay.
  s.fx[0] = {Fx::VIB, 5};
  s.fx[3] = {Fx::GAT, 50};
  TEST_ASSERT_EQUAL(RollLen::Ok, rollSetGate(s, 160, 50));
  TEST_ASSERT_EQUAL(Fx::VIB, s.fx[0].cmd);
  TEST_ASSERT_EQUAL(Fx::GAT, s.fx[3].cmd);
  TEST_ASSERT_EQUAL_UINT8(160, s.fx[3].val);
  TEST_ASSERT_EQUAL(Fx::None, s.fx[1].cmd);
  // Def gate exactly removes GAT.
  TEST_ASSERT_EQUAL(RollLen::Ok, rollSetGate(s, 50, 50));
  TEST_ASSERT_NULL(s.find(Fx::GAT));
  TEST_ASSERT_EQUAL(Fx::VIB, s.fx[0].cmd);
  // No free slot.
  for (FxSlot& f : s.fx) f = {Fx::VIB, 1};
  const Step was = s;
  TEST_ASSERT_EQUAL(RollLen::NoSlot, rollSetGate(s, 110, 50));
  for (int k = 0; k < kFxSlots; ++k) TEST_ASSERT_EQUAL(was.fx[k].cmd, s.fx[k].cmd);
  // Removing needs no slot.
  TEST_ASSERT_EQUAL(RollLen::Ok, rollSetGate(s, 50, 50));
}

static void test_span() {
  Step t[16];
  t[0] = note(60);
  TEST_ASSERT_EQUAL(50, rollSpan(t, 16, 0, 50));   // Def gate
  t[0].fx[0] = {Fx::GAT, 30};
  TEST_ASSERT_EQUAL(30, rollSpan(t, 16, 0, 50));   // under a step: its share
  t[0].fx[0] = {Fx::GAT, 129};
  TEST_ASSERT_EQUAL(gatePercent(129), rollSpan(t, 16, 0, 50));
  t[2] = note(62);                                 // the next note cuts the bar
  TEST_ASSERT_EQUAL(200, rollSpan(t, 16, 0, 50));
  t[2] = Step();
  t[1].note = kNoteOff;                            // so does OFF
  TEST_ASSERT_EQUAL(100, rollSpan(t, 16, 0, 50));
  t[1] = Step();
  TEST_ASSERT_EQUAL(200, rollSpan(t, 2, 0, 50));   // the track's end
  TEST_ASSERT_EQUAL(0, rollSpan(t, 16, 5, 50));    // no note
  TEST_ASSERT_EQUAL(0, rollSpan(t, 16, 1, 50));
  // An fx-only step does not cut.
  t[1].fx[0] = {Fx::VIB, 3};
  TEST_ASSERT_EQUAL(gatePercent(129), rollSpan(t, 16, 0, 50));
}

static void test_chord_extra() {
  Step s = note(60);
  uint8_t out[4];
  TEST_ASSERT_EQUAL(0, rollChordExtra(s, 0, ScaleType::Major, out));
  s.fx[1] = {Fx::CHD, chordValue(kChordMaj, 0)};
  TEST_ASSERT_EQUAL(2, rollChordExtra(s, 0, ScaleType::Chromatic, out));
  TEST_ASSERT_EQUAL_UINT8(64, out[0]);
  TEST_ASSERT_EQUAL_UINT8(67, out[1]);
  s.note = kNoteOff;
  TEST_ASSERT_EQUAL(0, rollChordExtra(s, 0, ScaleType::Chromatic, out));
}

static void test_key() {
  TEST_ASSERT_EQUAL(RollKey::Root, rollKey(62, 2, ScaleType::Major));
  TEST_ASSERT_EQUAL(RollKey::In, rollKey(64, 2, ScaleType::Major));   // E in D major
  TEST_ASSERT_EQUAL(RollKey::Out, rollKey(65, 2, ScaleType::Major));  // F in D major
  TEST_ASSERT_EQUAL(RollKey::Root, rollKey(48, 0, ScaleType::Chromatic));
  TEST_ASSERT_EQUAL(RollKey::Chromatic, rollKey(49, 0, ScaleType::Chromatic));
}

static void test_key_row() {
  RollKeyRow k = rollKeyRow(60);  // C: white, a white B below
  TEST_ASSERT_FALSE(k.black);
  TEST_ASSERT_TRUE(k.lineBelow);
  TEST_ASSERT_EQUAL(60, k.upper);
  TEST_ASSERT_EQUAL(60, k.lower);
  k = rollKeyRow(61);  // C#: D above, C below
  TEST_ASSERT_TRUE(k.black);
  TEST_ASSERT_FALSE(k.lineBelow);
  TEST_ASSERT_EQUAL(62, k.upper);
  TEST_ASSERT_EQUAL(60, k.lower);
  TEST_ASSERT_TRUE(rollKeyRow(65).lineBelow);   // F over E
  TEST_ASSERT_FALSE(rollKeyRow(64).lineBelow);  // E over D#
  TEST_ASSERT_FALSE(rollKeyRow(62).lineBelow);  // D over C#
  TEST_ASSERT_TRUE(rollKeyRow(0).lineBelow);    // the lowest C
  TEST_ASSERT_FALSE(rollKeyRow(127).black);     // G9, the top
  TEST_ASSERT_TRUE(rollKeyRow(70).black);       // A#
}

void test_toggle_lane() {
  Step s;
  TEST_ASSERT_TRUE(rollToggleLane(s, 2));
  TEST_ASSERT_EQUAL_UINT8(0, s.note);  // the track's velocity
  TEST_ASSERT_EQUAL_UINT8(0x04, s.vel);
  s.note = 90;
  TEST_ASSERT_TRUE(rollToggleLane(s, 0));
  TEST_ASSERT_EQUAL_UINT8(0x05, s.vel);
  TEST_ASSERT_EQUAL_UINT8(90, s.note);  // the step velocity stays
  TEST_ASSERT_FALSE(rollToggleLane(s, 2));
  TEST_ASSERT_EQUAL_UINT8(0x01, s.vel);
  s.note = kNoteOff;
  s.vel = kVelDefault;
  TEST_ASSERT_TRUE(rollToggleLane(s, 7));
  TEST_ASSERT_EQUAL_UINT8(0, s.note);
  TEST_ASSERT_EQUAL_UINT8(0x80, s.vel);
}

// Lane 12 lives in lanesHi (bit 3); a stale lanesHi on a note-off step is cleared with the mask.
void test_roll_toggle_lane_twelve() {
  Step s;
  TEST_ASSERT_TRUE(rollToggleLane(s, 11));
  TEST_ASSERT_EQUAL_UINT8(0, s.note);
  TEST_ASSERT_EQUAL_UINT8(0, s.vel);
  TEST_ASSERT_EQUAL_UINT8(0x08, s.lanesHi);
  TEST_ASSERT_TRUE(laneOn(s, 11));
  TEST_ASSERT_FALSE(rollToggleLane(s, 11));
  TEST_ASSERT_EQUAL_UINT8(0, s.lanesHi);
  TEST_ASSERT_FALSE(rollToggleLane(s, 16));  // out of range: nothing
  TEST_ASSERT_FALSE(rollToggleLane(s, -1));
  TEST_ASSERT_EQUAL_UINT16(0, laneMask(s));
  s.note = kNoteOff;
  s.lanesHi = 0x40;
  TEST_ASSERT_TRUE(rollToggleLane(s, 0));
  TEST_ASSERT_EQUAL_UINT16(0x0001, laneMask(s));
}

static void test_follow() {
  TEST_ASSERT_EQUAL(0, rollFollow(0, 5, 16, 128));
  TEST_ASSERT_EQUAL(5, rollFollow(10, 5, 16, 128));    // up to the cursor
  TEST_ASSERT_EQUAL(15, rollFollow(0, 30, 16, 128));   // down: the cursor on the last row
  TEST_ASSERT_EQUAL(112, rollFollow(120, 127, 16, 128));  // clamped to total - visible
  TEST_ASSERT_EQUAL(0, rollFollow(3, 2, 16, 8));       // fewer than visible
  TEST_ASSERT_EQUAL(0, rollPage(15, 16));
  TEST_ASSERT_EQUAL(16, rollPage(16, 16));
  TEST_ASSERT_EQUAL(112, rollPage(127, 16));
}

static void test_mix() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, rollMix565(0x0000, 0xFFFF, 0));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, rollMix565(0x0000, 0xFFFF, 256));
  const uint16_t m = rollMix565(0x0000, 0xFFFF, 128);
  TEST_ASSERT_EQUAL(15, (m >> 11) & 31);
  TEST_ASSERT_EQUAL(31, (m >> 5) & 63);
  TEST_ASSERT_EQUAL(15, m & 31);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_roll_toggle_lane_twelve);
  RUN_TEST(test_gate_val);
  RUN_TEST(test_set_gate);
  RUN_TEST(test_span);
  RUN_TEST(test_chord_extra);
  RUN_TEST(test_key);
  RUN_TEST(test_key_row);
  RUN_TEST(test_toggle_lane);
  RUN_TEST(test_follow);
  RUN_TEST(test_mix);
  return UNITY_END();
}
