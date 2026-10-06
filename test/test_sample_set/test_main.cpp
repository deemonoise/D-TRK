#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unity.h>
#include <vector>
#include "project_io.h"
#include "sample_set.h"
#include "wt_mip.h"

using namespace mt;

// NOR flash in RAM: erase sets 0xFF in whole 4 KB sectors, writes may only land on erased bytes.
struct RamFlash final : BankFlash {
  std::vector<uint8_t> mem;
  bool badWrite = false;
  int erases = 0;
  bool failRead = false;
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
    if (failRead || off + n > mem.size()) return false;
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

static uint32_t sectors(uint32_t frames) { return (frames * 2 + kBankAlign - 1) / kBankAlign; }

// Adds data to the bank under its key; returns crc.
static uint32_t addKeyed(SampleBank& b, uint32_t frames, int seed) {
  const std::vector<int16_t> v = ramp(frames, seed);
  const uint32_t crc = sampleCrc(v.data(), frames);
  char k[kSampleNameMax + 1];
  sampleKey(crc, k);
  TEST_ASSERT_TRUE(b.begin(k, frames, 32000, 60));
  TEST_ASSERT_TRUE(b.write(v.data(), frames));
  TEST_ASSERT_TRUE(b.commit());
  return crc;
}

static int findKey(const SampleBank& b, uint32_t crc) {
  char k[kSampleNameMax + 1];
  sampleKey(crc, k);
  return b.find(k);
}

void test_key() {
  char k[kSampleNameMax + 1];
  sampleKey(0x00ABCDEF, k);
  TEST_ASSERT_EQUAL_STRING("00abcdef", k);
  TEST_ASSERT_TRUE(isSampleKey(k));
  TEST_ASSERT_FALSE(isSampleKey("kick"));
  TEST_ASSERT_FALSE(isSampleKey("00abcdeg"));
  TEST_ASSERT_FALSE(isSampleKey("00ABCDEF"));
  TEST_ASSERT_FALSE(isSampleKey("00abcdef0"));
  TEST_ASSERT_FALSE(isSampleKey("00abcde"));
  TEST_ASSERT_FALSE(isSampleKey(""));
}

void test_crc_chunks_equal_whole() {
  const std::vector<int16_t> v = ramp(1000, 3);
  const uint32_t whole = sampleCrc(v.data(), 1000);
  const uint32_t part = sampleCrc(v.data() + 400, 600, sampleCrc(v.data(), 400));
  TEST_ASSERT_EQUAL_HEX32(whole, part);
  // Little-endian bytes of the data.
  const int16_t one = 0x0201;
  const uint8_t bytes[2] = {1, 2};
  TEST_ASSERT_EQUAL_HEX32(crc32(bytes, 2), sampleCrc(&one, 1));
}

void test_set_find_replace() {
  static Project p;
  p.reset();
  TEST_ASSERT_EQUAL(0, projSampleSet(p, "kick", 1, 10));
  TEST_ASSERT_EQUAL(1, projSampleSet(p, "snare", 2, 20));
  TEST_ASSERT_EQUAL(0, projSampleSet(p, "KICK", 3, 30));  // replace, keeps name
  TEST_ASSERT_EQUAL(2, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("kick", p.samples[0].name);
  TEST_ASSERT_EQUAL_UINT32(3, p.samples[0].crc);
  TEST_ASSERT_EQUAL_UINT32(30, p.samples[0].frames);
  TEST_ASSERT_EQUAL(1, projSampleFind(p, "Snare"));
  TEST_ASSERT_EQUAL(-1, projSampleFind(p, "hat"));
  TEST_ASSERT_EQUAL(-1, projSampleSet(p, "", 1, 1));
  TEST_ASSERT_EQUAL(-1, projSampleSet(p, "seventeen_chars_x", 1, 1));
  TEST_ASSERT_EQUAL(-1, projSampleSet(p, "my kick", 1, 1));  // not a file name: [A-Za-z0-9_-]
  TEST_ASSERT_EQUAL(-1, projSampleSet(p, "a.b", 1, 1));
  TEST_ASSERT_EQUAL(-1, projSampleSet(p, "k/x", 1, 1));
  TEST_ASSERT_EQUAL(2, p.sampleCount);
}

void test_set_full() {
  static Project p;
  p.reset();
  char nm[8];
  for (int i = 0; i < kProjSamples; ++i) {
    snprintf(nm, sizeof(nm), "s%d", i);
    TEST_ASSERT_EQUAL(i, projSampleSet(p, nm, i, 1));
  }
  TEST_ASSERT_EQUAL(-1, projSampleSet(p, "more", 1, 1));
  TEST_ASSERT_EQUAL(5, projSampleSet(p, "S5", 9, 9));  // replacing still works
  TEST_ASSERT_EQUAL(kProjSamples, p.sampleCount);
}

void test_rename_follows_instruments() {
  static Project p;
  p.reset();
  projSampleSet(p, "kick", 1, 10);
  projSampleSet(p, "snare", 2, 20);
  strcpy(p.instruments[3].sample, "KICK");
  strcpy(p.instruments[4].sample, "snare");
  TEST_ASSERT_FALSE(projSampleRename(p, 0, "Snare"));  // taken
  TEST_ASSERT_FALSE(projSampleRename(p, 0, ""));
  TEST_ASSERT_FALSE(projSampleRename(p, 0, "bad name"));
  TEST_ASSERT_FALSE(projSampleRename(p, 0, "a.wav"));
  TEST_ASSERT_FALSE(projSampleRename(p, 2, "x"));
  TEST_ASSERT_TRUE(projSampleRename(p, 0, "bd"));
  TEST_ASSERT_EQUAL_STRING("bd", p.samples[0].name);
  TEST_ASSERT_EQUAL_STRING("bd", p.instruments[3].sample);
  TEST_ASSERT_EQUAL_STRING("snare", p.instruments[4].sample);
  TEST_ASSERT_TRUE(projSampleRename(p, 1, "SNARE"));  // own name, other case
  TEST_ASSERT_EQUAL_STRING("SNARE", p.samples[1].name);
  TEST_ASSERT_EQUAL_STRING("SNARE", p.instruments[4].sample);
}

void test_rename_updates_kit_lanes() {
  static Project p;
  p.reset();
  projSampleSet(p, "kick", 1, 10);
  projSampleSet(p, "snare", 2, 20);
  instrSetType(p.instruments[1], InstrType::Sample);
  strcpy(p.instruments[1].sample, "kick");
  instrSetType(p.instruments[2], InstrType::Kit);
  strcpy(p.instruments[2].kit[3].sample, "KICK");  // names compare case-insensitively
  strcpy(p.instruments[2].kit[4].sample, "snare");
  TEST_ASSERT_TRUE(projSampleRename(p, 0, "bd"));
  TEST_ASSERT_EQUAL_STRING("bd", p.instruments[1].sample);
  TEST_ASSERT_EQUAL_STRING("bd", p.instruments[2].kit[3].sample);
  TEST_ASSERT_EQUAL_STRING("snare", p.instruments[2].kit[4].sample);
}

void test_sample_user_counts_kit_lanes() {
  static Project p;
  p.reset();
  TEST_ASSERT_EQUAL(-1, projSampleUser(p, "kick"));
  instrSetType(p.instruments[2], InstrType::Kit);
  strcpy(p.instruments[2].kit[5].sample, "KICK");
  TEST_ASSERT_EQUAL(2, projSampleUser(p, "kick"));
  p.instruments[2].kit[5].instr = 0;  // INST lane: its sample name is not played
  TEST_ASSERT_EQUAL(-1, projSampleUser(p, "kick"));
  instrSetType(p.instruments[1], InstrType::Sample);
  strcpy(p.instruments[1].sample, "kick");
  TEST_ASSERT_EQUAL(1, projSampleUser(p, "kick"));
  instrSetType(p.instruments[1], InstrType::Chip);  // a leftover name on another type is not a user
  TEST_ASSERT_EQUAL(-1, projSampleUser(p, "kick"));
}

void test_remove_shifts() {
  static Project p;
  p.reset();
  projSampleSet(p, "a", 1, 1);
  projSampleSet(p, "b", 2, 2);
  projSampleSet(p, "c", 3, 3);
  strcpy(p.instruments[0].sample, "b");
  projSampleRemove(p, 1);
  TEST_ASSERT_EQUAL(2, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("c", p.samples[1].name);
  TEST_ASSERT_EQUAL_UINT32(3, p.samples[1].crc);
  TEST_ASSERT_EQUAL_STRING("", p.samples[2].name);
  TEST_ASSERT_EQUAL_STRING("b", p.instruments[0].sample);  // missing now
  TEST_ASSERT_EQUAL(-1, projSampleFind(p, "b"));
  projSampleRemove(p, 5);  // out of range: nothing
  TEST_ASSERT_EQUAL(2, p.sampleCount);
}

void test_bank_lookup() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t c = addKeyed(b, 5000, 1);
  projSampleSet(p, "kick", c, 5000);
  projSampleSet(p, "gone", 0x1234, 10);
  TEST_ASSERT_TRUE(findKey(b, c) >= 0);
  TEST_ASSERT_EQUAL(findKey(b, c), projSampleBank(p, b, 0));
  TEST_ASSERT_EQUAL(-1, projSampleBank(p, b, 1));
  TEST_ASSERT_EQUAL(-1, projSampleBank(p, b, 2));
  TEST_ASSERT_TRUE(bankEntryUsed(p, *b.entry(findKey(b, c))));
  p.samples[0].frames = 4999;  // frames must match too
  TEST_ASSERT_EQUAL(-1, projSampleBank(p, b, 0));
  TEST_ASSERT_FALSE(bankEntryUsed(p, *b.entry(findKey(b, c))));
}

void test_fits() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  TEST_ASSERT_TRUE(b.fits(1));
  TEST_ASSERT_TRUE(b.fits(b.capacity() / 2));
  TEST_ASSERT_FALSE(b.fits(b.capacity() / 2 + 1));  // begin's limit
  TEST_ASSERT_FALSE(b.fits(0));
}

// Bank of 254 sectors: used 49, big unused 74, small unused 25, then 106 free at the end.
void test_make_room_evicts_unused_largest_first() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t used = addKeyed(b, 100000, 1);
  const uint32_t big = addKeyed(b, 150000, 2);
  const uint32_t small = addKeyed(b, 50000, 3);
  projSampleSet(p, "keep", used, 100000);
  TEST_ASSERT_EQUAL_UINT32(106, b.freeBytes() / kBankAlign);
  // 118 sectors: only with the big one gone and the hole closed (74 + 106), small stays.
  const uint32_t need = 240000;
  TEST_ASSERT_EQUAL_UINT32(118, sectors(need));
  TEST_ASSERT_FALSE(b.fits(need));
  TEST_ASSERT_TRUE(bankMakeRoom(b, p, need));
  TEST_ASSERT_TRUE(b.fits(need));
  TEST_ASSERT_EQUAL(2, b.count());
  TEST_ASSERT_TRUE(projSampleBank(p, b, 0) >= 0);  // used one survives
  TEST_ASSERT_EQUAL(-1, findKey(b, big));
  TEST_ASSERT_TRUE(findKey(b, small) >= 0);  // not needed
  // Data intact after compaction.
  const std::vector<int16_t> v = ramp(100000, 1);
  TEST_ASSERT_EQUAL_INT16_ARRAY(v.data(), b.data(projSampleBank(p, b, 0)), 100000);
}

void test_make_room_no_eviction_needed() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  addKeyed(b, 1000, 1);
  TEST_ASSERT_TRUE(bankMakeRoom(b, p, 1000));
  TEST_ASSERT_EQUAL(1, b.count());
}

void test_make_room_fails_when_all_used() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t c1 = addKeyed(b, 250000, 1);
  const uint32_t c2 = addKeyed(b, 250000, 2);
  projSampleSet(p, "one", c1, 250000);
  projSampleSet(p, "two", c2, 250000);
  TEST_ASSERT_FALSE(bankMakeRoom(b, p, 100000));
  TEST_ASSERT_TRUE(projSampleBank(p, b, 0) >= 0);
  TEST_ASSERT_TRUE(projSampleBank(p, b, 1) >= 0);
}

void test_make_room_too_big_keeps_cache() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  addKeyed(b, 1000, 1);
  TEST_ASSERT_FALSE(bankMakeRoom(b, p, b.capacity() / 2 + 1));  // never fits: nothing evicted
  TEST_ASSERT_EQUAL(1, b.count());
}

void test_make_room_full_table() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  uint32_t last = 0;
  for (int i = 0; i < kBankEntries; ++i) {
    const uint32_t c = addKeyed(b, 1, i + 1);
    char nm[8];
    snprintf(nm, sizeof(nm), "s%d", i);
    if (i + 1 < kBankEntries) projSampleSet(p, nm, c, 1);
    else last = c;
  }
  TEST_ASSERT_EQUAL(kBankEntries, b.count());
  TEST_ASSERT_TRUE(b.freeBytes() >= 1000 * 2);  // space, but no table slot
  TEST_ASSERT_FALSE(b.fits(1000));
  TEST_ASSERT_TRUE(bankMakeRoom(b, p, 1000));
  TEST_ASSERT_EQUAL(kBankEntries - 1, b.count());
  TEST_ASSERT_EQUAL(-1, findKey(b, last));
  for (int i = 0; i < p.sampleCount; ++i) TEST_ASSERT_TRUE(projSampleBank(p, b, i) >= 0);
}

void test_clear_unused() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t c = addKeyed(b, 1000, 1);
  addKeyed(b, 1000, 2);
  addKeyed(b, 1000, 3);
  projSampleSet(p, "x", c, 1000);
  TEST_ASSERT_EQUAL(2, bankClearUnused(b, p));
  TEST_ASSERT_EQUAL(1, b.count());
  TEST_ASSERT_EQUAL(0, projSampleBank(p, b, 0));
}

// Old bank entry under a plain name; returns crc of its data.
static uint32_t addLegacy(SampleBank& b, const char* name, uint32_t frames, int seed) {
  const std::vector<int16_t> v = ramp(frames, seed);
  TEST_ASSERT_TRUE(b.begin(name, frames, 32000, 60));
  TEST_ASSERT_TRUE(b.write(v.data(), frames));
  TEST_ASSERT_TRUE(b.commit());
  return sampleCrc(v.data(), frames);
}

void test_make_room_evicts_keyed_before_legacy() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  addLegacy(b, "old", 150000, 1);           // 74 sectors, may belong to an old project
  const uint32_t k = addKeyed(b, 50000, 2);  // 25 sectors, plain cache
  // 155 free at the end; 157 sectors fit once the keyed one goes (180 in one hole).
  TEST_ASSERT_EQUAL_UINT32(157, sectors(320000));
  TEST_ASSERT_FALSE(b.fits(320000));
  TEST_ASSERT_TRUE(bankMakeRoom(b, p, 320000));
  TEST_ASSERT_EQUAL(-1, findKey(b, k));
  TEST_ASSERT_TRUE(b.find("old") >= 0);
  // Only legacy left unused: it goes too when needed.
  TEST_ASSERT_TRUE(bankMakeRoom(b, p, 400000));
  TEST_ASSERT_EQUAL(-1, b.find("old"));
}

void test_clear_unused_keeps_legacy() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  addLegacy(b, "old", 3000, 1);  // may belong to an old project not migrated yet
  addKeyed(b, 1000, 2);
  TEST_ASSERT_EQUAL(1, bankClearUnused(b, p));
  TEST_ASSERT_EQUAL(1, b.count());
  TEST_ASSERT_TRUE(b.find("old") >= 0);
}

void test_unused_bytes() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  TEST_ASSERT_EQUAL_UINT32(0, bankUnusedBytes(b, p));
  const uint32_t c = addKeyed(b, 1000, 1);  // 1 sector, used
  addKeyed(b, 3000, 2);                     // 2 sectors
  addLegacy(b, "old", 5000, 3);             // 3 sectors
  projSampleSet(p, "x", c, 1000);
  TEST_ASSERT_EQUAL_UINT32(5 * kBankAlign, bankUnusedBytes(b, p));
}

void test_read_data() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const std::vector<int16_t> v = ramp(3000, 4);
  addLegacy(b, "x", 3000, 4);
  int16_t buf[100];
  TEST_ASSERT_TRUE(b.readData(0, 2900, buf, 100));
  TEST_ASSERT_EQUAL_INT16_ARRAY(v.data() + 2900, buf, 100);
  TEST_ASSERT_FALSE(b.readData(0, 2950, buf, 100));  // past the end
  TEST_ASSERT_FALSE(b.readData(1, 0, buf, 1));        // no entry
}

struct MemIndex final : LegacyIndex {
  std::vector<LegacySample> v;
  bool failAdd = false;
  bool find(const char* name, LegacySample& out) override {
    for (const LegacySample& s : v)
      if (strcasecmp(s.name, name) == 0) {
        out = s;
        return true;
      }
    return false;
  }
  bool add(const LegacySample& s) override {
    if (failAdd) return false;
    v.push_back(s);
    return true;
  }
};

void test_migrate_renames_and_records() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const uint32_t crc = addLegacy(b, "Kick", 3000, 5);
  static Project p;
  p.reset();
  strcpy(p.name, "song");
  strcpy(p.instruments[0].sample, "kick");
  strcpy(p.instruments[1].sample, "KICK");  // same sample twice
  strcpy(p.instruments[2].sample, "lost");
  MemIndex idx;
  TEST_ASSERT_EQUAL(1, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(1, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("kick", p.samples[0].name);
  TEST_ASSERT_EQUAL_HEX32(crc, p.samples[0].crc);
  TEST_ASSERT_EQUAL_UINT32(3000, p.samples[0].frames);
  TEST_ASSERT_EQUAL(-1, b.find("Kick"));
  TEST_ASSERT_TRUE(projSampleBank(p, b, 0) >= 0);
  TEST_ASSERT_EQUAL(1, (int)idx.v.size());
  TEST_ASSERT_EQUAL_STRING("Kick", idx.v[0].name);
  TEST_ASSERT_EQUAL_HEX32(crc, idx.v[0].crc);
  TEST_ASSERT_EQUAL_UINT32(3000, idx.v[0].frames);
  TEST_ASSERT_EQUAL_STRING("song", idx.v[0].project);  // its folder will hold the file
}

void test_migrate_second_project_uses_index() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const uint32_t crc = addLegacy(b, "kick", 3000, 5);
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "kick");
  MemIndex idx;
  TEST_ASSERT_EQUAL(0, migrateSamples(p, b, idx));
  const uint32_t gen = b.generation();
  static Project q;
  q.reset();
  strcpy(q.instruments[5].sample, "Kick");
  TEST_ASSERT_EQUAL(0, migrateSamples(q, b, idx));
  TEST_ASSERT_EQUAL(1, q.sampleCount);
  TEST_ASSERT_EQUAL_STRING("Kick", q.samples[0].name);
  TEST_ASSERT_EQUAL_HEX32(crc, q.samples[0].crc);
  TEST_ASSERT_TRUE(projSampleBank(q, b, 0) >= 0);
  TEST_ASSERT_EQUAL_UINT32(gen, b.generation());  // bank unchanged
  TEST_ASSERT_EQUAL(1, (int)idx.v.size());
}

void test_migrate_same_data_twice_dedups() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const uint32_t crc = addLegacy(b, "a", 3000, 7);
  TEST_ASSERT_EQUAL_HEX32(crc, addLegacy(b, "b", 3000, 7));
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "a");
  strcpy(p.instruments[1].sample, "b");
  MemIndex idx;
  TEST_ASSERT_EQUAL(0, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(1, b.count());
  TEST_ASSERT_TRUE(findKey(b, crc) >= 0);
  TEST_ASSERT_EQUAL(2, p.sampleCount);
  TEST_ASSERT_TRUE(projSampleBank(p, b, 0) >= 0);
  TEST_ASSERT_TRUE(projSampleBank(p, b, 1) >= 0);
  TEST_ASSERT_EQUAL(2, (int)idx.v.size());
}

void test_migrate_keyed_name_already_cached() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const uint32_t crc = addKeyed(b, 2000, 3);
  char k[kSampleNameMax + 1];
  sampleKey(crc, k);
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, k);  // the key of its own data: plain cache, nothing to migrate
  MemIndex idx;
  const uint32_t gen = b.generation();
  TEST_ASSERT_EQUAL(0, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(1, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING(k, p.samples[0].name);
  TEST_ASSERT_EQUAL_HEX32(crc, p.samples[0].crc);
  TEST_ASSERT_EQUAL(0, projSampleBank(p, b, 0));
  TEST_ASSERT_EQUAL_UINT32(gen, b.generation());
  TEST_ASSERT_EQUAL(0, (int)idx.v.size());
}

void test_migrate_key_like_old_name() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const uint32_t crc = addLegacy(b, "0000abcd", 2000, 3);  // looks like a key, data says otherwise
  TEST_ASSERT_TRUE(crc != 0x0000abcd);
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "0000abcd");
  MemIndex idx;
  TEST_ASSERT_EQUAL(0, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(-1, b.find("0000abcd"));
  TEST_ASSERT_TRUE(findKey(b, crc) >= 0);
  TEST_ASSERT_EQUAL_HEX32(crc, p.samples[0].crc);
  TEST_ASSERT_EQUAL(1, (int)idx.v.size());
}

void test_migrate_index_add_fails_bank_untouched() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const uint32_t crc = addLegacy(b, "kick", 3000, 5);
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "kick");
  MemIndex idx;
  idx.failAdd = true;
  const uint32_t gen = b.generation();
  TEST_ASSERT_EQUAL(1, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(0, p.sampleCount);
  TEST_ASSERT_TRUE(b.find("kick") >= 0);
  TEST_ASSERT_EQUAL(-1, findKey(b, crc));
  TEST_ASSERT_EQUAL_UINT32(gen, b.generation());
}

void test_migrate_rename_collision_missing() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  const uint32_t crc = addLegacy(b, "kick", 3000, 5);
  char k[kSampleNameMax + 1];
  sampleKey(crc, k);
  addLegacy(b, k, 100, 9);  // the key is taken by other data (frames differ)
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "kick");
  MemIndex idx;
  TEST_ASSERT_EQUAL(1, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(0, p.sampleCount);
  TEST_ASSERT_TRUE(b.find("kick") >= 0);
  TEST_ASSERT_EQUAL(2, b.count());
}

void test_migrate_read_error_missing() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  addLegacy(b, "kick", 3000, 5);
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "kick");
  MemIndex idx;
  flash->failRead = true;
  TEST_ASSERT_EQUAL(1, migrateSamples(p, b, idx));
  flash->failRead = false;
  TEST_ASSERT_EQUAL(0, p.sampleCount);
  TEST_ASSERT_TRUE(b.find("kick") >= 0);
  TEST_ASSERT_EQUAL(0, (int)idx.v.size());
}

void test_migrate_invalid_name_missing() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  addLegacy(b, "my kick", 3000, 5);
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "my kick");  // no file name: cannot live in the project folder
  strcpy(p.instruments[1].sample, "MY KICK");
  MemIndex idx;
  TEST_ASSERT_EQUAL(1, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(0, p.sampleCount);
  TEST_ASSERT_TRUE(b.find("my kick") >= 0);
  TEST_ASSERT_EQUAL(0, (int)idx.v.size());
}

void test_migrate_missing() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "x");
  strcpy(p.instruments[1].sample, "y");
  strcpy(p.instruments[2].sample, "X");  // counted once
  MemIndex idx;
  TEST_ASSERT_EQUAL(2, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(0, p.sampleCount);
  TEST_ASSERT_EQUAL(0, (int)idx.v.size());
}


// ---- Wavetables ----

void test_wt_key() {
  char k[kSampleNameMax + 1];
  wtKey(0xABCDEF01, k);
  TEST_ASSERT_EQUAL_STRING("wabcdef01", k);
  TEST_ASSERT_TRUE(isWtKey(k));
  wtKey(1, k);
  TEST_ASSERT_EQUAL_STRING("w00000001", k);
  TEST_ASSERT_FALSE(isWtKey("abcdef01"));
  TEST_ASSERT_FALSE(isWtKey("wABCDEF01"));
  TEST_ASSERT_FALSE(isWtKey("wabcdef0"));
  TEST_ASSERT_FALSE(isWtKey("wabcdef012"));
  TEST_ASSERT_FALSE(isWtKey("*SAWSQR"));
  TEST_ASSERT_FALSE(isWtKey(""));
  TEST_ASSERT_FALSE(isSampleKey("wabcdef01"));
}

void test_wt_set_find_remove() {
  static Project p;
  p.reset();
  TEST_ASSERT_EQUAL(0, projWtSet(p, "pad", 1));
  TEST_ASSERT_EQUAL(1, projWtSet(p, "lead", 2));
  TEST_ASSERT_EQUAL(0, projWtSet(p, "PAD", 5));  // same name ignoring case: replaces crc
  TEST_ASSERT_EQUAL(2, p.wavetableCount);
  TEST_ASSERT_EQUAL_STRING("pad", p.wavetables[0].name);
  TEST_ASSERT_EQUAL_UINT32(5, p.wavetables[0].crc);
  TEST_ASSERT_EQUAL(1, projWtFind(p, "LEAD"));
  TEST_ASSERT_EQUAL(-1, projWtFind(p, "bass"));
  TEST_ASSERT_EQUAL(-1, projWtSet(p, "*X", 3));
  TEST_ASSERT_EQUAL(-1, projWtSet(p, "a.b", 3));
  TEST_ASSERT_EQUAL(-1, projWtSet(p, "", 3));
  TEST_ASSERT_EQUAL(2, p.wavetableCount);
  projWtRemove(p, 0);
  TEST_ASSERT_EQUAL(1, p.wavetableCount);
  TEST_ASSERT_EQUAL_STRING("lead", p.wavetables[0].name);
  TEST_ASSERT_EQUAL_UINT32(2, p.wavetables[0].crc);
  TEST_ASSERT_EQUAL_STRING("", p.wavetables[1].name);
  projWtRemove(p, 5);  // out of range: nothing
  TEST_ASSERT_EQUAL(1, p.wavetableCount);
  for (int i = 1; i < kProjWavetables; ++i) {
    char nm[8];
    snprintf(nm, sizeof(nm), "t%d", i);
    TEST_ASSERT_EQUAL(i, projWtSet(p, nm, i));
  }
  TEST_ASSERT_EQUAL(-1, projWtSet(p, "more", 1));  // full
  TEST_ASSERT_EQUAL(0, projWtSet(p, "lead", 9));   // existing name still replaceable
}

void test_wt_prune() {
  static Project p;
  p.reset();
  projWtSet(p, "pad", 1);
  projWtSet(p, "lead", 2);
  projWtSet(p, "bass", 3);
  p.instruments[2].type = InstrType::Synth;
  strcpy(p.instruments[2].synWt[1], "LEAD");  // osc 2, any mode, ignoring case
  strcpy(p.instruments[5].synWt[0], "bass");
  strcpy(p.instruments[6].synWt[0], "*SAWSQR");
  TEST_ASSERT_EQUAL(1, projWtPrune(p));
  TEST_ASSERT_EQUAL(2, p.wavetableCount);
  TEST_ASSERT_EQUAL(-1, projWtFind(p, "pad"));
  TEST_ASSERT_TRUE(projWtFind(p, "lead") >= 0);
  TEST_ASSERT_TRUE(projWtFind(p, "bass") >= 0);
}

// Adds a wavetable entry (rate 0) under name; returns its bank index.
static int addWt(SampleBank& b, const char* name, int seed, uint32_t frames = kWtTableSamples) {
  const std::vector<int16_t> v = ramp(frames, seed);
  TEST_ASSERT_TRUE(b.begin(name, frames, 0, 0));
  TEST_ASSERT_TRUE(b.write(v.data(), frames));
  TEST_ASSERT_TRUE(b.commit());
  return b.find(name);
}

void test_wt_bank_lookup() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  char k[kSampleNameMax + 1];
  wtKey(0x1234, k);
  addWt(b, k, 1);
  wtKey(0x5678, k);
  addWt(b, k, 2, 1000);  // wrong size: not this table
  projWtSet(p, "pad", 0x1234);
  projWtSet(p, "lead", 0x5678);
  projWtSet(p, "bass", 0x9999);
  wtKey(0x1234, k);
  TEST_ASSERT_EQUAL(b.find(k), projWtBank(p, b, 0));
  TEST_ASSERT_EQUAL(-1, projWtBank(p, b, 1));
  TEST_ASSERT_EQUAL(-1, projWtBank(p, b, 2));
  TEST_ASSERT_EQUAL(-1, projWtBank(p, b, 3));
  TEST_ASSERT_EQUAL(-1, projWtBank(p, b, -1));
}

void test_wt_clear_unused() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  char used[kSampleNameMax + 1], unused[kSampleNameMax + 1];
  wtKey(0x11, used);
  wtKey(0x22, unused);
  addWt(b, "*SAWSQR", 1);
  addWt(b, used, 2);
  addWt(b, unused, 3);
  projWtSet(p, "pad", 0x11);
  TEST_ASSERT_TRUE(bankEntryUsed(p, *b.entry(b.find("*SAWSQR"))));
  TEST_ASSERT_EQUAL_UINT32(sectors(kWtTableSamples) * kBankAlign, bankUnusedBytes(b, p));
  TEST_ASSERT_EQUAL(1, bankClearUnused(b, p));
  TEST_ASSERT_EQUAL(2, b.count());
  TEST_ASSERT_TRUE(b.find("*SAWSQR") >= 0);
  TEST_ASSERT_TRUE(b.find(used) >= 0);
  TEST_ASSERT_EQUAL(-1, b.find(unused));
}

void test_wt_make_room_keeps_builtin_and_used() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  char used[kSampleNameMax + 1], unused[kSampleNameMax + 1];
  wtKey(0x11, used);
  wtKey(0x22, unused);
  addWt(b, "*SAWSQR", 1);  // 24 sectors each
  addWt(b, used, 2);
  addWt(b, unused, 3);
  const uint32_t s1 = addKeyed(b, 100000, 4);  // 49 sectors each, used
  const uint32_t s2 = addKeyed(b, 100000, 5);
  projSampleSet(p, "one", s1, 100000);
  projSampleSet(p, "two", s2, 100000);
  projWtSet(p, "pad", 0x11);
  const uint32_t need = 100 * kBankAlign / 2;  // 100 sectors: only with the unused table gone
  TEST_ASSERT_FALSE(b.fits(need));
  TEST_ASSERT_TRUE(bankMakeRoom(b, p, need));
  TEST_ASSERT_EQUAL(-1, b.find(unused));
  TEST_ASSERT_TRUE(b.find("*SAWSQR") >= 0);
  TEST_ASSERT_TRUE(b.find(used) >= 0);
  // Nothing else may go: built-in and used entries stay.
  TEST_ASSERT_FALSE(bankMakeRoom(b, p, b.capacity() / 2));
  TEST_ASSERT_TRUE(b.find("*SAWSQR") >= 0);
  TEST_ASSERT_TRUE(b.find(used) >= 0);
  TEST_ASSERT_EQUAL(4, b.count());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_key);
  RUN_TEST(test_crc_chunks_equal_whole);
  RUN_TEST(test_set_find_replace);
  RUN_TEST(test_set_full);
  RUN_TEST(test_rename_follows_instruments);
  RUN_TEST(test_rename_updates_kit_lanes);
  RUN_TEST(test_sample_user_counts_kit_lanes);
  RUN_TEST(test_remove_shifts);
  RUN_TEST(test_bank_lookup);
  RUN_TEST(test_fits);
  RUN_TEST(test_make_room_evicts_unused_largest_first);
  RUN_TEST(test_make_room_no_eviction_needed);
  RUN_TEST(test_make_room_fails_when_all_used);
  RUN_TEST(test_make_room_too_big_keeps_cache);
  RUN_TEST(test_make_room_full_table);
  RUN_TEST(test_clear_unused);
  RUN_TEST(test_make_room_evicts_keyed_before_legacy);
  RUN_TEST(test_clear_unused_keeps_legacy);
  RUN_TEST(test_unused_bytes);
  RUN_TEST(test_read_data);
  RUN_TEST(test_migrate_renames_and_records);
  RUN_TEST(test_migrate_second_project_uses_index);
  RUN_TEST(test_migrate_same_data_twice_dedups);
  RUN_TEST(test_migrate_keyed_name_already_cached);
  RUN_TEST(test_migrate_key_like_old_name);
  RUN_TEST(test_migrate_index_add_fails_bank_untouched);
  RUN_TEST(test_migrate_rename_collision_missing);
  RUN_TEST(test_migrate_read_error_missing);
  RUN_TEST(test_migrate_invalid_name_missing);
  RUN_TEST(test_migrate_missing);
  RUN_TEST(test_wt_key);
  RUN_TEST(test_wt_set_find_remove);
  RUN_TEST(test_wt_prune);
  RUN_TEST(test_wt_bank_lookup);
  RUN_TEST(test_wt_clear_unused);
  RUN_TEST(test_wt_make_room_keeps_builtin_and_used);
  return UNITY_END();
}
