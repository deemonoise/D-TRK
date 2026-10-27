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

void test_web_export_path() {
  TEST_ASSERT_TRUE(webExportPath("/export/DEMO/P01/DEMO_P01_MASTER.wav"));
  TEST_ASSERT_TRUE(webExportPath("/export/DEMO/SONG"));
  TEST_ASSERT_FALSE(webExportPath("/export"));
  TEST_ASSERT_FALSE(webExportPath("/export/"));
  TEST_ASSERT_FALSE(webExportPath("/export/../projects/A.mtp"));
  TEST_ASSERT_FALSE(webExportPath("/export/A/../../x"));
  TEST_ASSERT_FALSE(webExportPath("/export/A/.."));
  TEST_ASSERT_FALSE(webExportPath("/samples/x.wav"));
  TEST_ASSERT_FALSE(webExportPath("//export/x"));
  TEST_ASSERT_FALSE(webExportPath("/exportx/a"));
  TEST_ASSERT_FALSE(webExportPath("/export//a"));
  TEST_ASSERT_FALSE(webExportPath("/export/a\\b"));
  TEST_ASSERT_FALSE(webExportPath(""));
  TEST_ASSERT_FALSE(webExportPath(nullptr));
}

void test_range_start() {
  uint32_t at = 7;
  TEST_ASSERT_TRUE(parseRangeStart("bytes=1000-", at));
  TEST_ASSERT_EQUAL_UINT32(1000, at);
  TEST_ASSERT_TRUE(parseRangeStart("bytes=0-", at));
  TEST_ASSERT_EQUAL_UINT32(0, at);
  TEST_ASSERT_TRUE(parseRangeStart("bytes=5-99", at));  // an end: served to the file's end anyway
  TEST_ASSERT_EQUAL_UINT32(5, at);
  TEST_ASSERT_FALSE(parseRangeStart("bytes=-500", at));  // suffix ranges: not supported
  TEST_ASSERT_FALSE(parseRangeStart("items=1-", at));
  TEST_ASSERT_FALSE(parseRangeStart("bytes=x-", at));
  TEST_ASSERT_FALSE(parseRangeStart("bytes=99999999999-", at));
  TEST_ASSERT_FALSE(parseRangeStart(nullptr, at));
}

void test_browse_up() {
  char dir[64], left[16];
  strcpy(dir, "/samples/drums/909");
  TEST_ASSERT_TRUE(browseUp(dir, "/samples", left, sizeof left));
  TEST_ASSERT_EQUAL_STRING("/samples/drums", dir);
  TEST_ASSERT_EQUAL_STRING("909", left);
  TEST_ASSERT_TRUE(browseUp(dir, "/samples", left, sizeof left));
  TEST_ASSERT_EQUAL_STRING("/samples", dir);
  TEST_ASSERT_EQUAL_STRING("drums", left);
  TEST_ASSERT_FALSE(browseUp(dir, "/samples", left, sizeof left));  // the root: B closes
  TEST_ASSERT_EQUAL_STRING("/samples", dir);
  TEST_ASSERT_EQUAL_STRING("", left);
  strcpy(dir, "/samplesX/a");  // not below root, only a prefix
  TEST_ASSERT_TRUE(browseUp(dir, "/samples", left, sizeof left));
  TEST_ASSERT_EQUAL_STRING("/samples", dir);
  TEST_ASSERT_EQUAL_STRING("", left);
  strcpy(dir, "/mid");  // shorter than root
  TEST_ASSERT_FALSE(browseUp(dir, "/midi", left, sizeof left));
  strcpy(dir, "/midi/a long folder name here");  // left cut to its size
  TEST_ASSERT_TRUE(browseUp(dir, "/midi", left, sizeof left));
  TEST_ASSERT_EQUAL_STRING("/midi", dir);
  TEST_ASSERT_EQUAL_STRING("a long folder n", left);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_project_base);
  RUN_TEST(test_firmware_names);
  RUN_TEST(test_web_export_path);
  RUN_TEST(test_range_start);
  RUN_TEST(test_browse_up);
  return UNITY_END();
}
