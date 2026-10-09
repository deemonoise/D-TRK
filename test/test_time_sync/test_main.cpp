#include <unity.h>
#include <stdlib.h>
#include <initializer_list>
#include "time_sync.h"

using namespace mt::link;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint64_t kEspStart = 7000000;     // ESP up for 7 s
constexpr uint64_t kLocalStart = 123456789;  // Teensy up longer
constexpr uint32_t kPeriod = 50000;          // Time every 50 ms

int64_t err(const TimeSync& s, uint64_t espUs, uint64_t trueLocal) {
  return static_cast<int64_t>(s.toLocal(espUs)) - static_cast<int64_t>(trueLocal);
}

}  // namespace

void test_invalid_before_first_sample() {
  TimeSync s;
  TEST_ASSERT_FALSE(s.valid());
  s.sample(1000, 5000);
  TEST_ASSERT_TRUE(s.valid());
  TEST_ASSERT_TRUE(s.toLocal(2000) == 6000);
  s.reset();
  TEST_ASSERT_FALSE(s.valid());
}

// Worst error of the offset over 20 s of Time samples with a fixed 300 us delay plus jitter().
template <class J>
int64_t worstWithJitter(J jitter) {
  TimeSync s;
  int64_t worst = 0;
  for (int i = 0; i < 400; ++i) {
    uint64_t esp = kEspStart + static_cast<uint64_t>(i) * kPeriod;
    s.sample(esp, kLocalStart + (esp - kEspStart) + 300 + jitter());
    if (i < 40) continue;  // one window to settle
    // Truth: the event sent at esp arrives with the fixed 300 us only.
    int64_t e = err(s, esp, kLocalStart + (esp - kEspStart) + 300);
    if (llabs(e) > worst) worst = llabs(e);
  }
  return worst;
}

uint32_t lcg(uint32_t& seed) { return seed = seed * 1664525u + 1013904223u; }

// Link-like: mostly a short wait, sometimes behind a bulk frame (up to 2 ms).
void test_constant_delay_with_link_jitter() {
  uint32_t seed = 1;
  int64_t worst = worstWithJitter([&] {
    uint32_t r = lcg(seed);
    return (r >> 8) % 10 < 3 ? lcg(seed) % 2001 : lcg(seed) % 101;
  });
  TEST_ASSERT_LESS_THAN(50, static_cast<int>(worst));
}

// Uniform 0..2 ms on every sample: the window minimum still stays within ~10 samples.
void test_constant_delay_with_uniform_jitter() {
  uint32_t seed = 1;
  int64_t worst = worstWithJitter([&] { return lcg(seed) % 2001; });
  TEST_ASSERT_LESS_THAN(250, static_cast<int>(worst));
}

void test_drift_both_ways() {
  for (int sign : {1, -1}) {
    TimeSync s;
    int64_t worst = 0;
    for (int i = 0; i < 1200; ++i) {  // 60 s
      uint64_t esp = kEspStart + static_cast<uint64_t>(i) * kPeriod;
      uint64_t el = esp - kEspStart;
      uint64_t local = kLocalStart + el + sign * static_cast<int64_t>(el / 20000);  // 50 ppm
      s.sample(esp, local + 300);
      int64_t e = err(s, esp, local + 300);
      if (llabs(e) > worst) worst = llabs(e);
    }
    TEST_ASSERT_LESS_THAN(100, static_cast<int>(worst));
  }
}

void test_one_late_sample_ignored() {
  TimeSync s;
  for (int i = 0; i < 40; ++i) s.sample(kEspStart + i * kPeriod, kLocalStart + i * kPeriod + 300);
  uint64_t before = s.toLocal(kEspStart);
  s.sample(kEspStart + 40 * kPeriod, kLocalStart + 40 * kPeriod + 20300);
  TEST_ASSERT_TRUE(s.toLocal(kEspStart) == before);
}

void test_window_forgets_old_minimum() {
  TimeSync s;
  s.sample(kEspStart, kLocalStart);  // a lucky early sample
  for (int i = 1; i <= 60; ++i) s.sample(kEspStart + i * kPeriod, kLocalStart + i * kPeriod + 1000);
  TEST_ASSERT_TRUE(s.toLocal(kEspStart) == kLocalStart + 1000);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_invalid_before_first_sample);
  RUN_TEST(test_constant_delay_with_link_jitter);
  RUN_TEST(test_constant_delay_with_uniform_jitter);
  RUN_TEST(test_drift_both_ways);
  RUN_TEST(test_one_late_sample_ignored);
  RUN_TEST(test_window_forgets_old_minimum);
  return UNITY_END();
}
