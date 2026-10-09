#include <unity.h>
#include <string.h>
#include "file_rules.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_project_base() {
  TEST_ASSERT_TRUE(projectBaseValid("song_1-a"));
  TEST_ASSERT_TRUE(projectBaseValid("abcdefghijklmnop"));   // 16
  TEST_ASSERT_FALSE(projectBaseValid("abcdefghijklmnopq"));  // 17
  TEST_ASSERT_FALSE(projectBaseValid(""));
  TEST_ASSERT_FALSE(projectBaseValid("my song"));
  TEST_ASSERT_FALSE(projectBaseValid("a.b"));
}

void test_firmware_names() {
  TEST_ASSERT_TRUE(webFirmwareName("firmware.bin"));
  TEST_ASSERT_TRUE(webFirmwareName("X.BIN"));
  TEST_ASSERT_FALSE(webFirmwareName(".bin"));
  TEST_ASSERT_TRUE(webSynthFirmwareName("firmware.HEX"));
  TEST_ASSERT_FALSE(webSynthFirmwareName("firmware.bin"));
  TEST_ASSERT_FALSE(webSynthFirmwareName(".hex"));
  TEST_ASSERT_FALSE(webSynthFirmwareName(nullptr));
  TEST_ASSERT_FALSE(webFirmwareName("firmware.elf"));
  TEST_ASSERT_FALSE(webFirmwareName(nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_project_base);
  RUN_TEST(test_firmware_names);
  return UNITY_END();
}
