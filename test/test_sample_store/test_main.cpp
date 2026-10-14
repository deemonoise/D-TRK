#include <string.h>
#include <unity.h>
#include <vector>
#include "sample_set.h"
#include "sample_store.h"
#include "wav.h"

using namespace mt;

struct VecSource : ByteSource {
  const std::vector<uint8_t>& buf;
  size_t pos = 0;
  explicit VecSource(const std::vector<uint8_t>& b) : buf(b) {}
  bool read(void* d, size_t n) override {
    if (pos + n > buf.size()) return false;
    memcpy(d, buf.data() + pos, n);
    pos += n;
    return true;
  }
  bool skip(size_t n) override {
    if (pos + n > buf.size()) return false;
    pos += n;
    return true;
  }
};

static StoreTable* table;
static StreamPool* pool;
static Project* model;  // projSampleSet takes a Project

void setUp() {
  table = new StoreTable();
  pool = new StreamPool();
  model = new Project();
}
void tearDown() {
  delete model;
  delete pool;
  delete table;
}

void test_path_is_lower_hex() {
  char p[kStorePathMax];
  storePath(0x00ABCDEFu, p);
  TEST_ASSERT_EQUAL_STRING("/mt-store/00abcdef.wav", p);
  storePeaksPath(0x00ABCDEFu, p);
  TEST_ASSERT_EQUAL_STRING("/mt-store/00abcdef.pk", p);
}

void test_header_puts_data_at_512_and_parses_back() {
  std::vector<uint8_t> f(kStoreDataOffset);
  storeHeader(f.data(), 1000, 22050, 48, 0xABCD1234u);
  f.resize(kStoreDataOffset + 2000, 0);
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(WavErr::Ok, wavParse(src, w));
  TEST_ASSERT_EQUAL_UINT32(kStoreDataOffset, w.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(1000, w.frames());
  TEST_ASSERT_EQUAL_UINT32(22050, w.rate);
  TEST_ASSERT_EQUAL(1, w.channels);
  TEST_ASSERT_EQUAL(16, w.bits);
  TEST_ASSERT_TRUE(w.hasCrc);
  TEST_ASSERT_EQUAL_HEX32(0xABCD1234u, w.crc);
  TEST_ASSERT_EQUAL(48, w.root);
  uint32_t riff;
  memcpy(&riff, f.data() + 4, 4);
  TEST_ASSERT_EQUAL_UINT32(f.size() - 8, riff);
}

void test_table_add_find_and_full() {
  TEST_ASSERT_EQUAL(-1, table->find(5));
  const int i = table->add(5, 100, 44100, 1234);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL(i, table->find(5));
  TEST_ASSERT_EQUAL(i, table->add(5, 100, 44100, 1234));  // already there
  for (uint32_t k = 6; table->count() < kStoreEntries; ++k) TEST_ASSERT_TRUE(table->add(k, 1, 1, 1) >= 0);
  TEST_ASSERT_EQUAL(-1, table->add(0xFFFF0000u, 1, 1, 1));
  table->clear();
  TEST_ASSERT_EQUAL(0, table->count());
  TEST_ASSERT_EQUAL(-1, table->find(5));
}

void test_table_keeps_heads() {
  static int16_t d[100];
  SampleHead h[2];
  h[0].frame = 0;
  h[0].len = 50;
  h[0].d = d;
  h[1].frame = 500;
  h[1].len = 50;
  h[1].d = d + 50;
  const int i = table->add(9, 1000, 44100, 77);
  TEST_ASSERT_TRUE(table->setHeads(i, h, 2));
  TEST_ASSERT_EQUAL(2, table->at(i).nHeads);
  TEST_ASSERT_EQUAL_UINT32(500, table->heads(i)[1].frame);
  TEST_ASSERT_TRUE(table->heads(i)[1].d == d + 50);
}

void test_source_streams_and_prefers_psram() {
  projSampleSet(*model, "PAD", 0x1111u, 4000);
  const int i = table->add(0x1111u, 4000, 32000, 900);
  StoreSampleSource src(*table, *pool, nullptr, *model);
  SampleData d;
  TEST_ASSERT_TRUE(src.find("pad", d));
  TEST_ASSERT_EQUAL_HEX32(0x1111u, d.key);
  TEST_ASSERT_TRUE(d.streamable);
  TEST_ASSERT_NULL(d.whole);
  TEST_ASSERT_EQUAL_UINT32(32000, d.rate);
  static int16_t copy[4000];
  table->setPsram(i, copy);
  pool->psramOk = true;
  TEST_ASSERT_TRUE(src.find("pad", d));
  TEST_ASSERT_TRUE(d.whole == copy);
  TEST_ASSERT_TRUE(d.psram);
  pool->psramOk = false;
  TEST_ASSERT_TRUE(src.find("pad", d));
  TEST_ASSERT_NULL(d.whole);
}

void test_source_skips_stale_or_scattered_entries() {
  projSampleSet(*model, "PAD", 0x2222u, 4000);
  table->add(0x2222u, 3999, 32000, 900);  // other length: another file under this crc
  StoreSampleSource src(*table, *pool, nullptr, *model);
  SampleData d;
  TEST_ASSERT_FALSE(src.find("pad", d));
  table->clear();
  table->add(0x2222u, 4000, 32000, 0);  // not contiguous: cannot stream
  TEST_ASSERT_FALSE(src.find("pad", d));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_path_is_lower_hex);
  RUN_TEST(test_header_puts_data_at_512_and_parses_back);
  RUN_TEST(test_table_add_find_and_full);
  RUN_TEST(test_table_keeps_heads);
  RUN_TEST(test_source_streams_and_prefers_psram);
  RUN_TEST(test_source_skips_stale_or_scattered_entries);
  return UNITY_END();
}
