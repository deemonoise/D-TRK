#include <unity.h>
#include <string.h>
#include <vector>
#include "synth_model.h"
#include "synth.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

// Chunks whose bytes differ between a and b.
std::vector<uint16_t> changed(const SynthModel& a, const SynthModel& b) {
  std::vector<uint16_t> out;
  const uint8_t* pa = reinterpret_cast<const uint8_t*>(&a);
  const uint8_t* pb = reinterpret_cast<const uint8_t*>(&b);
  for (int i = 0; i < kChunks; ++i) {
    uint32_t off, len;
    chunkRange(chunkId(i), off, len);
    if (memcmp(pa + off, pb + off, len) != 0) out.push_back(chunkId(i));
  }
  return out;
}

}  // namespace

void test_chunks_tile_the_model() {
  std::vector<int> cover(sizeof(SynthModel), 0);
  for (int i = 0; i < kChunks; ++i) {
    uint32_t off = 0, len = 0;
    TEST_ASSERT_TRUE(chunkRange(chunkId(i), off, len));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(off + len <= sizeof(SynthModel));
    TEST_ASSERT_TRUE(len <= 0xFFFF);
    for (uint32_t k = off; k < off + len; ++k) cover[k]++;
  }
  for (size_t k = 0; k < cover.size(); ++k) TEST_ASSERT_EQUAL_MESSAGE(1, cover[k], "byte not covered exactly once");
}

void test_copy_chunk_by_chunk_rebuilds_the_model() {
  Project p;
  p.bpm = 133;
  p.djFilter = -20;
  p.instruments[31].vol = 7;
  p.wavetableCount = 3;
  SynthModel m;
  memset(static_cast<void*>(&m), 0, sizeof m);
  for (int i = 0; i < kChunks; ++i) {
    uint32_t off, len;
    chunkRange(chunkId(i), off, len);
    memcpy(reinterpret_cast<uint8_t*>(&m) + off, reinterpret_cast<const uint8_t*>(static_cast<const SynthModel*>(&p)) + off, len);
  }
  TEST_ASSERT_EQUAL_MEMORY(static_cast<const SynthModel*>(&p), &m, sizeof m);
}

void test_send_order_and_ids() {
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Master), chunkId(0));
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Tracks), chunkId(1));
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Instr0), chunkId(2));
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Wavetables), chunkId(kChunks - 1));
  for (int i = 0; i < kChunks; ++i) TEST_ASSERT_EQUAL(i, chunkIndex(chunkId(i)));
  uint32_t off, len;
  TEST_ASSERT_FALSE(chunkRange(4, off, len));
  TEST_ASSERT_FALSE(chunkRange(static_cast<uint16_t>(static_cast<int>(Chunk::Instr0) + kInstruments), off, len));
  TEST_ASSERT_EQUAL(-1, chunkIndex(999));
}

void test_one_edit_one_chunk() {
  Project a;
  Project b = a;
  b.instruments[5].vol = static_cast<uint8_t>(a.instruments[5].vol ^ 0x10);
  std::vector<uint16_t> c = changed(a, b);
  TEST_ASSERT_EQUAL(1, static_cast<int>(c.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Instr0) + 5, c[0]);

  b = a;
  b.dlyFb = 99;
  b.bpm = 90;
  c = changed(a, b);
  TEST_ASSERT_EQUAL(1, static_cast<int>(c.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Master), c[0]);

  b = a;
  b.tracks[3].vol = 1;
  b.samples[0].crc = 5;
  c = changed(a, b);
  TEST_ASSERT_EQUAL(2, static_cast<int>(c.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Tracks), c[0]);
  TEST_ASSERT_EQUAL(static_cast<int>(Chunk::Samples), c[1]);
}

void test_project_only_fields_not_in_model() {
  Project a;
  Project b = a;
  b.patterns[2].steps[0][0].note = 60;
  b.perfMap[0] = 5;
  b.songMode = true;
  TEST_ASSERT_EQUAL(0, static_cast<int>(changed(a, b).size()));
}

void test_synth_runs_on_a_bare_model() {
  static SynthModel m;
  m.tracks[0].instr = 0;
  static Synth s(m);
  const uint8_t on[3] = {0x90, 60, 100};
  TEST_ASSERT_TRUE(s.event(0, 0, on, 3));
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  int peak = 0;
  for (int b = 0; b < 8; ++b) {
    s.render(out, outR);
    for (int16_t v : out) peak = v > peak ? v : (-v > peak ? -v : peak);
  }
  TEST_ASSERT_TRUE(peak > 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_chunks_tile_the_model);
  RUN_TEST(test_copy_chunk_by_chunk_rebuilds_the_model);
  RUN_TEST(test_send_order_and_ids);
  RUN_TEST(test_one_edit_one_chunk);
  RUN_TEST(test_project_only_fields_not_in_model);
  RUN_TEST(test_synth_runs_on_a_bare_model);
  return UNITY_END();
}
