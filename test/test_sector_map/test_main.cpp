#include <cstring>
#include <vector>
#include <unity.h>
#include "sector_map.h"

using namespace mt;

struct Read : SectorReader {
  struct Call { uint32_t sector, count; };
  std::vector<Call> calls;
  bool fail = false;
  bool read(uint32_t sector, uint32_t count, uint8_t* out) override {
    calls.push_back({sector, count});
    if (fail) return false;
    for (uint32_t i = 0; i < count; ++i) memset(out + 512*i, (sector+i) & 255, 512);
    return true;
  }
};
void setUp() {}
void tearDown() {}

void test_contiguous_file_keeps_one_raw_read() {
  SectorMap map;
  map.first = 100; map.sectors = 10;
  uint8_t data[3*512]; Read card;
  TEST_ASSERT_TRUE(map.read(2, 3, data, card));
  TEST_ASSERT_EQUAL(1, card.calls.size());
  TEST_ASSERT_EQUAL(102, card.calls[0].sector);
  TEST_ASSERT_EQUAL(3, card.calls[0].count);
  TEST_ASSERT_EQUAL(102, data[0]); TEST_ASSERT_EQUAL(104, data[1024]);
}
void test_fragmented_read_splits_at_extent_boundary() {
  SectorExtent runs[] = {{0, 100, 4}, {4, 300, 4}};
  SectorMap map;
  map.sectors = 8; map.extents = runs; map.count = 2;
  uint8_t data[6*512]; Read card;
  TEST_ASSERT_TRUE(map.read(1, 6, data, card));
  TEST_ASSERT_EQUAL(2, card.calls.size());
  TEST_ASSERT_EQUAL(101, card.calls[0].sector); TEST_ASSERT_EQUAL(3, card.calls[0].count);
  TEST_ASSERT_EQUAL(300, card.calls[1].sector); TEST_ASSERT_EQUAL(3, card.calls[1].count);
  const uint32_t sectors[] = {101,102,103,300,301,302};
  for (int i=0;i<6;++i) for(int j=0;j<512;++j)
    TEST_ASSERT_EQUAL(sectors[i]&255, data[512*i+j]);
}
void test_file_tail_is_zero_padded_without_reading_other_files() {
  SectorExtent runs[] = {{0, 100, 3}, {3, 300, 2}};
  SectorMap map;
  map.sectors = 5; map.extents = runs; map.count = 2;
  uint8_t data[3*512]; memset(data, 90, sizeof data); Read card;
  TEST_ASSERT_TRUE(map.read(4, 3, data, card));
  TEST_ASSERT_EQUAL(1, card.calls.size()); TEST_ASSERT_EQUAL(1, card.calls[0].count);
  TEST_ASSERT_EQUAL(301&255, data[0]);
  for(int i=512;i<3*512;++i) TEST_ASSERT_EQUAL(0,data[i]);
  card.calls.clear();
  TEST_ASSERT_TRUE(map.read(100, 3, data, card));
  TEST_ASSERT_TRUE(card.calls.empty());
  for(uint8_t b:data) TEST_ASSERT_EQUAL(0,b);
}
void test_invalid_map_and_read_error_do_not_report_success() {
  SectorMap map; map.sectors = 2;
  uint8_t data[512]; Read card;
  TEST_ASSERT_FALSE(map.read(0,1,data,card));
  TEST_ASSERT_TRUE(card.calls.empty());
  SectorExtent gap[] = {{0,100,1},{2,200,1}};
  map.extents=gap; map.count=2; map.sectors=3;
  TEST_ASSERT_FALSE(map.read(1,1,data,card));
  TEST_ASSERT_TRUE(card.calls.empty());
  map.extents=nullptr; map.count=0; map.first=100; card.fail=true;
  TEST_ASSERT_FALSE(map.read(0,1,data,card));
}
void test_extent_builder_merges_adjacent_clusters_and_preserves_failures() {
  SectorExtent runs[2]; int count=0;
  TEST_ASSERT_TRUE(appendSectorExtent(runs,count,2,0,100,4));
  TEST_ASSERT_TRUE(appendSectorExtent(runs,count,2,4,104,4));
  TEST_ASSERT_EQUAL(1,count); TEST_ASSERT_EQUAL(8,runs[0].count);
  TEST_ASSERT_TRUE(appendSectorExtent(runs,count,2,8,300,4));
  TEST_ASSERT_FALSE(appendSectorExtent(runs,count,2,12,500,4));
  TEST_ASSERT_EQUAL(2,count); TEST_ASSERT_EQUAL(4,runs[1].count);
  TEST_ASSERT_TRUE(appendSectorExtent(runs,count,2,12,304,4));  // merging needs no new slot
  TEST_ASSERT_EQUAL(8,runs[1].count);
  TEST_ASSERT_FALSE(appendSectorExtent(runs,count,2,99,400,1));  // no logical gaps
}
void test_sector_arithmetic_rejects_overflow() {
  SectorExtent runs[2]; int count=0;
  TEST_ASSERT_FALSE(appendSectorExtent(runs,count,2,0,UINT32_MAX,2));
  TEST_ASSERT_EQUAL(0,count);
  SectorMap map;map.first=UINT32_MAX;map.sectors=2;
  uint8_t data[2*512];Read card;
  TEST_ASSERT_FALSE(map.read(0,2,data,card));
  TEST_ASSERT_TRUE(card.calls.empty());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_contiguous_file_keeps_one_raw_read);
  RUN_TEST(test_fragmented_read_splits_at_extent_boundary);
  RUN_TEST(test_file_tail_is_zero_padded_without_reading_other_files);
  RUN_TEST(test_invalid_map_and_read_error_do_not_report_success);
  RUN_TEST(test_extent_builder_merges_adjacent_clusters_and_preserves_failures);
  RUN_TEST(test_sector_arithmetic_rejects_overflow);
  return UNITY_END();
}
