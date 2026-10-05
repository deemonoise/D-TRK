#include <unity.h>
#include "track_leds.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_selected_lit_without_flash() {
  TEST_ASSERT_EQUAL_HEX8(1 << 3, trackLedMask(3, 0));
  TEST_ASSERT_EQUAL_HEX8(0, trackLedMask(-1, 0));
}

void test_flash_on_selected_goes_dark() {
  TEST_ASSERT_EQUAL_HEX8(0, trackLedMask(3, 1 << 3));
  TEST_ASSERT_EQUAL_HEX8(1 << 0, trackLedMask(3, (1 << 3) | (1 << 0)));
}

void test_flash_on_others_lights_them() {
  TEST_ASSERT_EQUAL_HEX8((1 << 3) | (1 << 0) | (1 << 7), trackLedMask(3, (1 << 0) | (1 << 7)));
}

// Default wiring: buttons P00-P07, LEDs P10-P17.
constexpr uint8_t kBtn[8] = {0, 1, 2, 3, 4, 5, 6, 7};
constexpr uint8_t kLed[8] = {8, 9, 10, 11, 12, 13, 14, 15};
// Button 1 moved to P10, LED 1 dropped.
constexpr uint8_t kBtnMoved[8] = {8, 1, 2, 3, 4, 5, 6, 7};
constexpr uint8_t kLedMoved[8] = {kNoPin, 9, 10, 11, 12, 13, 14, 15};

void test_port_word_default() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, trackPortWord(0, kLed));
  TEST_ASSERT_EQUAL_HEX16(0xFEFF, trackPortWord(1 << 0, kLed));  // LED 1 sinks on P10
  TEST_ASSERT_EQUAL_HEX16(0x7FFF, trackPortWord(1 << 7, kLed));
}

void test_port_word_skips_missing_led() {
  // LED 1 has no pin: P10 stays high so the button there can be read.
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, trackPortWord(1 << 0, kLedMoved));
  TEST_ASSERT_EQUAL_HEX16(0xFDFF, trackPortWord(0x03, kLedMoved));
}

void test_pressed_default() {
  TEST_ASSERT_EQUAL_HEX8(0, trackPressed(0xFFFF, kBtn));
  TEST_ASSERT_EQUAL_HEX8(0x81, trackPressed(0xFF7E, kBtn));
  TEST_ASSERT_EQUAL_HEX8(0, trackPressed(0x00FF, kBtn));  // LED pins low are not presses
}

void test_pressed_moved_button() {
  TEST_ASSERT_EQUAL_HEX8(0x01, trackPressed(0xFEFF, kBtnMoved));  // P10 low = button 1
  TEST_ASSERT_EQUAL_HEX8(0, trackPressed(0xFFFE, kBtnMoved));     // dead P00 ignored
  TEST_ASSERT_EQUAL_HEX8(0x02, trackPressed(0xFFFD, kBtnMoved));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_selected_lit_without_flash);
  RUN_TEST(test_flash_on_selected_goes_dark);
  RUN_TEST(test_flash_on_others_lights_them);
  RUN_TEST(test_port_word_default);
  RUN_TEST(test_port_word_skips_missing_led);
  RUN_TEST(test_pressed_default);
  RUN_TEST(test_pressed_moved_button);
  return UNITY_END();
}
