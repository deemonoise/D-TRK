#include <string.h>
#include <unity.h>
#include <vector>
#include "stream_loader.h"

using namespace mt;

static int16_t mem[kStreamSlots * kSlotBlocks * kStreamBlock];
static StreamPool pool;

struct VecReader : BlockReader {
  std::vector<int16_t> d;
  int reads = 0;
  bool read(uint32_t, uint32_t b, int n, int16_t* out) override {
    ++reads;
    for (int i = 0; i < n * kStreamBlock; ++i) {
      const uint32_t f = b * kStreamBlock + i;
      out[i] = f < d.size() ? d[f] : 0;
    }
    return true;
  }
};
static VecReader rd;

static PlaySnap snap(uint32_t pos, int32_t step16, uint32_t lo, uint32_t hi, LoopMode mode) {
  PlaySnap p;
  p.pos = pos;
  p.step16 = step16;
  p.from = p.loopLo = lo;
  p.to = p.loopHi = hi;
  p.loopMode = static_cast<uint8_t>(mode);
  return p;
}
static void expectWant(const PlaySnap& p, std::vector<uint32_t> want) {
  uint32_t got[kAhead + 1];
  const int n = streamWant(p, got);
  TEST_ASSERT_EQUAL(static_cast<int>(want.size()), n);
  for (int i = 0; i < n; ++i) TEST_ASSERT_EQUAL_UINT32(want[i], got[i]);
}
static int voice(uint32_t frames, uint32_t start) {
  SampleRegion g;
  g.from = g.loopLo = 0;
  g.to = g.loopHi = frames;
  g.pos = static_cast<int64_t>(start) << 32;
  return pool.acquire(1, frames, 44100, nullptr, 0, g, SlotOwner::Voice);
}

void setUp() {
  pool.init(mem);
  rd.d.assign(100000, 0);
  for (uint32_t i = 0; i < rd.d.size(); ++i) rd.d[i] = static_cast<int16_t>(i);
  rd.reads = 0;
}
void tearDown() {}

void test_want_forward() { expectWant(snap(1000, 65536, 0, 100000, LoopMode::Off), {1, 2, 3, 4, 5}); }
void test_want_stops_at_end_without_loop() { expectWant(snap(1000, 65536, 0, 1500, LoopMode::Off), {1, 2}); }
void test_want_reverse() { expectWant(snap(3000, -65536, 0, 100000, LoopMode::Off), {5, 4, 3, 2, 1}); }
void test_want_forward_loop_wraps() { expectWant(snap(1900, 65536, 0, 2048, LoopMode::Forward), {3, 0, 1, 2}); }
void test_want_pingpong_turns() { expectWant(snap(3000, 65536, 0, 3072, LoopMode::PingPong), {5, 4, 3, 2, 1}); }

void test_plan_reads_consecutive_missing_blocks() {
  const int s = voice(100000, 0);
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(0, l.block);
  TEST_ASSERT_EQUAL(kLoadMax, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(4, l.block);
  TEST_ASSERT_EQUAL(1, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_FALSE(streamPlan(pool, s, l));  // all wanted blocks in
  TEST_ASSERT_NULL(pool.block(s, 5));  // not wanted yet
}

void test_plan_evicts_blocks_behind() {
  const int s = voice(100000, 0);
  while (streamLoad(pool, s, rd)) {
  }
  pool.publish(s, int64_t(2600) << 32, int64_t(1) << 32);  // block 5
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(5, l.block);
  TEST_ASSERT_EQUAL(kLoadMax, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_NULL(pool.block(s, 0));  // behind: evicted (any unwanted way may go, not only the farthest)
  const int16_t* d = pool.block(s, 7);
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_EQUAL_INT16(7 * 512 + 3, d[3]);
}

void test_reserve_counts_loaded_frames_ahead() {
  const int s = voice(100000, 0);
  TEST_ASSERT_EQUAL_UINT32(0, streamReserveUs(pool, s));
  streamLoad(pool, s, rd);  // blocks 0..3: 2048 frames at 1.0 = 46.44 ms
  TEST_ASSERT_UINT32_WITHIN(50, 46440, streamReserveUs(pool, s));
  pool.publish(s, 0, int64_t(2) << 32);  // an octave up: half the time
  TEST_ASSERT_UINT32_WITHIN(50, 23220, streamReserveUs(pool, s));
}

void test_heads_count_as_loaded() {
  static int16_t h[1024];
  static SampleHead head;
  head.frame = 0;
  head.len = 1024;
  head.d = h;
  SampleRegion g;
  g.to = g.loopHi = 100000;
  const int s = pool.acquire(1, 100000, 44100, &head, 1, g, SlotOwner::Voice);
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(2, l.block);  // blocks 0, 1 are in the head
  TEST_ASSERT_UINT32_WITHIN(50, 23220, streamReserveUs(pool, s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_want_forward);
  RUN_TEST(test_want_stops_at_end_without_loop);
  RUN_TEST(test_want_reverse);
  RUN_TEST(test_want_forward_loop_wraps);
  RUN_TEST(test_want_pingpong_turns);
  RUN_TEST(test_plan_reads_consecutive_missing_blocks);
  RUN_TEST(test_plan_evicts_blocks_behind);
  RUN_TEST(test_reserve_counts_loaded_frames_ahead);
  RUN_TEST(test_heads_count_as_loaded);
  return UNITY_END();
}
