#include <cstring>
#include <deque>
#include <new>
#include <vector>
#include <unity.h>
#include "frame_writer.h"

using namespace mt::link;

struct Port : FramePort {
  bool open = true;
  int space = 8192, calls = 0;
  std::deque<int> accept;
  std::vector<uint8_t> bytes;
  bool connected() override { return open; }
  int capacity() override { return space; }
  size_t write(const uint8_t* data, size_t size) override {
    ++calls;
    size_t n = size;
    if (!accept.empty()) {
      if (n > static_cast<size_t>(accept.front())) n = accept.front();
      accept.pop_front();
    }
    bytes.insert(bytes.end(), data, data + n);
    space -= static_cast<int>(n);
    return n;
  }
};

static std::vector<uint8_t> packet(uint8_t seq) {
  uint8_t bytes[kMaxEncoded];
  const uint8_t payload[] = {1, 0, 2, 3};
  const int size = encode(7, seq, payload, sizeof payload, bytes);
  return {bytes, bytes + size};
}

void setUp() {}
void tearDown() {}

void test_closed_host_never_calls_sdk_write() {
  FrameWriter tx; Port port; port.open = false;
  const auto frame = packet(1);
  TEST_ASSERT_FALSE(tx.submit(port, frame.data(), frame.size(), true));
  TEST_ASSERT_FALSE(tx.submit(port, frame.data(), frame.size(), false));
  TEST_ASSERT_FALSE(tx.step(port));
  TEST_ASSERT_EQUAL(0, port.calls);
  TEST_ASSERT_EQUAL(2, tx.stats().closedDropped);
  TEST_ASSERT_EQUAL(0, tx.stats().pending);
}

void test_reply_waits_until_entire_frame_fits_without_sdk_call() {
  FrameWriter tx; Port port;
  const auto frame = packet(2);
  port.space = static_cast<int>(frame.size()) - 1;
  TEST_ASSERT_TRUE(tx.submit(port, frame.data(), frame.size(), true));
  TEST_ASSERT_FALSE(tx.step(port));
  TEST_ASSERT_EQUAL(0, port.calls);
  TEST_ASSERT_EQUAL(1, tx.stats().pending);
  port.space = frame.size();
  TEST_ASSERT_TRUE(tx.step(port));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data(), port.bytes.data(), frame.size());
  TEST_ASSERT_EQUAL(0, tx.stats().pending);
  TEST_ASSERT_FALSE(tx.step(port));
}

void test_telemetry_cannot_fill_queue_or_overtake_a_reply() {
  FrameWriter tx; Port port;
  const auto reply = packet(3), status = packet(4);
  port.space = 0;
  TEST_ASSERT_FALSE(tx.submit(port, status.data(), status.size(), false));
  TEST_ASSERT_TRUE(tx.submit(port, reply.data(), reply.size(), true));
  port.space = 8192;
  TEST_ASSERT_FALSE(tx.submit(port, status.data(), status.size(), false));
  TEST_ASSERT_EQUAL(2, tx.stats().telemetryDropped);
  TEST_ASSERT_TRUE(tx.step(port));
  TEST_ASSERT_TRUE(tx.submit(port, status.data(), status.size(), false));
  TEST_ASSERT_TRUE(tx.step(port));
  std::vector<uint8_t> expected = reply;
  expected.insert(expected.end(), status.begin(), status.end());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), port.bytes.data(), expected.size());
}

void test_short_write_retains_tail_before_following_frame() {
  FrameWriter tx; Port port;
  const auto first = packet(5), second = packet(6);
  port.accept.push_back(3);
  TEST_ASSERT_TRUE(tx.submit(port, first.data(), first.size(), true));
  TEST_ASSERT_TRUE(tx.step(port));
  TEST_ASSERT_TRUE(tx.submit(port, second.data(), second.size(), true));
  TEST_ASSERT_EQUAL(1, tx.stats().shortWrites);
  TEST_ASSERT_EQUAL(2, tx.stats().pending);
  TEST_ASSERT_TRUE(tx.step(port));
  TEST_ASSERT_TRUE(tx.step(port));
  std::vector<uint8_t> expected = first;
  expected.insert(expected.end(), second.begin(), second.end());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), port.bytes.data(), expected.size());
  Decoder decoder; int frames = 0;
  for (uint8_t b : port.bytes) if (decoder.feed(b)) ++frames;
  TEST_ASSERT_EQUAL(2, frames);
  TEST_ASSERT_EQUAL(0, decoder.errors());
}

void test_zero_write_does_not_lose_or_duplicate_the_reply() {
  FrameWriter tx; Port port;
  const auto frame = packet(7);
  port.accept.push_back(0);
  TEST_ASSERT_TRUE(tx.submit(port, frame.data(), frame.size(), true));
  TEST_ASSERT_FALSE(tx.step(port));
  TEST_ASSERT_EQUAL(1, tx.stats().pending);
  TEST_ASSERT_EQUAL(1, tx.stats().shortWrites);
  TEST_ASSERT_TRUE(tx.step(port));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data(), port.bytes.data(), frame.size());
}

void test_full_queue_retains_replies_and_exposes_backpressure() {
  FrameWriter tx; Port port; port.space = 0;
  const auto frame = packet(8);
  std::vector<uint8_t> expected;
  for (int i = 0; i < FrameWriter::kQueue; ++i) {
    const auto unique = packet(i + 8);
    TEST_ASSERT_TRUE(tx.submit(port, unique.data(), unique.size(), true));
    expected.insert(expected.end(), unique.begin(), unique.end());
  }
  TEST_ASSERT_FALSE(tx.roomForCritical());
  TEST_ASSERT_FALSE(tx.submit(port, frame.data(), frame.size(), true));
  TEST_ASSERT_EQUAL(1, tx.stats().overflow);
  TEST_ASSERT_EQUAL(FrameWriter::kQueue, tx.stats().pending);
  port.space = 8192;
  for (int i = 0; i < 3; ++i) TEST_ASSERT_TRUE(tx.step(port));
  for (int i = 0; i < 3; ++i) {
    const auto unique = packet(i + 8 + FrameWriter::kQueue);
    TEST_ASSERT_TRUE(tx.submit(port, unique.data(), unique.size(), true));
    expected.insert(expected.end(), unique.begin(), unique.end());
  }
  for (int i = 0; i < FrameWriter::kQueue; ++i) TEST_ASSERT_TRUE(tx.step(port));
  TEST_ASSERT_TRUE(tx.roomForCritical());
  TEST_ASSERT_EQUAL(expected.size(), port.bytes.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), port.bytes.data(), expected.size());
}

void test_disconnect_cancels_old_session_and_delimits_the_new_one() {
  FrameWriter tx; Port port;
  const auto old = packet(9), hello = packet(10);
  port.accept.push_back(3);
  TEST_ASSERT_TRUE(tx.submit(port, old.data(), old.size(), true));
  TEST_ASSERT_TRUE(tx.step(port));
  port.open = false;
  TEST_ASSERT_FALSE(tx.step(port));
  TEST_ASSERT_EQUAL(1, tx.stats().closedDropped);
  TEST_ASSERT_EQUAL(0, tx.stats().pending);
  port.open = true;
  TEST_ASSERT_TRUE(tx.submit(port, hello.data(), hello.size(), true));
  port.space = 0;
  TEST_ASSERT_FALSE(tx.step(port));
  port.space = 8192;
  TEST_ASSERT_TRUE(tx.step(port));  // separator, not a stale reply suffix
  TEST_ASSERT_EQUAL(0, port.bytes.back());
  TEST_ASSERT_EQUAL(1, tx.stats().pending);
  TEST_ASSERT_TRUE(tx.step(port));
  Decoder decoder; int frames = 0;
  for (uint8_t b : port.bytes) if (decoder.feed(b)) ++frames;
  TEST_ASSERT_EQUAL(1, frames);
  TEST_ASSERT_EQUAL(10, decoder.seq());
  TEST_ASSERT_EQUAL(1, decoder.errors());  // the old prefix is not disguised as a good frame
}

void test_invalid_frame_is_reported_without_writing() {
  FrameWriter tx; Port port;
  const uint8_t bad[] = {1, 2, 3};
  TEST_ASSERT_FALSE(tx.submit(port, bad, sizeof bad, true));
  TEST_ASSERT_FALSE(tx.submit(port, bad, 0, true));
  TEST_ASSERT_FALSE(tx.submit(port, nullptr, 1, true));
  TEST_ASSERT_FALSE(tx.submit(port, bad, kMaxEncoded + 1, true));
  TEST_ASSERT_EQUAL(4, tx.stats().invalid);
  TEST_ASSERT_EQUAL(0, port.calls);
}

void test_placement_initialization_does_not_require_zeroed_storage() {
  alignas(FrameWriter) uint8_t bytes[sizeof(FrameWriter)];
  memset(bytes, 0xA5, sizeof bytes);
  FrameWriter* tx = new (bytes) FrameWriter();
  const FrameTxStats stats = tx->stats();
  TEST_ASSERT_EQUAL(0, stats.pending);
  TEST_ASSERT_EQUAL(0, stats.telemetryDropped);
  TEST_ASSERT_EQUAL(0, stats.closedDropped);
  TEST_ASSERT_EQUAL(0, stats.overflow);
  TEST_ASSERT_EQUAL(0, stats.shortWrites);
  TEST_ASSERT_EQUAL(0, stats.invalid);
  TEST_ASSERT_TRUE(tx->roomForCritical());
  Port port;
  TEST_ASSERT_FALSE(tx->step(port));
  TEST_ASSERT_EQUAL(0, port.calls);
  tx->~FrameWriter();
}

void test_new_session_decoder_does_not_join_an_old_partial_request() {
  Decoder decoder;
  const auto old = packet(11), hello = packet(12);
  for (int i = 0; i < 3; ++i) TEST_ASSERT_FALSE(decoder.feed(old[i]));
  decoder = Decoder{};  // the desk integration does this on observed DTR falling edges only
  int frames = 0;
  for (uint8_t byte : hello) if (decoder.feed(byte)) ++frames;
  TEST_ASSERT_EQUAL(1, frames);
  TEST_ASSERT_EQUAL(12, decoder.seq());
  TEST_ASSERT_EQUAL(0, decoder.errors());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_closed_host_never_calls_sdk_write);
  RUN_TEST(test_reply_waits_until_entire_frame_fits_without_sdk_call);
  RUN_TEST(test_telemetry_cannot_fill_queue_or_overtake_a_reply);
  RUN_TEST(test_short_write_retains_tail_before_following_frame);
  RUN_TEST(test_zero_write_does_not_lose_or_duplicate_the_reply);
  RUN_TEST(test_full_queue_retains_replies_and_exposes_backpressure);
  RUN_TEST(test_disconnect_cancels_old_session_and_delimits_the_new_one);
  RUN_TEST(test_invalid_frame_is_reported_without_writing);
  RUN_TEST(test_placement_initialization_does_not_require_zeroed_storage);
  RUN_TEST(test_new_session_decoder_does_not_join_an_old_partial_request);
  return UNITY_END();
}
