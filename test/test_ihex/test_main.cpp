#include <unity.h>
#include <string.h>
#include <vector>
#include "ihex.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static IhexReader::Line feed(IhexReader& r, const char* s) { return r.feed(s, static_cast<int>(strlen(s))); }

void test_data_and_linear_base() {
  IhexReader r;
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Other), static_cast<int>(feed(r, ":0200000460009A")));
  r = IhexReader();
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Other), static_cast<int>(feed(r, ":0200000460009A\r")));
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Data), static_cast<int>(feed(r, ":0410000046434642DB")));
  TEST_ASSERT_EQUAL_HEX32(0x60001000, r.address());
  TEST_ASSERT_EQUAL(4, r.size());
  TEST_ASSERT_EQUAL_HEX8(0x46, r.data()[0]);
  TEST_ASSERT_EQUAL_HEX8(0x42, r.data()[3]);
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Other), static_cast<int>(feed(r, ":040000056000100186")));  // start: ignored
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Data), static_cast<int>(feed(r, ":01200000AA35")));
  TEST_ASSERT_EQUAL_HEX32(0x60002000, r.address());  // the base stays
  TEST_ASSERT_FALSE(r.eof());
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Eof), static_cast<int>(feed(r, ":00000001FF")));
  TEST_ASSERT_TRUE(r.eof());
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::AfterEof), static_cast<int>(feed(r, ":01200000AA35")));
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Other), static_cast<int>(feed(r, "")));
}

void test_segment_base() {
  IhexReader r;
  feed(r, ":020000021000EC");
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Data), static_cast<int>(feed(r, ":01000400AA51")));
  TEST_ASSERT_EQUAL_HEX32(0x10004, r.address());
}

void test_bad_lines() {
  IhexReader r;
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Bad), static_cast<int>(feed(r, ":01200000AA36")));  // checksum
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Bad), static_cast<int>(feed(r, "01200000AA35")));   // no colon
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Bad), static_cast<int>(feed(r, ":02200000AA34")));  // short
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Bad), static_cast<int>(feed(r, ":01200000AG35")));  // not hex
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Bad), static_cast<int>(feed(r, ":00000006FA")));    // type
  TEST_ASSERT_EQUAL(static_cast<int>(IhexReader::Line::Bad), static_cast<int>(feed(r, ":01200000AA3")));   // odd
}

static void put32(std::vector<uint8_t>& v, uint32_t at, uint32_t x) {
  for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}

static std::vector<uint8_t> image() {
  std::vector<uint8_t> v(0x3000, 0xFF);
  put32(v, 0, 0x42464346);
  put32(v, 0x1000, 0x432000D1);
  put32(v, 0x1004, 0x60001400);
  put32(v, 0x1014, 0x60001000);
  memcpy(v.data() + 0x2100, "fw_teensy41", 11);
  return v;
}

void test_image_check() {
  std::vector<uint8_t> v = image();
  TEST_ASSERT_TRUE(teensyImageOk(v.data(), v.size(), 0x60000000, "fw_teensy41"));
  TEST_ASSERT_FALSE(teensyImageOk(v.data(), v.size(), 0x60000000, "fw_teensy40"));
  TEST_ASSERT_FALSE(teensyImageOk(v.data(), 0x1010, 0x60000000, "fw_teensy41"));  // cut
  v = image();
  v[0] = 0;
  TEST_ASSERT_FALSE(teensyImageOk(v.data(), v.size(), 0x60000000, "fw_teensy41"));  // no FCFB
  v = image();
  put32(v, 0x1004, 0x70000000);
  TEST_ASSERT_FALSE(teensyImageOk(v.data(), v.size(), 0x60000000, "fw_teensy41"));  // entry outside
  v = image();
  put32(v, 0x1014, 0x60002000);
  TEST_ASSERT_FALSE(teensyImageOk(v.data(), v.size(), 0x60000000, "fw_teensy41"));  // IVT self
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_data_and_linear_base);
  RUN_TEST(test_segment_base);
  RUN_TEST(test_bad_lines);
  RUN_TEST(test_image_check);
  return UNITY_END();
}
