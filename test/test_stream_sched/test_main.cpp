#include <string>
#include <unity.h>
#include "stream_sched.h"

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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lowest_reserve_first_and_at_most_four_loads);
  RUN_TEST(test_background_only_when_every_reserve_is_large);
  RUN_TEST(test_background_waits_below_30_ms);
  RUN_TEST(test_background_round_robin_skips_idle);
  return UNITY_END();
}
