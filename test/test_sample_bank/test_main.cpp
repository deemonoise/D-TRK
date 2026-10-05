#include <stdio.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "sample_bank.h"

using namespace mt;

// NOR flash in RAM: erase sets 0xFF in whole 4 KB sectors, writes may only land on erased bytes.
struct RamFlash final : BankFlash {
  std::vector<uint8_t> mem;
  bool badWrite = false;
  int erases = 0;
  int failAfter = -1;  // >= 0: power cut after that many more erase/write calls (they fail)
  bool cut() {
    if (failAfter < 0) return false;
    if (failAfter == 0) return true;
    --failAfter;
    return false;
  }
  explicit RamFlash(uint32_t n) : mem(n, 0x00) {}  // not erased: mount must format
  uint32_t size() const override { return static_cast<uint32_t>(mem.size()); }
  bool read(uint32_t off, void* d, uint32_t n) override {
    if (off + n > mem.size()) return false;
    memcpy(d, mem.data() + off, n);
    return true;
  }
  bool erase(uint32_t off, uint32_t n) override {
    if (cut()) return false;
    if (off % kBankAlign || n % kBankAlign || off + n > mem.size()) {
      badWrite = true;
      return false;
    }
    memset(mem.data() + off, 0xFF, n);
    erases += static_cast<int>(n / kBankAlign);
    return true;
  }
  bool write(uint32_t off, const void* d, uint32_t n) override {
    if (cut()) return false;
    if (off + n > mem.size()) return false;
    for (uint32_t i = 0; i < n; ++i)
      if (mem[off + i] != 0xFF) badWrite = true;
    memcpy(mem.data() + off, d, n);
    return true;
  }
  const uint8_t* mapped() const override { return mem.data(); }
};

constexpr uint32_t kFlash = 1024 * 1024;
static RamFlash* flash;

void setUp() { flash = new RamFlash(kFlash); }
void tearDown() {
  TEST_ASSERT_FALSE_MESSAGE(flash->badWrite, "write to a non-erased byte or unaligned erase");
  delete flash;
}

static std::vector<int16_t> ramp(uint32_t frames, int seed) {
  std::vector<int16_t> v(frames);
  for (uint32_t i = 0; i < frames; ++i) v[i] = static_cast<int16_t>(seed * 1000 + i * 7);
  return v;
}

// Adds in uneven write pieces.
static bool add(SampleBank& b, const char* name, const std::vector<int16_t>& d, uint32_t rate = 32000) {
  if (!b.begin(name, static_cast<uint32_t>(d.size()), rate, 60)) return false;
  uint32_t pos = 0, piece = 1;
  while (pos < d.size()) {
    uint32_t n = piece;
    if (n > d.size() - pos) n = static_cast<uint32_t>(d.size() - pos);
    if (!b.write(d.data() + pos, n)) return false;
    pos += n;
    piece = piece * 3 + 1;
  }
  return b.commit();
}

static void checkData(SampleBank& b, const char* name, const std::vector<int16_t>& d) {
  const int i = b.find(name);
  TEST_ASSERT_TRUE_MESSAGE(i >= 0, name);
  TEST_ASSERT_EQUAL_UINT32(d.size(), b.entry(i)->frames);
  TEST_ASSERT_EQUAL_UINT32(0, b.entry(i)->offset % kBankAlign);
  TEST_ASSERT_TRUE(b.entry(i)->offset >= kBankHeader);
  TEST_ASSERT_EQUAL_INT16_ARRAY(d.data(), b.data(i), d.size());
}

static uint32_t sectors(uint32_t frames) { return (frames * 2 + kBankAlign - 1) / kBankAlign * kBankAlign; }

void test_mount_formats_empty() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  TEST_ASSERT_EQUAL(0, b.count());
  TEST_ASSERT_EQUAL_UINT32(kFlash - kBankHeader, b.freeBytes());
  TEST_ASSERT_EQUAL(-1, b.find("x"));
}

void test_add_three() {
  SampleBank b(*flash);
  b.mount();
  const auto a = ramp(1000, 1), c = ramp(5000, 2), d = ramp(2048, 3);
  TEST_ASSERT_TRUE(add(b, "kick", a));
  TEST_ASSERT_TRUE(add(b, "snare", c, 22050));
  TEST_ASSERT_TRUE(add(b, "hat", d));
  TEST_ASSERT_EQUAL(3, b.count());
  checkData(b, "kick", a);
  checkData(b, "snare", c);
  checkData(b, "hat", d);
  TEST_ASSERT_EQUAL_UINT32(22050, b.entry(b.find("snare"))->rate);
  TEST_ASSERT_EQUAL(60, b.entry(b.find("snare"))->root);
  TEST_ASSERT_EQUAL_UINT32(kFlash - kBankHeader - sectors(1000) - sectors(5000) - sectors(2048), b.freeBytes());
  TEST_ASSERT_FALSE(b.begin("kick", 10, 32000, 60));  // duplicate name
}

void test_entries_sorted_by_name() {
  SampleBank b(*flash);
  b.mount();
  add(b, "b", ramp(10, 1));
  add(b, "C", ramp(10, 1));
  add(b, "a", ramp(10, 1));
  TEST_ASSERT_EQUAL_STRING("a", b.entry(0)->name);
  TEST_ASSERT_EQUAL_STRING("b", b.entry(1)->name);
  TEST_ASSERT_EQUAL_STRING("C", b.entry(2)->name);
}

void test_remove_and_reuse_hole() {
  SampleBank b(*flash);
  b.mount();
  const auto a = ramp(4000, 1), m = ramp(10000, 2), z = ramp(3000, 3);
  add(b, "a", a);
  add(b, "m", m);
  add(b, "z", z);
  const uint32_t holeOff = b.entry(b.find("m"))->offset;
  const uint32_t endOff = b.entry(b.find("z"))->offset + sectors(3000);
  TEST_ASSERT_TRUE(b.remove(b.find("m")));
  TEST_ASSERT_EQUAL(2, b.count());
  TEST_ASSERT_EQUAL(-1, b.find("m"));
  const auto small = ramp(6000, 4), big = ramp(20000, 5);
  TEST_ASSERT_TRUE(add(b, "small", small));
  TEST_ASSERT_EQUAL_UINT32(holeOff, b.entry(b.find("small"))->offset);
  TEST_ASSERT_TRUE(add(b, "big", big));
  TEST_ASSERT_EQUAL_UINT32(endOff, b.entry(b.find("big"))->offset);
  checkData(b, "a", a);
  checkData(b, "z", z);
  checkData(b, "small", small);
  checkData(b, "big", big);
}

void test_overflow() {
  SampleBank b(*flash);
  b.mount();
  const uint32_t maxFrames = (kFlash - kBankHeader) / 2;
  TEST_ASSERT_FALSE(b.begin("huge", maxFrames + 1, 32000, 60));
  TEST_ASSERT_TRUE(add(b, "full", ramp(maxFrames, 1)));
  TEST_ASSERT_EQUAL_UINT32(0, b.freeBytes());
  TEST_ASSERT_FALSE(b.begin("more", 1, 32000, 60));
  TEST_ASSERT_FALSE(b.begin("zero", 0, 32000, 60));
}

void test_write_past_reservation_fails() {
  SampleBank b(*flash);
  b.mount();
  TEST_ASSERT_TRUE(b.begin("s", 10, 32000, 60));
  const auto d = ramp(11, 1);
  TEST_ASSERT_FALSE(b.write(d.data(), 11));
  TEST_ASSERT_TRUE(b.write(d.data(), 8));  // fewer than reserved: commit stores what was written
  TEST_ASSERT_TRUE(b.commit());
  TEST_ASSERT_EQUAL_UINT32(8, b.entry(b.find("s"))->frames);
}

void test_table_full() {
  SampleBank b(*flash);
  b.mount();
  char nm[8];
  for (int i = 0; i < kBankEntries; ++i) {
    snprintf(nm, sizeof(nm), "s%d", i);
    TEST_ASSERT_TRUE(add(b, nm, ramp(1, i)));
  }
  TEST_ASSERT_FALSE(b.begin("extra", 1, 32000, 60));
}

void test_compact() {
  SampleBank b(*flash);
  b.mount();
  const auto a = ramp(3000, 1), m = ramp(9000, 2), z = ramp(5000, 3), y = ramp(4096, 4);
  add(b, "a", a);
  add(b, "m", m);
  add(b, "z", z);
  add(b, "y", y);
  b.remove(b.find("a"));
  b.remove(b.find("z"));
  const uint32_t before = b.freeBytes();
  TEST_ASSERT_TRUE(b.compact());
  checkData(b, "m", m);
  checkData(b, "y", y);
  TEST_ASSERT_TRUE(b.freeBytes() >= before);
  // No holes: data starts right after the header and is contiguous.
  const BankEntry* e0 = b.entry(b.find("m"));
  const BankEntry* e1 = b.entry(b.find("y"));
  TEST_ASSERT_EQUAL_UINT32(kBankHeader, e0->offset);
  TEST_ASSERT_EQUAL_UINT32(kBankHeader + sectors(9000), e1->offset);
  // The free space is one hole at the end: the largest possible sample fits.
  TEST_ASSERT_TRUE(b.begin("fill", b.freeBytes() / 2, 32000, 60));
  b.abort();
  SampleBank again(*flash);
  TEST_ASSERT_TRUE(again.mount());
  checkData(again, "m", m);
  checkData(again, "y", y);
}

void test_remount_same_table() {
  const auto a = ramp(1234, 1), c = ramp(777, 2);
  {
    SampleBank b(*flash);
    b.mount();
    add(b, "one", a, 44100);
    add(b, "two", c);
    b.remove(b.find("one"));
    add(b, "three", a);
  }
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  TEST_ASSERT_EQUAL(2, b.count());
  checkData(b, "two", c);
  checkData(b, "three", a);
  TEST_ASSERT_EQUAL_UINT32(kFlash - kBankHeader - sectors(777) - sectors(1234), b.freeBytes());
}

void test_abort_frees_space() {
  SampleBank b(*flash);
  b.mount();
  const uint32_t free0 = b.freeBytes();
  TEST_ASSERT_TRUE(b.begin("part", 20000, 32000, 60));
  const auto d = ramp(9000, 1);
  TEST_ASSERT_TRUE(b.write(d.data(), 9000));
  b.abort();
  TEST_ASSERT_EQUAL(0, b.count());
  TEST_ASSERT_EQUAL_UINT32(free0, b.freeBytes());
  // The dirty sectors are erased again before reuse.
  const auto e = ramp(15000, 2);
  TEST_ASSERT_TRUE(add(b, "real", e));
  checkData(b, "real", e);
  TEST_ASSERT_EQUAL_UINT32(kBankHeader, b.entry(0)->offset);
}

void test_corrupt_table_falls_back_to_other_copy() {
  const auto a = ramp(100, 1);
  {
    SampleBank b(*flash);
    b.mount();
    add(b, "one", a);
    add(b, "two", a);
  }
  // A power cut while the newest copy was being written: it fails its CRC, the older one is used.
  bool hit = false;
  for (uint32_t s = 0; s < 2 && !hit; ++s) {
    SampleBank probe(*flash);
    flash->mem[s * kBankAlign + 20] ^= 0x55;
    probe.mount();
    if (probe.count() == 1) hit = true;
    else flash->mem[s * kBankAlign + 20] ^= 0x55;
  }
  TEST_ASSERT_TRUE(hit);
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  TEST_ASSERT_EQUAL(1, b.count());
  checkData(b, "one", a);
}

// A sample moving down by less than its own length overlaps itself: a power cut at any point
// leaves a table that points at intact data.
void test_compact_overlapping_move_survives_power_cut() {
  const auto a = ramp(2048, 1), m = ramp(9000, 2), y = ramp(3000, 3);
  bool finished = false;
  for (int cutAt = 0; cutAt < 400 && !finished; ++cutAt) {
    delete flash;
    flash = new RamFlash(kFlash);
    SampleBank b(*flash);
    b.mount();
    add(b, "a", a);
    add(b, "m", m);
    add(b, "y", y);
    b.remove(b.find("a"));  // one sector hole before m (5 sectors)
    flash->failAfter = cutAt;
    finished = b.compact();
    flash->failAfter = -1;
    SampleBank again(*flash);
    TEST_ASSERT_TRUE(again.mount());
    TEST_ASSERT_EQUAL(2, again.count());
    checkData(again, "m", m);
    checkData(again, "y", y);
  }
  TEST_ASSERT_TRUE(finished);
  SampleBank b(*flash);
  b.mount();
  TEST_ASSERT_EQUAL_UINT32(kBankHeader, b.entry(b.find("m"))->offset);
  TEST_ASSERT_EQUAL_UINT32(kBankHeader + sectors(9000), b.entry(b.find("y"))->offset);
}

void test_commit_failure_rolls_back() {
  SampleBank b(*flash);
  b.mount();
  const auto a = ramp(100, 1), c = ramp(200, 2);
  TEST_ASSERT_TRUE(add(b, "a", a));
  TEST_ASSERT_TRUE(b.begin("c", 200, 32000, 60));
  TEST_ASSERT_TRUE(b.write(c.data(), 200));
  flash->failAfter = 0;  // the table save fails
  TEST_ASSERT_FALSE(b.commit());
  flash->failAfter = -1;
  TEST_ASSERT_EQUAL(1, b.count());
  TEST_ASSERT_EQUAL(-1, b.find("c"));
  checkData(b, "a", a);
  TEST_ASSERT_TRUE(add(b, "c", c));
  checkData(b, "c", c);
}

void test_rename() {
  SampleBank b(*flash);
  b.mount();
  const auto a = ramp(100, 1), c = ramp(200, 2);
  add(b, "a", a);
  add(b, "c", c);
  const uint32_t g = b.generation();
  TEST_ASSERT_TRUE(b.rename(b.find("a"), "z"));
  TEST_ASSERT_TRUE(b.generation() != g);
  TEST_ASSERT_EQUAL(-1, b.find("a"));
  TEST_ASSERT_EQUAL(1, b.find("z"));  // sorted by name
  checkData(b, "z", a);
  TEST_ASSERT_FALSE(b.rename(b.find("z"), "C"));  // taken, ignoring case
  TEST_ASSERT_FALSE(b.rename(b.find("z"), ""));
  TEST_ASSERT_FALSE(b.rename(-1, "q"));
  TEST_ASSERT_TRUE(b.rename(b.find("c"), "C"));  // own name, other case
  TEST_ASSERT_EQUAL_STRING("C", b.entry(b.find("c"))->name);
  flash->failAfter = 0;
  TEST_ASSERT_FALSE(b.rename(b.find("z"), "b"));  // save fails: unchanged
  flash->failAfter = -1;
  TEST_ASSERT_EQUAL(-1, b.find("b"));
  checkData(b, "z", a);
  SampleBank again(*flash);
  TEST_ASSERT_TRUE(again.mount());
  checkData(again, "z", a);
  checkData(again, "C", c);
}

void test_replace_takes_name_in_one_save() {
  SampleBank b(*flash);
  b.mount();
  const auto old = ramp(300, 1), nu = ramp(500, 2), other = ramp(100, 3);
  add(b, "kick", old);
  add(b, "snare", other);
  add(b, "~tmp0", nu);
  const uint32_t g = b.generation();
  TEST_ASSERT_FALSE(b.replace(b.find("kick"), b.find("kick")));
  TEST_ASSERT_FALSE(b.replace(-1, b.find("kick")));
  flash->failAfter = 0;
  TEST_ASSERT_FALSE(b.replace(b.find("kick"), b.find("~tmp0")));  // save fails: unchanged
  flash->failAfter = -1;
  TEST_ASSERT_EQUAL(3, b.count());
  checkData(b, "kick", old);
  checkData(b, "~tmp0", nu);
  TEST_ASSERT_TRUE(b.replace(b.find("kick"), b.find("~tmp0")));
  TEST_ASSERT_TRUE(b.generation() != g);
  TEST_ASSERT_EQUAL(2, b.count());
  TEST_ASSERT_EQUAL(-1, b.find("~tmp0"));
  checkData(b, "kick", nu);
  checkData(b, "snare", other);
  TEST_ASSERT_EQUAL_UINT32(kFlash - kBankHeader - sectors(500) - sectors(100), b.freeBytes());
  SampleBank again(*flash);
  TEST_ASSERT_TRUE(again.mount());
  TEST_ASSERT_EQUAL(2, again.count());
  checkData(again, "kick", nu);
  checkData(again, "snare", other);
}

void test_generation_changes_on_every_change() {
  SampleBank b(*flash);
  b.mount();
  uint32_t g = b.generation();
  add(b, "a", ramp(3000, 1));
  add(b, "c", ramp(100, 2));
  TEST_ASSERT_TRUE(b.generation() != g);
  g = b.generation();
  b.remove(b.find("a"));
  TEST_ASSERT_TRUE(b.generation() != g);
  g = b.generation();
  b.compact();
  TEST_ASSERT_TRUE(b.generation() != g);
}

void test_remove_bad_index() {
  SampleBank b(*flash);
  b.mount();
  add(b, "a", ramp(100, 1));
  const uint32_t g = b.generation();
  TEST_ASSERT_FALSE(b.remove(-1));
  TEST_ASSERT_FALSE(b.remove(1));
  TEST_ASSERT_EQUAL(1, b.count());
  TEST_ASSERT_EQUAL_UINT32(g, b.generation());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mount_formats_empty);
  RUN_TEST(test_add_three);
  RUN_TEST(test_entries_sorted_by_name);
  RUN_TEST(test_remove_and_reuse_hole);
  RUN_TEST(test_overflow);
  RUN_TEST(test_write_past_reservation_fails);
  RUN_TEST(test_table_full);
  RUN_TEST(test_compact);
  RUN_TEST(test_remount_same_table);
  RUN_TEST(test_abort_frees_space);
  RUN_TEST(test_corrupt_table_falls_back_to_other_copy);
  RUN_TEST(test_compact_overlapping_move_survives_power_cut);
  RUN_TEST(test_commit_failure_rolls_back);
  RUN_TEST(test_rename);
  RUN_TEST(test_replace_takes_name_in_one_save);
  RUN_TEST(test_generation_changes_on_every_change);
  RUN_TEST(test_remove_bad_index);
  return UNITY_END();
}
