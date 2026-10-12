#include <unity.h>
#include "track_buttons.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Default wiring: buttons P00-P07.
constexpr uint8_t kBtn[8] = {0, 1, 2, 3, 4, 5, 6, 7};
// Button 1 moved to P10, P00 not wired.
constexpr uint8_t kBtnMoved[8] = {8, 1, 2, 3, 4, 5, 6, 7};
constexpr uint8_t kBtnMissing[8] = {kNoPin, 1, 2, 3, 4, 5, 6, 7};

void test_pressed_default() {
  TEST_ASSERT_EQUAL_HEX8(0, trackPressed(0xFFFF, kBtn));
  TEST_ASSERT_EQUAL_HEX8(0x81, trackPressed(0xFF7E, kBtn));
  TEST_ASSERT_EQUAL_HEX8(0, trackPressed(0x00FF, kBtn));  // other pins low are not presses
}

void test_pressed_moved_button() {
  TEST_ASSERT_EQUAL_HEX8(0x01, trackPressed(0xFEFF, kBtnMoved));  // P10 low = button 1
  TEST_ASSERT_EQUAL_HEX8(0, trackPressed(0xFFFE, kBtnMoved));     // dead P00 ignored
  TEST_ASSERT_EQUAL_HEX8(0x02, trackPressed(0xFFFD, kBtnMoved));
}

void test_pressed_unwired_button() { TEST_ASSERT_EQUAL_HEX8(0, trackPressed(0x0000, kBtnMissing) & 0x01); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pressed_default);
  RUN_TEST(test_pressed_moved_button);
  RUN_TEST(test_pressed_unwired_button);
  return UNITY_END();
}
