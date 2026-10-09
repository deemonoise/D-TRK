#include <unity.h>
#include <string.h>
#include <vector>
#include "link_frame.h"

using namespace mt::link;

void setUp() {}
void tearDown() {}

namespace {

// Encodes and feeds the bytes; true when the decoder reported exactly one frame at the end.
bool roundTrip(Decoder& d, uint8_t type, uint8_t seq, const std::vector<uint8_t>& p) {
  uint8_t enc[kMaxEncoded];
  int n = encode(type, seq, p.data(), static_cast<int>(p.size()), enc);
  if (n <= 0 || enc[n - 1] != 0) return false;
  for (int i = 0; i < n - 1; ++i)
    if (enc[i] == 0) return false;
  int frames = 0;
  for (int i = 0; i < n; ++i) frames += d.feed(enc[i]) ? 1 : 0;
  if (frames != 1) return false;
  return d.type() == type && d.seq() == seq && d.size() == static_cast<int>(p.size()) &&
         (p.empty() || memcmp(d.payload(), p.data(), p.size()) == 0);
}

}  // namespace

void test_crc16_check_value() {
  const char* s = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(reinterpret_cast<const uint8_t*>(s), 9));
}

void test_round_trip_sizes() {
  Decoder d;
  for (int n : {0, 1, 253, 254, 255, 256, 508, 512}) {
    std::vector<uint8_t> zeros(n, 0), ones(n, 0xFF), mixed(n);
    for (int i = 0; i < n; ++i) mixed[i] = static_cast<uint8_t>(i * 7 % 3 == 0 ? 0 : i);
    TEST_ASSERT_TRUE(roundTrip(d, 3, static_cast<uint8_t>(n), zeros));
    TEST_ASSERT_TRUE(roundTrip(d, 0, 0, ones));
    TEST_ASSERT_TRUE(roundTrip(d, 0xFF, 0xFF, mixed));
  }
  TEST_ASSERT_EQUAL_UINT32(0, d.errors());
}

void test_oversize_payload_rejected() {
  std::vector<uint8_t> p(kMaxPayload + 1, 1);
  uint8_t enc[kMaxEncoded + 8];
  TEST_ASSERT_EQUAL(0, encode(1, 0, p.data(), static_cast<int>(p.size()), enc));
}

void test_corrupt_byte_drops_frame_then_resyncs() {
  Decoder d;
  std::vector<uint8_t> p = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t enc[kMaxEncoded];
  int n = encode(9, 1, p.data(), static_cast<int>(p.size()), enc);
  enc[3] ^= 0x40;
  int frames = 0;
  for (int i = 0; i < n; ++i) frames += d.feed(enc[i]) ? 1 : 0;
  TEST_ASSERT_EQUAL(0, frames);
  TEST_ASSERT_EQUAL_UINT32(1, d.errors());
  TEST_ASSERT_TRUE(roundTrip(d, 9, 2, p));
}

void test_truncated_frame_resyncs() {
  Decoder d;
  std::vector<uint8_t> p(100, 0x55);
  uint8_t enc[kMaxEncoded];
  int n = encode(4, 1, p.data(), static_cast<int>(p.size()), enc);
  for (int i = 0; i < n / 2; ++i) TEST_ASSERT_FALSE(d.feed(enc[i]));
  TEST_ASSERT_FALSE(d.feed(0));  // cut short: bad CRC / COBS
  TEST_ASSERT_EQUAL_UINT32(1, d.errors());
  TEST_ASSERT_TRUE(roundTrip(d, 4, 2, p));
}

void test_garbage_before_first_delimiter_ignored() {
  Decoder d;
  for (uint8_t b : {0x12, 0x34, 0x56}) TEST_ASSERT_FALSE(d.feed(b));
  TEST_ASSERT_FALSE(d.feed(0));
  TEST_ASSERT_TRUE(roundTrip(d, 7, 7, {9, 8, 7}));
}

void test_overlong_dropped() {
  Decoder d;
  for (int i = 0; i < kMaxEncoded + 50; ++i) TEST_ASSERT_FALSE(d.feed(0x11));
  TEST_ASSERT_FALSE(d.feed(0));
  TEST_ASSERT_EQUAL_UINT32(1, d.errors());
  TEST_ASSERT_TRUE(roundTrip(d, 1, 1, {1}));
}

void test_empty_frames_ignored() {
  Decoder d;
  TEST_ASSERT_FALSE(d.feed(0));
  TEST_ASSERT_FALSE(d.feed(0));
  TEST_ASSERT_EQUAL_UINT32(0, d.errors());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_check_value);
  RUN_TEST(test_round_trip_sizes);
  RUN_TEST(test_oversize_payload_rejected);
  RUN_TEST(test_corrupt_byte_drops_frame_then_resyncs);
  RUN_TEST(test_truncated_frame_resyncs);
  RUN_TEST(test_garbage_before_first_delimiter_ignored);
  RUN_TEST(test_overlong_dropped);
  RUN_TEST(test_empty_frames_ignored);
  return UNITY_END();
}
