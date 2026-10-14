#include <string.h>
#include <unity.h>
#include <vector>
#include "sample_region.h"
#include "stream_loader.h"
#include "synth.h"

using namespace mt;

static Project* p;
static Synth* s;
static int16_t mem[kStreamSlots * kSlotBlocks * kStreamBlock];
static StreamPool pool;

// One sample "s": whole (RAM) or streamable, optionally with a head.
struct Src : SampleSource {
  std::vector<int16_t> d;
  bool stream = false, psram = false;
  SampleHead head;
  uint8_t nHeads = 0;
  bool find(const char* name, SampleData& o) const override {
    if (strcmp(name, "s") != 0) return false;
    o = SampleData();
    o.key = 0x1234;
    o.frames = static_cast<uint32_t>(d.size());
    o.rate = 44100;
    o.heads = nHeads ? &head : nullptr;
    o.nHeads = nHeads;
    if (stream) {
      o.streamable = true;
    } else {
      o.whole = d.data();
      o.psram = psram;
      o.streamable = psram;
    }
    return true;
  }
};
struct VecReader : BlockReader {
  const std::vector<int16_t>* d = nullptr;
  bool read(uint32_t, uint32_t b, int n, int16_t* out) override {
    for (int i = 0; i < n * kStreamBlock; ++i) {
      const uint32_t f = b * kStreamBlock + i;
      out[i] = f < d->size() ? (*d)[f] : 0;
    }
    return true;
  }
};
static Src src;
static VecReader rd;

void setUp() {
  p = new Project();
  p->masterVol = 100;
  for (int t = 0; t < kTracks; ++t) {
    p->tracks[t].out = TrackOut::Int;
    p->tracks[t].vol = 127;
  }
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Sample);
  strcpy(m.sample, "s");
  m.root = 60;
  m.vol = 127;
  m.attack = 0;
  m.decay = 0;
  m.sustain = 127;
  m.release = 0;
  src = Src();
  src.d.resize(30000);
  uint32_t x = 1;
  for (auto& v : src.d) {  // noise: any lost or repeated frame shows
    x = x * 1664525u + 1013904223u;
    v = static_cast<int16_t>(x >> 18);
  }
  rd.d = &src.d;
  pool.init(mem);
  pool.sync = nullptr;
  pool.psramOk = true;
  s = new Synth(*p);
  s->setBank(&src);
  s->setPool(&pool);
}
void tearDown() {
  delete s;
  delete p;
}

static std::vector<int16_t> play(uint8_t note, int blocks) {
  const uint8_t on[3] = {0x90, note, 127};
  TEST_ASSERT_TRUE(s->event(0, 0, on, 3));
  std::vector<int16_t> out;
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  for (int k = 0; k < blocks; ++k) {
    s->render(l, r);
    out.insert(out.end(), l, l + Synth::kBlock);
  }
  return out;
}
static std::vector<int16_t> ram(uint8_t note, int blocks) {
  src.stream = false;
  s->reset();
  return play(note, blocks);
}
static std::vector<int16_t> streamed(uint8_t note, int blocks) {
  src.stream = true;
  pool.sync = &rd;
  s->reset();
  return play(note, blocks);
}

void test_stream_matches_ram_forward_loop() {
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  p->instruments[0].loopStart = 0x4000;
  const auto a = ram(72, 600);
  const auto b = streamed(72, 600);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
  TEST_ASSERT_EQUAL_UINT32(0, pool.underruns);
}

void test_stream_matches_ram_reverse_pingpong() {
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::PingPong);
  p->instruments[0].loopStart = 0x2000;
  p->instruments[0].reverse = true;
  const auto a = ram(55, 600);
  const auto b = streamed(55, 600);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}

void test_underrun_fades_and_counts() {
  const auto a = ram(60, 60);
  src.stream = true;
  src.head.frame = 0;
  src.head.len = 4410;
  src.head.d = src.d.data();
  src.nHeads = 1;
  pool.sync = nullptr;  // nothing ever loads
  s->reset();
  const auto b = play(60, 60);
  // The head plays like RAM (one frame short: interpolation needs the next one).
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), 4400);
  // Then it fades out within 2 ms and stays silent; the voice keeps going.
  for (size_t i = 4410 + 90; i < b.size(); ++i) TEST_ASSERT_EQUAL_INT16(0, b[i]);
  TEST_ASSERT_EQUAL_UINT32(1, pool.underruns);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_voice_frees_its_slot() {
  src.d.resize(3000);  // ends after 3000 frames, LoopMode::Off
  streamed(60, 60);
  for (int i = 0; i < pool.slots(); ++i) TEST_ASSERT_EQUAL(SlotOwner::Free, pool.slot(i).owner);
}

void test_failed_psram_falls_back_to_stream() {
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  const auto a = ram(64, 300);
  src.stream = false;
  src.psram = true;
  pool.sync = &rd;
  s->reset();
  const uint8_t on[3] = {0x90, 64, 127};
  TEST_ASSERT_TRUE(s->event(0, 0, on, 3));
  std::vector<int16_t> b;
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  for (int k = 0; k < 300; ++k) {
    if (k == 100) pool.psramOk = false;
    s->render(l, r);
    b.insert(b.end(), l, l + Synth::kBlock);
  }
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}

void test_hint_never_reaches_the_voices() {
  const uint8_t h[3] = {kSynthHint, 60, 0};
  TEST_ASSERT_FALSE(s->event(0, 0, h, 3));
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  s->render(l, r);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_prefetch_fills_a_slot_the_note_adopts() {
  src.stream = true;  // no heads, no sync reader: without the hint the start would underrun
  s->reset();
  const int sl = s->prefetch(0, 60, 0);
  TEST_ASSERT_TRUE(sl >= 0);
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Prefetch), static_cast<int>(pool.slot(sl).owner));
  while (streamLoad(pool, sl, rd)) {
  }  // the loader, before the note
  const auto b = play(60, 16);  // 2048 frames: inside the loaded blocks
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Voice), static_cast<int>(pool.slot(sl).owner));
  TEST_ASSERT_EQUAL_UINT32(0, pool.underruns);
  const auto a = ram(60, 16);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}

void test_prefetch_starts_at_the_ofs_point() {
  src.stream = true;
  s->reset();
  const int sl = s->prefetch(0, 60, 0x80);
  TEST_ASSERT_TRUE(sl >= 0);
  PlaySnap ps;
  TEST_ASSERT_TRUE(pool.snap(sl, ps));
  const SampleRegion g = sampleRegion(p->instruments[0], -1, 30000, 0x80);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g.pos >> 32), ps.pos);
}

void test_prefetch_skips_what_needs_no_load() {
  src.stream = false;  // whole (flash / PSRAM)
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 60, 0));
  src.stream = true;
  TEST_ASSERT_EQUAL(-1, s->prefetch(kTracks, 60, 0));
  Instrument& m = p->instruments[0];
  m.sliceCount = 2;
  m.sliceMode = static_cast<uint8_t>(SliceMode::Fx);  // SLC is not in the hint
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 60, 0));
  m.sliceMode = static_cast<uint8_t>(SliceMode::Note);
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 59, 0));  // below the root: no slice, silent
  m.sliceCount = 0;
  instrSetType(m, InstrType::Chip);
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 60, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stream_matches_ram_forward_loop);
  RUN_TEST(test_stream_matches_ram_reverse_pingpong);
  RUN_TEST(test_underrun_fades_and_counts);
  RUN_TEST(test_voice_frees_its_slot);
  RUN_TEST(test_failed_psram_falls_back_to_stream);
  RUN_TEST(test_hint_never_reaches_the_voices);
  RUN_TEST(test_prefetch_fills_a_slot_the_note_adopts);
  RUN_TEST(test_prefetch_starts_at_the_ofs_point);
  RUN_TEST(test_prefetch_skips_what_needs_no_load);
  return UNITY_END();
}
