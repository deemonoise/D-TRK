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

void test_source_accepts_a_published_fragment_map() {
  projSampleSet(*model, "PAD", 0x3333u, 4000);
  SectorExtent extents[] = {{0, 900, 8}, {8, 3000, 9}};
  SectorMap map;
  map.sectors = 17; map.extents = extents; map.count = 2;
  const int i = table->add(0x3333u, 4000, 44100, 0, &map);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_TRUE(table->at(i).sectors == &map);
  StoreSampleSource source(*table, *pool, nullptr, *model);
  SampleData data;
  TEST_ASSERT_TRUE(source.find("PAD", data));
  TEST_ASSERT_TRUE(data.streamable);
  TEST_ASSERT_NULL(data.whole);
  table->clear();
  TEST_ASSERT_FALSE(source.find("PAD", data));
}

#include "test_edit.inc"
#include "test_operations.inc"

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stretch_unity_is_exact_copy_with_preserved_source_and_metadata);
  RUN_TEST(test_stretch_ratios_have_exact_duration_crc_rate_and_bounded_reads);
  RUN_TEST(test_stretch_invalid_ratio_and_overflow_never_create_output);
  RUN_TEST(test_stretch_cancel_and_failures_close_source_and_leave_it_unchanged);
  RUN_TEST(test_tempo_is_read_only_and_short_or_silent_input_is_valid_unknown);
  RUN_TEST(test_tempo_click_train_and_cancel_have_bounded_io_without_output);
  RUN_TEST(test_tempo_invalid_and_failed_read_never_touch_output_or_old_temp_files);
  RUN_TEST(test_stretch_short_silence_and_dedup_keep_existing_assets);
  RUN_TEST(test_edit_extracts_half_open_region_preserves_source_metadata_and_builds_peaks);
  RUN_TEST(test_edit_normalizes_selected_pcm_with_int16_min_and_silence);
  RUN_TEST(test_edit_normalizes_only_the_selection_and_copies_one_frame);
  RUN_TEST(test_edit_rejects_empty_reversed_out_of_bounds_and_noncanonical_input);
  RUN_TEST(test_edit_actual_file_bounds_are_checked_before_copy);
  RUN_TEST(test_edit_full_range_dedup_preserves_existing_wav_and_overview_on_cancel_or_success);
  RUN_TEST(test_edit_cancel_drops_only_own_temporary_files_and_never_published_result);
  RUN_TEST(test_edit_faults_clean_temporary_output_and_are_retryable);
  RUN_TEST(test_edit_progress_is_bounded_per_step_and_does_not_restart_an_active_job);
  RUN_TEST(test_edit_cancel_and_overview_failures_keep_committed_wav_and_prior_overview);
  RUN_TEST(test_edit_can_be_followed_by_ordinary_import_and_added_callback_has_closed_source);
  RUN_TEST(test_edit_reservation_failure_removes_temp_and_leaves_source_intact);
  RUN_TEST(test_edit_dedup_metadata_conflict_never_changes_existing_keyed_files);
  RUN_TEST(test_edit_large_progress_total_never_wraps_or_allocates_whole_file);
  RUN_TEST(test_edit_scan_read_failure_closes_source_without_creating_output);
  RUN_TEST(test_path_is_lower_hex);
  RUN_TEST(test_header_puts_data_at_512_and_parses_back);
  RUN_TEST(test_table_add_find_and_full);
  RUN_TEST(test_table_keeps_heads);
  RUN_TEST(test_source_streams_and_prefers_psram);
  RUN_TEST(test_source_skips_stale_or_scattered_entries);
  RUN_TEST(test_source_accepts_a_published_fragment_map);
  return UNITY_END();
}
