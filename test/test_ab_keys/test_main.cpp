#include <unity.h>
#include "ab_keys.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static void expect(AbAction a, const AbOut& o) { TEST_ASSERT_EQUAL_INT(static_cast<int>(a), static_cast<int>(o.act)); }

void test_idle_passes_through() {
  AbKeys k;
  expect(AbAction::Pass, k.turn(3, false));
  expect(AbAction::Pass, k.track(2, false));
}

void test_a_turn_edits_and_release_ends() {
  AbKeys k;
  expect(AbAction::None, k.aDown());
  AbOut o = k.turn(-2, true);
  expect(AbAction::EditTurn, o);
  TEST_ASSERT_EQUAL_INT(-2, o.delta);
  TEST_ASSERT_TRUE(o.shift);
  expect(AbAction::EditTurn, k.turn(1, false));
  expect(AbAction::EditEnd, k.aUp(false));
  expect(AbAction::Pass, k.turn(1, false));
}

void test_a_tap() {
  AbKeys k;
  k.aDown();
  AbOut o = k.aUp(true);
  expect(AbAction::ATap, o);
  TEST_ASSERT_TRUE(o.shift);
}

void test_b_during_a_edit_cancels() {
  AbKeys k;
  k.aDown();
  k.turn(1, false);
  expect(AbAction::EditCancel, k.bDown());
  expect(AbAction::None, k.bUp(false));  // no Back
  expect(AbAction::None, k.aUp(false));  // no EditEnd, no tap
}

void test_b_during_a_without_edit_does_nothing() {
  AbKeys k;
  k.aDown();
  expect(AbAction::None, k.bDown());
  expect(AbAction::None, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
}

void test_b_turn_tabs_and_pages() {
  AbKeys k;
  k.bDown();
  AbOut o = k.turn(1, false);
  expect(AbAction::TabTurn, o);
  TEST_ASSERT_EQUAL_INT(1, o.delta);
  expect(AbAction::PageTurn, k.turn(-1, true));
  expect(AbAction::None, k.bUp(false));  // used: no Back
}

void test_b_tap_back_and_shift_undo() {
  AbKeys k;
  k.bDown();
  expect(AbAction::Back, k.bUp(false));
  k.bDown();
  expect(AbAction::Undo, k.bUp(true));
}

void test_track_chords() {
  AbKeys k;
  k.aDown();
  AbOut o = k.track(5, false);
  expect(AbAction::Solo, o);
  TEST_ASSERT_EQUAL_INT(5, o.delta);
  expect(AbAction::None, k.aUp(false));  // used: no tap
  k.bDown();
  o = k.track(3, true);
  expect(AbAction::QueuePattern, o);
  TEST_ASSERT_EQUAL_INT(3, o.delta);
  TEST_ASSERT_TRUE(o.shift);
  expect(AbAction::None, k.bUp(false));
}

void test_b_wins_over_a() {
  AbKeys k;
  k.aDown();
  k.bDown();
  expect(AbAction::TabTurn, k.turn(1, false));
  expect(AbAction::QueuePattern, k.track(0, false));
}

void test_reset_clears_held() {
  AbKeys k;
  k.aDown();
  k.turn(1, false);
  k.reset();
  TEST_ASSERT_FALSE(k.aHeld());
  expect(AbAction::Pass, k.turn(1, false));
}

void test_release_after_reset_does_nothing() {
  AbKeys k;
  k.aDown();
  k.bDown();
  k.reset();
  expect(AbAction::None, k.aUp(false));  // no tap
  expect(AbAction::None, k.bUp(false));  // no Back
  k.bDown();
  k.reset();
  expect(AbAction::None, k.bUp(true));  // no Undo
}

void test_b_then_a_turn_no_taps() {
  AbKeys k;
  k.bDown();
  expect(AbAction::None, k.aDown());
  expect(AbAction::TabTurn, k.turn(1, false));
  expect(AbAction::None, k.aUp(false));  // no tap
  expect(AbAction::None, k.bUp(false));  // no Back
}

void test_b_then_a_release_both_no_taps() {
  AbKeys k;
  k.bDown();
  k.aDown();
  expect(AbAction::None, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
  k.bDown();
  k.aDown();
  expect(AbAction::None, k.aUp(false));
  expect(AbAction::None, k.bUp(false));
}

void test_b_then_a_track_no_taps() {
  AbKeys k;
  k.bDown();
  k.aDown();
  expect(AbAction::QueuePattern, k.track(2, false));
  expect(AbAction::None, k.aUp(false));
  expect(AbAction::None, k.bUp(false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_passes_through);
  RUN_TEST(test_a_turn_edits_and_release_ends);
  RUN_TEST(test_a_tap);
  RUN_TEST(test_b_during_a_edit_cancels);
  RUN_TEST(test_b_during_a_without_edit_does_nothing);
  RUN_TEST(test_b_turn_tabs_and_pages);
  RUN_TEST(test_b_tap_back_and_shift_undo);
  RUN_TEST(test_track_chords);
  RUN_TEST(test_b_wins_over_a);
  RUN_TEST(test_reset_clears_held);
  RUN_TEST(test_release_after_reset_does_nothing);
  RUN_TEST(test_b_then_a_turn_no_taps);
  RUN_TEST(test_b_then_a_release_both_no_taps);
  RUN_TEST(test_b_then_a_track_no_taps);
  return UNITY_END();
}
