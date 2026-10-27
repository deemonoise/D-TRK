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

// A then B, nothing else, released: one Both on the first release; no tap, no Back.
void test_a_then_b_is_both() {
  AbKeys k;
  k.aDown();
  expect(AbAction::AThenB, k.bDown());  // reported on B's press; not taken: Both on release
  expect(AbAction::Both, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
}

// A held, then B, taken by the app (GRID clear): no Both, no Back, no tap, in either release order.
void test_a_then_b_taken_drops_both() {
  AbKeys k;
  k.aDown();
  expect(AbAction::AThenB, k.bDown());
  k.dropBoth();
  expect(AbAction::None, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
  k.aDown();
  expect(AbAction::AThenB, k.bDown());
  k.dropBoth();
  expect(AbAction::None, k.aUp(true));
  expect(AbAction::None, k.bUp(true));
  // B first, then A: not AThenB; A used before B (track chord): not AThenB either.
  k.bDown();
  expect(AbAction::None, k.aDown());
  expect(AbAction::Both, k.aUp(false));
  k.bUp(false);
  k.aDown();
  k.track(1, false);
  expect(AbAction::None, k.bDown());
  k.bUp(false);
  k.aUp(false);
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
  AbOut o = k.track(5, true);  // Shift + A + N: solo
  expect(AbAction::Solo, o);
  TEST_ASSERT_EQUAL_INT(5, o.delta);
  expect(AbAction::None, k.aUp(false));  // used: no tap
  k.bDown();
  o = k.track(3, true);  // B + Shift + N: queue
  expect(AbAction::QueuePattern, o);
  TEST_ASSERT_EQUAL_INT(3, o.delta);
  expect(AbAction::None, k.bUp(false));
}

// Without Shift: A + N / B + N (the MIX scene ends), still chords (consumed, no tap / Back).
void test_track_chords_no_shift() {
  AbKeys k;
  k.aDown();
  AbOut o = k.track(2, false);
  expect(AbAction::ATrack, o);
  TEST_ASSERT_EQUAL_INT(2, o.delta);
  expect(AbAction::None, k.aUp(false));
  k.bDown();
  o = k.track(6, false);
  expect(AbAction::BTrack, o);
  TEST_ASSERT_EQUAL_INT(6, o.delta);
  expect(AbAction::None, k.bUp(false));
}

void test_b_wins_over_a() {
  AbKeys k;
  k.aDown();
  k.bDown();
  expect(AbAction::TabTurn, k.turn(1, false));
  expect(AbAction::BTrack, k.track(0, false));
  expect(AbAction::QueuePattern, k.track(0, true));
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

void test_b_then_a_release_is_both_once() {
  AbKeys k;
  k.bDown();
  k.aDown();
  expect(AbAction::Both, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
  k.bDown();
  k.aDown();
  expect(AbAction::Both, k.aUp(false));
  expect(AbAction::None, k.bUp(false));
}

// A turn or a track button while both are held is that chord, not Both; an A edit cancelled by B neither.
void test_both_not_after_turn_track_or_cancel() {
  AbKeys k;
  k.aDown();
  k.bDown();
  expect(AbAction::TabTurn, k.turn(1, false));
  expect(AbAction::None, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
  k.aDown();
  k.bDown();
  expect(AbAction::BTrack, k.track(1, false));
  expect(AbAction::None, k.aUp(false));
  expect(AbAction::None, k.bUp(false));
  k.aDown();
  k.turn(1, false);
  expect(AbAction::EditCancel, k.bDown());
  expect(AbAction::None, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
}

// A chord before the other button came down (B + turn, A + track button) is not Both either.
void test_both_not_after_chord_before_the_other() {
  AbKeys k;
  k.bDown();
  expect(AbAction::TabTurn, k.turn(1, false));
  k.aDown();
  expect(AbAction::None, k.aUp(false));
  expect(AbAction::None, k.bUp(false));
  k.aDown();
  expect(AbAction::ATrack, k.track(2, false));
  k.bDown();
  expect(AbAction::None, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
}

void test_b_then_a_track_no_taps() {
  AbKeys k;
  k.bDown();
  k.aDown();
  expect(AbAction::BTrack, k.track(2, false));
  expect(AbAction::None, k.aUp(false));
  expect(AbAction::None, k.bUp(false));
}

void test_a_click() {
  AbKeys k;
  expect(AbAction::Pass, k.aClick());  // nothing held
  k.aDown();
  expect(AbAction::AClick, k.aClick());
  expect(AbAction::None, k.aUp(false));  // the click used A: no tap
  k.aDown();
  k.turn(1, false);
  expect(AbAction::AClick, k.aClick());
  expect(AbAction::EditEnd, k.aUp(false));  // the edit still ends
  k.aDown();
  k.bDown();
  expect(AbAction::Pass, k.aClick());  // with B: not an A click, the state stays
  expect(AbAction::Both, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
  k.aDown();
  k.aClick();
  k.bDown();
  expect(AbAction::None, k.bUp(false));  // A was used: no Both
  expect(AbAction::None, k.aUp(false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_passes_through);
  RUN_TEST(test_a_turn_edits_and_release_ends);
  RUN_TEST(test_a_tap);
  RUN_TEST(test_b_during_a_edit_cancels);
  RUN_TEST(test_a_then_b_is_both);
  RUN_TEST(test_a_then_b_taken_drops_both);
  RUN_TEST(test_b_turn_tabs_and_pages);
  RUN_TEST(test_b_tap_back_and_shift_undo);
  RUN_TEST(test_track_chords);
  RUN_TEST(test_track_chords_no_shift);
  RUN_TEST(test_b_wins_over_a);
  RUN_TEST(test_reset_clears_held);
  RUN_TEST(test_release_after_reset_does_nothing);
  RUN_TEST(test_b_then_a_turn_no_taps);
  RUN_TEST(test_b_then_a_release_is_both_once);
  RUN_TEST(test_both_not_after_turn_track_or_cancel);
  RUN_TEST(test_both_not_after_chord_before_the_other);
  RUN_TEST(test_b_then_a_track_no_taps);
  RUN_TEST(test_a_click);
  return UNITY_END();
}
