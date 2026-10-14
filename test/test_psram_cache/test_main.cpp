#include <string.h>
#include <unity.h>
#include <vector>
#include "psram_cache.h"
#include "sample_set.h"
#include "sample_store.h"
#include "stream_pool.h"

using namespace mt;

// RAM in place of the chip: no cache, flush / drop only count, unless a fault is set.
struct FakeHw : PsramHw {
  uint32_t* mem = nullptr;
  int aliasBit = -1;  // address bit stuck low: word 1 << bit reads what word 0 holds
  bool flip = false;  // the next drop flips bit 0 of the first dropped word
  int flushes = 0, drops = 0;
  void flush(const void*, uint32_t) override { ++flushes; }
  void drop(const void* p, uint32_t bytes) override {
    ++drops;
    if (aliasBit >= 0) mem[1u << aliasBit] = mem[0];
    if (flip && bytes) {
      flip = false;
      *static_cast<uint32_t*>(const_cast<void*>(p)) ^= 1;
    }
  }
};

static std::vector<uint32_t> chip;
static FakeHw hw;

void setUp() {
  chip.assign(256 * 1024, 0);  // 1 MB stands for the chip
  hw = FakeHw();
  hw.mem = chip.data();
}
void tearDown() {}

void test_arena_aligns_and_fills() {
  PsramArena a;
  a.init(chip.data(), 1000);
  uint8_t* base = reinterpret_cast<uint8_t*>(chip.data());
  TEST_ASSERT_EQUAL_PTR(base, a.alloc(10));
  TEST_ASSERT_EQUAL_PTR(base + 32, a.alloc(40));  // next cache line
  TEST_ASSERT_EQUAL_UINT32(72, a.used());
  TEST_ASSERT_NULL(a.alloc(1000 - 96 + 1));
  TEST_ASSERT_EQUAL_PTR(base + 96, a.alloc(1000 - 96));
  TEST_ASSERT_EQUAL_UINT32(1000, a.used());
  TEST_ASSERT_NULL(a.alloc(1));
  a.reset();
  TEST_ASSERT_EQUAL_UINT32(0, a.used());
  TEST_ASSERT_EQUAL_PTR(base, a.alloc(1));
}

void test_arena_reserve_comes_first_and_survives_reset() {
  PsramArena a;
  a.init(chip.data(), 1000);
  uint8_t* base = reinterpret_cast<uint8_t*>(chip.data());
  TEST_ASSERT_EQUAL_PTR(base, a.reserve(100));      // the PERF buffer: the arena's start
  TEST_ASSERT_EQUAL_UINT32(1000 - 128, a.size());   // the rest, from the next cache line
  TEST_ASSERT_EQUAL_UINT32(0, a.used());
  TEST_ASSERT_EQUAL_PTR(base + 128, a.alloc(10));
  TEST_ASSERT_NULL(a.reserve(10));                  // only before the first alloc
  a.reset();
  TEST_ASSERT_EQUAL_PTR(base + 128, a.alloc(1));    // the reserve stays
  PsramArena b;
  b.init(chip.data(), 1000);
  TEST_ASSERT_NULL(b.reserve(1001));
  TEST_ASSERT_EQUAL_UINT32(1000, b.size());
}

void test_selftest_passes_on_good_memory() {
  TEST_ASSERT_TRUE(psramSelfTest(chip.data(), chip.size() * 4, hw));
  TEST_ASSERT_TRUE(hw.flushes > 0);
  TEST_ASSERT_TRUE(hw.drops > 0);  // read back past the cache
}

void test_selftest_finds_stuck_address_line() {
  hw.aliasBit = 17;  // word 128 K = byte 512 K
  TEST_ASSERT_FALSE(psramSelfTest(chip.data(), chip.size() * 4, hw));
}

void test_selftest_finds_bad_bit() {
  hw.flip = true;
  TEST_ASSERT_FALSE(psramSelfTest(chip.data(), chip.size() * 4, hw));
}

void test_watch_every_100ms_catches_a_flip_and_stays_failed() {
  PsramWatch w;
  w.init(chip.data() + 1024, hw);  // a 4 KB guard
  TEST_ASSERT_TRUE(w.step(0));     // the first call checks
  const int d = hw.drops;
  TEST_ASSERT_TRUE(w.step(50));
  TEST_ASSERT_EQUAL(d, hw.drops);  // not due: the chip is not touched
  TEST_ASSERT_TRUE(w.step(100));
  TEST_ASSERT_TRUE(hw.drops > d);
  hw.flip = true;
  TEST_ASSERT_FALSE(w.step(200));
  TEST_ASSERT_TRUE(w.failed());
  const int d2 = hw.drops;
  TEST_ASSERT_FALSE(w.step(10000));  // back only after a reboot
  TEST_ASSERT_EQUAL(d2, hw.drops);
}

void test_watch_pattern_changes_every_check() {
  PsramWatch w;
  uint32_t* g = chip.data() + 1024;
  w.init(g, hw);
  w.step(0);
  const uint32_t a = g[0];
  w.step(100);
  TEST_ASSERT_NOT_EQUAL(a, g[0]);  // a stale word cannot pass for a fresh one
}

// Store files by crc, served block by block like the card.
struct Files : BlockReader {
  std::vector<std::vector<int16_t>> d;
  std::vector<uint32_t> crc;
  int reads = 0;
  bool fail = false;
  uint32_t add(uint32_t frames, int seed) {
    std::vector<int16_t> v(frames);
    for (uint32_t i = 0; i < frames; ++i) v[i] = static_cast<int16_t>(i * 7 + seed * 1000);
    d.push_back(v);
    crc.push_back(sampleCrc(v.data(), frames));
    return crc.back();
  }
  bool read(uint32_t key, uint32_t block, int n, int16_t* out) override {
    ++reads;
    if (fail) return false;
    for (size_t k = 0; k < crc.size(); ++k)
      if (crc[k] == key) {
        for (int i = 0; i < n * kStreamBlock; ++i) {
          const uint32_t f = block * kStreamBlock + i;
          out[i] = f < d[k].size() ? d[k][f] : 0;
        }
        return true;
      }
    return false;
  }
};

static StoreTable table;
static StreamPool fpool;
static Files files;
static PsramArena arena;

// Store entries for the files (sector 1: contiguous), the fill over the 1 MB fake chip.
static void prepare(uint32_t arenaBytes) {
  table.clear();
  for (size_t k = 0; k < files.crc.size(); ++k)
    table.add(files.crc[k], static_cast<uint32_t>(files.d[k].size()), 44100, 1);
  fpool.psramOk = true;
  arena.init(chip.data(), arenaBytes);
}
static int run(PsramFill& f) {
  int n = 0;
  while (f.step()) ++n;
  return n;
}
static const int16_t* copyOf(uint32_t crc) { return table.at(table.find(crc)).psram; }

void test_fill_order_shortest_first_until_full() {
  const uint32_t frames[] = {5000, 1000, 3000, 1000};
  int order[4];
  TEST_ASSERT_EQUAL_UINT32(2048, psramBytes(1000));  // whole blocks
  TEST_ASSERT_EQUAL(3, psramFillOrder(frames, 4, 2048 + 2048 + 6144, order));
  TEST_ASSERT_EQUAL(1, order[0]);
  TEST_ASSERT_EQUAL(3, order[1]);  // equal lengths keep their order
  TEST_ASSERT_EQUAL(2, order[2]);
}

void test_fill_copies_shortest_first_and_verifies() {
  files = Files();
  const uint32_t a = files.add(5000, 1), b = files.add(1000, 2), c = files.add(3000, 3);
  prepare(1u << 20);
  PsramFill f(table, fpool, files, arena, hw);
  const uint32_t list[] = {a, b, c, b};  // b twice: one copy
  f.restart(list, 4);
  TEST_ASSERT_EQUAL(3, f.planned());
  TEST_ASSERT_NULL(copyOf(b));  // nothing before the crc check
  run(f);
  TEST_ASSERT_EQUAL(3, f.ready());
  TEST_ASSERT_EQUAL(0, f.bad());
  TEST_ASSERT_TRUE(copyOf(b) < copyOf(c) && copyOf(c) < copyOf(a));
  TEST_ASSERT_EQUAL_INT16_ARRAY(files.d[0].data(), copyOf(a), 5000);
  TEST_ASSERT_EQUAL_UINT32(f.plannedBytes(), f.doneBytes());
  TEST_ASSERT_TRUE(hw.flushes > 0);
}

void test_fill_pct_reaches_100_only_after_the_last_check() {
  files = Files();
  const uint32_t a = files.add(1000, 1), b = files.add(1000, 2);  // 2 blocks each, 1 copy step
  prepare(1u << 20);
  PsramFill f(table, fpool, files, arena, hw);
  TEST_ASSERT_EQUAL_UINT8(100, f.pct());  // nothing planned
  const uint32_t list[] = {a, b};
  f.restart(list, 2);
  TEST_ASSERT_EQUAL_UINT8(0, f.pct());
  f.step();  // a copied, its crc not checked yet
  const uint8_t p1 = f.pct();
  TEST_ASSERT_TRUE(p1 > 0 && p1 < 50);
  f.step();  // a checked: half of the plan done
  TEST_ASSERT_EQUAL_UINT8(50, f.pct());
  f.step();  // b copied: all bytes in, the check still to come
  TEST_ASSERT_TRUE(f.pct() > 50 && f.pct() < 100);
  f.step();  // b checked
  TEST_ASSERT_EQUAL_UINT8(100, f.pct());
  TEST_ASSERT_EQUAL(2, f.ready());
}

void test_fill_stops_at_capacity() {
  files = Files();
  const uint32_t a = files.add(5000, 1), b = files.add(1000, 2), c = files.add(3000, 3);
  prepare(9000);  // 2048 + 6144 fit, 10240 does not
  PsramFill f(table, fpool, files, arena, hw);
  const uint32_t list[] = {a, b, c};
  f.restart(list, 3);
  run(f);
  TEST_ASSERT_EQUAL(2, f.ready());
  TEST_ASSERT_NOT_NULL(copyOf(b));
  TEST_ASSERT_NOT_NULL(copyOf(c));
  TEST_ASSERT_NULL(copyOf(a));
}

void test_fill_rejects_a_copy_with_a_wrong_crc() {
  files = Files();
  const uint32_t a = files.add(3000, 1);
  files.d[0][2000] ^= 0x100;  // the file does not match its crc
  prepare(1u << 20);
  PsramFill f(table, fpool, files, arena, hw);
  f.restart(&a, 1);
  run(f);
  TEST_ASSERT_EQUAL(0, f.ready());
  TEST_ASSERT_EQUAL(1, f.bad());
  TEST_ASSERT_NULL(copyOf(a));
}

void test_fill_skips_unreadable_and_scattered_files() {
  files = Files();
  const uint32_t a = files.add(3000, 1), b = files.add(2000, 2);
  prepare(1u << 20);
  table.clear();
  table.add(a, 3000, 44100, 0);  // not contiguous: no block reads
  table.add(b, 2000, 44100, 1);
  PsramFill f(table, fpool, files, arena, hw);
  const uint32_t list[] = {a, b};
  f.restart(list, 2);
  TEST_ASSERT_EQUAL(1, f.planned());
  files.fail = true;  // the card went away
  run(f);
  TEST_ASSERT_EQUAL(0, f.ready());
  TEST_ASSERT_EQUAL(1, f.bad());
  TEST_ASSERT_NULL(copyOf(b));
}

void test_fill_idles_without_psram_and_restart_drops_copies() {
  files = Files();
  const uint32_t a = files.add(3000, 1);
  prepare(1u << 20);
  PsramFill f(table, fpool, files, arena, hw);
  f.restart(&a, 1);
  run(f);
  TEST_ASSERT_NOT_NULL(copyOf(a));
  f.restart(&a, 1);  // a new project load: copies go, the arena starts over
  TEST_ASSERT_NULL(copyOf(a));
  TEST_ASSERT_EQUAL_UINT32(0, arena.used());
  fpool.psramOk = false;  // the watchdog failed
  const int r = files.reads;
  TEST_ASSERT_FALSE(f.step());
  TEST_ASSERT_EQUAL(r, files.reads);
  TEST_ASSERT_NULL(copyOf(a));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_arena_aligns_and_fills);
  RUN_TEST(test_arena_reserve_comes_first_and_survives_reset);
  RUN_TEST(test_selftest_passes_on_good_memory);
  RUN_TEST(test_selftest_finds_stuck_address_line);
  RUN_TEST(test_selftest_finds_bad_bit);
  RUN_TEST(test_watch_every_100ms_catches_a_flip_and_stays_failed);
  RUN_TEST(test_watch_pattern_changes_every_check);
  RUN_TEST(test_fill_order_shortest_first_until_full);
  RUN_TEST(test_fill_copies_shortest_first_and_verifies);
  RUN_TEST(test_fill_pct_reaches_100_only_after_the_last_check);
  RUN_TEST(test_fill_stops_at_capacity);
  RUN_TEST(test_fill_rejects_a_copy_with_a_wrong_crc);
  RUN_TEST(test_fill_skips_unreadable_and_scattered_files);
  RUN_TEST(test_fill_idles_without_psram_and_restart_drops_copies);
  return UNITY_END();
}
