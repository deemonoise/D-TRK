#include <unity.h>
#include "stream_info.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static const char* line(uint8_t psram, uint8_t fill, uint32_t underruns) {
  static char b[40];
  streamLine(b, sizeof b, psram, fill, underruns);
  return b;
}

void test_stream_line() {
  TEST_ASSERT_EQUAL_STRING("SD, UNDERRUNS 0", line(0, 0, 0));
  TEST_ASSERT_EQUAL_STRING("SD+PSRAM 45%, UNDERRUNS 3", line(1, 45, 3));
  TEST_ASSERT_EQUAL_STRING("SD+PSRAM, UNDERRUNS 12", line(1, 100, 12));
  TEST_ASSERT_EQUAL_STRING("SD, PSRAM FAILED, UNDERRUNS 1", line(2, 80, 1));
}

void test_stream_line_fits_small_buffers() {
  char b[8];
  streamLine(b, sizeof b, 1, 45, 3);
  TEST_ASSERT_EQUAL_STRING("SD+PSRA", b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stream_line);
  RUN_TEST(test_stream_line_fits_small_buffers);
  return UNITY_END();
}
