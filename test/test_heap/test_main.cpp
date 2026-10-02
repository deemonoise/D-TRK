#include <unity.h>
#include "event_heap.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static SchedEvent ev(uint64_t t, uint8_t status, uint8_t note = 60) {
  SchedEvent e{};
  e.t = t;
  e.b[0] = status;
  e.b[1] = note;
  e.len = 3;
  return e;
}

static EventHeap heap;  // too big for the stack

void test_pops_in_time_order() {
  heap.clear();
  const uint64_t ts[] = {500, 100, 900, 300, 700, 200, 800, 400, 600};
  for (uint64_t t : ts) TEST_ASSERT_TRUE(heap.push(ev(t, 0x90)));
  uint64_t prev = 0;
  int n = 0;
  while (!heap.empty()) {
    TEST_ASSERT_TRUE(heap.top().t >= prev);
    prev = heap.top().t;
    heap.pop();
    ++n;
  }
  TEST_ASSERT_EQUAL(9, n);
}

void test_note_off_before_note_on_at_same_time() {
  heap.clear();
  heap.push(ev(100, 0x90));
  heap.push(ev(100, 0x80));
  TEST_ASSERT_TRUE(heap.top().isNoteOff());
}

void test_note_on_rejected_when_reserve_reached_but_off_accepted() {
  heap.clear();
  const int onLimit = EventHeap::kCap - EventHeap::kOffReserve;
  for (int i = 0; i < onLimit; ++i) TEST_ASSERT_TRUE(heap.push(ev(i, 0x90)));
  TEST_ASSERT_FALSE(heap.push(ev(1, 0x90)));
  for (int i = 0; i < EventHeap::kOffReserve; ++i) TEST_ASSERT_TRUE(heap.push(ev(i, 0x80)));
  TEST_ASSERT_FALSE(heap.push(ev(1, 0x80)));
  TEST_ASSERT_EQUAL(EventHeap::kCap, heap.size());
}

void test_remove_if_keeps_heap_order() {
  heap.clear();
  for (int i = 0; i < 40; ++i) {
    SchedEvent e = ev(static_cast<uint64_t>((i * 37) % 40), 0x90);
    e.tag = i % 3;
    heap.push(e);
  }
  heap.removeIf([](const SchedEvent& e) { return e.tag == 1; });
  int n = 0;
  uint64_t prev = 0;
  while (!heap.empty()) {
    TEST_ASSERT_TRUE(heap.top().tag != 1);
    TEST_ASSERT_TRUE(heap.top().t >= prev);
    prev = heap.top().t;
    heap.pop();
    ++n;
  }
  TEST_ASSERT_EQUAL(27, n);
}

static int rank(uint8_t status) {
  switch (status & 0xF0) {
    case 0x80: return 0;
    case 0x90: return 2;
    default: return 1;
  }
}

void test_same_time_order_off_control_on() {
  const uint8_t st[] = {0x90, 0xB0, 0x80, 0xC0, 0xE0, 0x91, 0xB1, 0x81};
  // Every rotation of the push order pops by rank.
  for (int r = 0; r < 8; ++r) {
    heap.clear();
    for (int i = 0; i < 8; ++i) heap.push(ev(100, st[(i + r) % 8]));
    int prev = 0;
    while (!heap.empty()) {
      const int k = rank(heap.top().b[0]);
      TEST_ASSERT_TRUE(k >= prev);
      prev = k;
      heap.pop();
    }
  }
}

void test_same_time_same_rank_is_fifo() {
  heap.clear();
  for (int i = 0; i < 20; ++i) {
    heap.push(ev(100, 0xC0, static_cast<uint8_t>(i)));
    heap.push(ev(50 + i, 0x90));  // shuffle the heap
  }
  int next = 0;
  while (!heap.empty()) {
    if (heap.top().b[0] == 0xC0) TEST_ASSERT_EQUAL(next++, heap.top().b[1]);
    heap.pop();
  }
  TEST_ASSERT_EQUAL(20, next);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pops_in_time_order);
  RUN_TEST(test_note_off_before_note_on_at_same_time);
  RUN_TEST(test_note_on_rejected_when_reserve_reached_but_off_accepted);
  RUN_TEST(test_remove_if_keeps_heap_order);
  RUN_TEST(test_same_time_order_off_control_on);
  RUN_TEST(test_same_time_same_rank_is_fifo);
  return UNITY_END();
}
