#include <string>
#include <unity.h>
#include "stream_sched.h"
#include "stream_loader.h"

using namespace mt;

static std::string order;

struct C : StreamConsumer {
  char name;
  uint32_t res;
  uint32_t full = 200000;
  C(char n, uint32_t r) : name(n), res(r) {}
  bool needs(uint32_t& r) override {
    r = res;
    return res < full;
  }
  void load() override {
    order += name;
    res += 50000;
  }
};
struct B : BgWork {
  char name;
  bool work;
  B(char n, bool w) : name(n), work(w) {}
  bool step() override {
    if (work) order += name;
    return work;
  }
};

void setUp() { order.clear(); }
void tearDown() {}

void test_lowest_reserve_first_and_at_most_four_loads() {
  C a('a', 10000), b('b', 5000), c('c', 20000);
  StreamConsumer* cs[] = {&a, &b, &c};
  int next = 0;
  TEST_ASSERT_EQUAL(kPassLoads, schedulePass(cs, 3, nullptr, 0, next));
  TEST_ASSERT_EQUAL_STRING("bacb", order.c_str());
}

void test_background_only_when_every_reserve_is_large() {
  C a('a', 10000);
  B x('x', true);
  StreamConsumer* cs[] = {&a};
  BgWork* bg[] = {&x};
  int next = 0;
  schedulePass(cs, 1, bg, 1, next);  // a: 10 -> 60 ms ... after loads > 30 ms
  TEST_ASSERT_EQUAL_STRING("aaaax", order.c_str());
  order.clear();
  a.res = 1000;
  a.full = 2000;  // one load leaves it at 51 ms but it still "needs": reserve counts
  schedulePass(cs, 1, bg, 1, next);
  TEST_ASSERT_EQUAL_STRING("ax", order.c_str());
}

void test_background_waits_below_30_ms() {
  B x('x', true);
  BgWork* bg[] = {&x};
  int next = 0;
  struct Stuck : StreamConsumer {
    bool needs(uint32_t& r) override {
      r = 20000;
      return true;
    }
    void load() override {}
  } st;
  StreamConsumer* cs2[] = {&st};
  schedulePass(cs2, 1, bg, 1, next);
  TEST_ASSERT_EQUAL_STRING("", order.c_str());
}

void test_background_round_robin_skips_idle() {
  B x('x', true), y('y', false), z('z', true);
  BgWork* bg[] = {&x, &y, &z};
  int next = 0;
  for (int i = 0; i < 4; ++i) schedulePass(nullptr, 0, bg, 3, next);
  TEST_ASSERT_EQUAL_STRING("xzxz", order.c_str());
}

void test_full_high_pitch_prefetch_still_blocks_background_io() {
  static int16_t memory[kSlotBlocks * kStreamBlock];
  StreamPool pool;
  pool.init(memory, 1);
  SampleRegion region;
  region.to = region.loopHi = 100000;
  const int slot = pool.acquire(1, 100000, 44100, nullptr, 0, region, SlotOwner::Voice);
  pool.publish(slot, 0, int64_t(4) << 32);
  struct Reader : BlockReader {
    bool read(uint32_t, uint32_t, int n, int16_t* out) override {
      for (int i = 0; i < n * kStreamBlock; ++i) out[i] = 0;
      return true;
    }
  } reader;
  while (streamLoad(pool, slot, reader)) {}
  struct Voice : StreamConsumer {
    StreamPool& pool;
    int slot;
    Voice(StreamPool& p, int s) : pool(p), slot(s) {}
    bool needs(uint32_t& reserve) override {
      reserve = streamReserveUs(pool, slot);
      StreamLoad load;
      return streamPlan(pool, slot, load);
    }
    void load() override { TEST_FAIL_MESSAGE("a fully prefetched voice needs no load"); }
  } voice(pool, slot);
  StreamConsumer* consumers[] = {&voice};
  B work('x', true);
  BgWork* background[] = {&work};
  int next = 0;
  TEST_ASSERT_EQUAL(0, schedulePass(consumers, 1, background, 1, next));
  TEST_ASSERT_EQUAL_STRING("", order.c_str());  // 14.5 ms, below the 30 ms background threshold
  pool.publish(slot, 0, int64_t(1) << 32);
  TEST_ASSERT_EQUAL(0, schedulePass(consumers, 1, background, 1, next));
  TEST_ASSERT_EQUAL_STRING("x", order.c_str());  // the same cached frames last 58 ms at root
}

struct ReservedWork : B {
  uint32_t reserve;
  int calls = 0;
  ReservedWork(char name, uint32_t r, bool work = true) : B(name, work), reserve(r) {}
  uint32_t minReserveUs() const override { return reserve; }
  bool step() override { ++calls; return B::step(); }
};

void test_background_reserve_threshold_is_strict_for_each_job() {
  C audio('a', 50000); audio.full = 0;
  ReservedWork fs('f', 50000);
  StreamConsumer* cs[] = {&audio}; BgWork* bg[] = {&fs}; int next = 0;
  schedulePass(cs, 1, bg, 1, next);
  TEST_ASSERT_EQUAL_STRING("", order.c_str());
  TEST_ASSERT_EQUAL(0, fs.calls);
  ++audio.res;
  schedulePass(cs, 1, bg, 1, next);
  TEST_ASSERT_EQUAL_STRING("f", order.c_str());
  TEST_ASSERT_EQUAL(1, fs.calls);
}

void test_blocked_high_reserve_job_does_not_starve_legacy_jobs() {
  C audio('a', 40000); audio.full = 0;
  ReservedWork fs('f', 50000); B x('x', true), y('y', true);
  StreamConsumer* cs[] = {&audio}; BgWork* bg[] = {&fs, &x, &y}; int next = 0;
  for (int i = 0; i < 4; ++i) schedulePass(cs, 1, bg, 3, next);
  TEST_ASSERT_EQUAL_STRING("xyxy", order.c_str());
  TEST_ASSERT_EQUAL(0, fs.calls);
  order.clear(); audio.res = 60000;
  for (int i = 0; i < 6; ++i) schedulePass(cs, 1, bg, 3, next);
  TEST_ASSERT_EQUAL_STRING("fxyfxy", order.c_str());
}

void test_background_threshold_uses_reserve_after_fourth_load() {
  struct Audio : StreamConsumer {
    uint32_t reserve = 10000;
    bool needs(uint32_t& r) override { r = reserve; return true; }
    void load() override { reserve += 10000; order += 'a'; }
  } audio;
  ReservedWork fs('f', 50000);
  StreamConsumer* cs[] = {&audio}; BgWork* bg[] = {&fs}; int next = 0;
  TEST_ASSERT_EQUAL(kPassLoads, schedulePass(cs, 1, bg, 1, next));
  TEST_ASSERT_EQUAL_STRING("aaaa", order.c_str());
  TEST_ASSERT_EQUAL_UINT32(50000, audio.reserve);
  TEST_ASSERT_EQUAL(0, fs.calls);
}

void test_legacy_background_threshold_equality_is_still_blocked() {
  C audio('a', kBgReserveUs); audio.full = 0;
  B work('x', true);
  StreamConsumer* cs[] = {&audio}; BgWork* bg[] = {&work}; int next = 0;
  schedulePass(cs, 1, bg, 1, next);
  TEST_ASSERT_TRUE(order.empty());
  ++audio.res; schedulePass(cs, 1, bg, 1, next);
  TEST_ASSERT_EQUAL_STRING("x", order.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_background_reserve_threshold_is_strict_for_each_job);
  RUN_TEST(test_blocked_high_reserve_job_does_not_starve_legacy_jobs);
  RUN_TEST(test_background_threshold_uses_reserve_after_fourth_load);
  RUN_TEST(test_legacy_background_threshold_equality_is_still_blocked);
  RUN_TEST(test_lowest_reserve_first_and_at_most_four_loads);
  RUN_TEST(test_background_only_when_every_reserve_is_large);
  RUN_TEST(test_background_waits_below_30_ms);
  RUN_TEST(test_background_round_robin_skips_idle);
  RUN_TEST(test_full_high_pitch_prefetch_still_blocks_background_io);
  return UNITY_END();
}
