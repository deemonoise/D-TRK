#include <unity.h>
#include <string.h>
#include <vector>
#include "state_mirror.h"

using namespace mt;
using namespace mt::link;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t* bytesOf(const SynthModel& m, uint16_t chunk) {
  uint32_t off, len;
  chunkRange(chunk, off, len);
  return reinterpret_cast<const uint8_t*>(&m) + off;
}

// Sends and acks every dirty chunk of live; the ids in order.
std::vector<int> drain(StateMirror& s, const SynthModel& live) {
  std::vector<int> ids;
  for (int c; (c = s.nextDirty(live)) >= 0;) {
    ids.push_back(c);
    s.acked(static_cast<uint16_t>(c), bytesOf(live, static_cast<uint16_t>(c)));
  }
  return ids;
}

Project live;  // large: keep off the stack

}  // namespace

void test_fresh_mirror_sends_everything_in_order() {
  StateMirror s;
  live = Project();
  TEST_ASSERT_FALSE(s.synced(live));
  std::vector<int> ids = drain(s, live);
  TEST_ASSERT_EQUAL(kChunks, static_cast<int>(ids.size()));
  for (int i = 0; i < kChunks; ++i) TEST_ASSERT_EQUAL(chunkId(i), ids[i]);
  TEST_ASSERT_TRUE(s.synced(live));
  TEST_ASSERT_EQUAL(-1, s.nextDirty(live));
}

void test_two_edits_two_chunks() {
  StateMirror s;
  live = Project();
  drain(s, live);
  live.instruments[3].vol = 1;
  live.instruments[20].vol = 2;
  std::vector<int> ids = drain(s, live);
  TEST_ASSERT_EQUAL(2, static_cast<int>(ids.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Instr0) + 3, ids[0]);
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Instr0) + 20, ids[1]);
  TEST_ASSERT_TRUE(s.synced(live));
}

void test_invalidate_dirties_all() {
  StateMirror s;
  live = Project();
  drain(s, live);
  s.invalidate();
  TEST_ASSERT_FALSE(s.synced(live));
  TEST_ASSERT_EQUAL(kChunks, static_cast<int>(drain(s, live).size()));
}

void test_in_flight_not_resent_and_failed_retries() {
  StateMirror s;
  live = Project();
  drain(s, live);
  live.bpm = 99;
  int c = s.nextDirty(live);
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Master), c);
  TEST_ASSERT_EQUAL(-1, s.nextDirty(live));
  TEST_ASSERT_TRUE(s.inFlight());
  TEST_ASSERT_FALSE(s.synced(live));
  s.failed(static_cast<uint16_t>(c));
  TEST_ASSERT_FALSE(s.inFlight());
  TEST_ASSERT_EQUAL(c, s.nextDirty(live));
}

void test_edit_during_flight_stays_dirty() {
  StateMirror s;
  live = Project();
  drain(s, live);
  live.instruments[7].vol = 10;
  const uint16_t c = static_cast<uint16_t>(s.nextDirty(live));
  uint32_t off, len;
  chunkRange(c, off, len);
  std::vector<uint8_t> sent(bytesOf(live, c), bytesOf(live, c) + len);
  live.instruments[7].vol = 11;  // edited while the old bytes travel
  s.acked(c, sent.data());
  TEST_ASSERT_FALSE(s.synced(live));
  TEST_ASSERT_EQUAL(c, s.nextDirty(live));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_mirror_sends_everything_in_order);
  RUN_TEST(test_two_edits_two_chunks);
  RUN_TEST(test_invalidate_dirties_all);
  RUN_TEST(test_in_flight_not_resent_and_failed_retries);
  RUN_TEST(test_edit_during_flight_stays_dirty);
  return UNITY_END();
}
