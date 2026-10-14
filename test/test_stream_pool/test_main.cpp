#include <string.h>
#include <unity.h>
#include "stream_pool.h"

using namespace mt;

static int16_t mem[kStreamSlots * kSlotBlocks * kStreamBlock];
static StreamPool pool;
static int16_t data[kLoadMax * kStreamBlock];

static SampleRegion reg(uint32_t start) {
  SampleRegion g;
  g.from = g.loopLo = 0;
  g.to = g.loopHi = 100000;
  g.pos = static_cast<int64_t>(start) << 32;
  return g;
}
static int get(uint32_t key, SlotOwner o = SlotOwner::Voice, uint32_t start = 0) {
  return pool.acquire(key, 100000, 44100, nullptr, 0, reg(start), o);
}
// Loads block b of slot s into way 0, as the loader would.
static bool load(int s, uint32_t b) {
  StreamLoad l;
  l.slot = s;
  l.key = pool.slot(s).key;
  l.gen = pool.slot(s).gen;
  l.block = b;
  l.n = 1;
  l.way[0] = 0;
  for (int i = 0; i < kStreamBlock; ++i) data[i] = static_cast<int16_t>(b * 1000 + i);
  return pool.commit(l, data);
}

void setUp() {
  memset(mem, 0, sizeof mem);
  pool.init(mem);
}
void tearDown() {}

void test_acquire_gives_distinct_slots_until_full() {
  bool used[kStreamSlots] = {};
  for (int i = 0; i < kStreamSlots; ++i) {
    const int s = get(100 + i);
    TEST_ASSERT_TRUE(s >= 0 && s < kStreamSlots);
    TEST_ASSERT_FALSE(used[s]);
    used[s] = true;
  }
  TEST_ASSERT_EQUAL(-1, get(999));
}

void test_loaded_block_is_found() {
  const int s = get(7);
  TEST_ASSERT_NULL(pool.block(s, 3));
  TEST_ASSERT_TRUE(load(s, 3));
  const int16_t* d = pool.block(s, 3);
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_EQUAL_INT16(3000, d[0]);
  TEST_ASSERT_EQUAL_INT16(3511, d[511]);
}

void test_released_slot_keeps_blocks_for_same_key() {
  const int s = get(7);
  TEST_ASSERT_TRUE(load(s, 0));
  pool.release(s);
  TEST_ASSERT_EQUAL(s, get(7));
  TEST_ASSERT_NOT_NULL(pool.block(s, 0));
}

void test_new_key_drops_blocks() {
  int s7 = -1;
  for (int i = 0; i < kStreamSlots; ++i) {
    const int s = get(1 + i);
    if (i == 0) s7 = s;
  }
  TEST_ASSERT_TRUE(load(s7, 0));
  pool.release(s7);
  TEST_ASSERT_EQUAL(s7, get(99));  // the only free slot
  TEST_ASSERT_NULL(pool.block(s7, 0));
}

void test_prefetch_slot_is_adopted() {
  const int s = get(5, SlotOwner::Prefetch);
  TEST_ASSERT_TRUE(load(s, 0));
  TEST_ASSERT_EQUAL(s, get(5));
  TEST_ASSERT_EQUAL(SlotOwner::Voice, pool.slot(s).owner);
  TEST_ASSERT_NOT_NULL(pool.block(s, 0));
}

void test_prefetch_evicted_when_full() {
  for (int i = 0; i < kStreamSlots - 1; ++i) get(1 + i);
  const int p = get(500, SlotOwner::Prefetch);
  TEST_ASSERT_TRUE(p >= 0);
  TEST_ASSERT_EQUAL(p, get(600));
  TEST_ASSERT_EQUAL_UINT32(600, pool.slot(p).key);
}

void test_preview_never_evicted() {
  for (int i = 0; i < kStreamSlots - 1; ++i) get(1 + i);
  TEST_ASSERT_TRUE(get(kPreviewKey, SlotOwner::Preview) >= 0);
  TEST_ASSERT_EQUAL(-1, get(600));
}

void test_commit_after_rekey_is_dropped() {
  int s0 = -1;
  for (int i = 0; i < kStreamSlots; ++i) {
    const int s = get(1 + i);
    if (i == 0) s0 = s;
  }
  StreamLoad l;
  l.slot = s0;
  l.key = pool.slot(s0).key;
  l.gen = pool.slot(s0).gen;
  l.block = 0;
  l.n = 1;
  l.way[0] = 0;
  pool.release(s0);
  TEST_ASSERT_EQUAL(s0, get(77));  // re-keyed while the card was read
  TEST_ASSERT_FALSE(pool.commit(l, data));
  TEST_ASSERT_NULL(pool.block(s0, 0));
}

void test_snapshot_round_trip() {
  const int s = get(9);
  pool.publish(s, int64_t(1234) << 32, -(int64_t(3) << 31));  // 1.5 frames per sample, backwards
  PlaySnap p;
  TEST_ASSERT_TRUE(pool.snap(s, p));
  TEST_ASSERT_EQUAL_UINT32(1234, p.pos);
  TEST_ASSERT_EQUAL_INT32(-98304, p.step16);
}

void test_stale_prefetch_is_released() {
  const int a = get(5, SlotOwner::Prefetch);
  const int b = get(6, SlotOwner::Prefetch);
  const int v = get(7);
  pool.slotRef(a).stamp = 1000;
  pool.slotRef(b).stamp = 1900;
  pool.slotRef(v).stamp = 0;
  TEST_ASSERT_EQUAL(1, pool.expirePrefetch(2100, 1000));  // a: 1100 ms without its note
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Free), static_cast<int>(pool.slot(a).owner));
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Prefetch), static_cast<int>(pool.slot(b).owner));
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Voice), static_cast<int>(pool.slot(v).owner));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_acquire_gives_distinct_slots_until_full);
  RUN_TEST(test_loaded_block_is_found);
  RUN_TEST(test_released_slot_keeps_blocks_for_same_key);
  RUN_TEST(test_new_key_drops_blocks);
  RUN_TEST(test_prefetch_slot_is_adopted);
  RUN_TEST(test_prefetch_evicted_when_full);
  RUN_TEST(test_preview_never_evicted);
  RUN_TEST(test_commit_after_rekey_is_dropped);
  RUN_TEST(test_snapshot_round_trip);
  RUN_TEST(test_stale_prefetch_is_released);
  return UNITY_END();
}
