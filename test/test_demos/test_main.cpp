#include <unity.h>
#include <string.h>
#include <vector>
#include "demos.h"
#include "project_io.h"
#include "render.h"
#include "synth.h"

using namespace mt;

static Project p, q;  // static storage: a Project is ~470 KB

void setUp() {}
void tearDown() {}

struct VecSink : ByteSink {
  std::vector<uint8_t> buf;
  bool write(const void* d, size_t n) override {
    const uint8_t* b = static_cast<const uint8_t*>(d);
    buf.insert(buf.end(), b, b + n);
    return true;
  }
};
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

static bool usesTrack(const Project& pr, int t) {
  for (const Pattern& pt : pr.patterns)
    for (int s = 0; s < pt.length; ++s)
      if (!pt.steps[t][s].isEmpty()) return true;
  return false;
}

void test_names_are_project_file_names() {
  TEST_ASSERT_EQUAL(5, demoCount());
  for (int i = 0; i < demoCount(); ++i) {
    demoBuild(i, p);
    TEST_ASSERT_EQUAL_STRING(demoName(i), p.name);
    const size_t n = strlen(p.name);
    TEST_ASSERT_TRUE(n > 0 && n <= 16);
    for (size_t k = 0; k < n; ++k) {
      const char c = p.name[k];
      TEST_ASSERT_TRUE((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
    }
  }
  TEST_ASSERT_EQUAL_STRING(demoName(0), demoName(99));
}

// Every track with steps plays a preset (no instrument left at the reset default by a typo),
// kits included, and the song chain only names patterns that have notes.
void test_every_used_track_has_its_instruments() {
  for (int i = 0; i < demoCount(); ++i) {
    demoBuild(i, p);
    for (int t = 0; t < kTracks; ++t) {
      if (!usesTrack(p, t)) continue;
      TEST_ASSERT_TRUE_MESSAGE(p.trackInternal(t), demoName(i));
      const Instrument& m = p.instruments[p.tracks[t].instr];
      TEST_ASSERT_FALSE_MESSAGE(strncmp(m.name, "INS", 3) == 0, demoName(i));
      if (m.type != InstrType::Kit) continue;
      for (const KitLane& l : m.kit) {
        if (l.instr == kNoInstr) continue;
        TEST_ASSERT_FALSE_MESSAGE(strncmp(p.instruments[l.instr].name, "INS", 3) == 0, demoName(i));
        TEST_ASSERT_TRUE(p.instruments[l.instr].type != InstrType::Kit);
      }
    }
    TEST_ASSERT_TRUE(p.songMode);
    TEST_ASSERT_TRUE(p.chainLen > 0);
    for (int c = 0; c < p.chainLen; ++c) {
      TEST_ASSERT_TRUE(p.chain[c] < kPatterns);
      TEST_ASSERT_FALSE_MESSAGE(p.patterns[p.chain[c]].isEmpty(), demoName(i));
      TEST_ASSERT_TRUE(p.chainRep[c] >= 1 && p.chainRep[c] <= kChainRepMax);
    }
    const uint64_t us = songUs(p);
    TEST_ASSERT_TRUE_MESSAGE(us > 60000000ull && us < 150000000ull, demoName(i));  // 1..2.5 min
  }
}

// A demo survives a save / load unchanged: everything it uses is in the file format.
void test_save_load_round_trip() {
  for (int i = 0; i < demoCount(); ++i) {
    demoBuild(i, p);
    VecSink out;
    TEST_ASSERT_TRUE(saveProject(p, out));
    VecSource in(out.buf);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, q)));
    TEST_ASSERT_EQUAL(p.bpm, q.bpm);
    TEST_ASSERT_EQUAL(p.masterVol, q.masterVol);
    TEST_ASSERT_EQUAL(p.chainLen, q.chainLen);
    TEST_ASSERT_EQUAL_MEMORY(p.chain, q.chain, sizeof(p.chain));
    TEST_ASSERT_EQUAL_MEMORY(p.chainTr, q.chainTr, sizeof(p.chainTr));
    for (int k = 0; k < kPatterns; ++k) {
      TEST_ASSERT_EQUAL(p.patterns[k].length, q.patterns[k].length);
      TEST_ASSERT_EQUAL_MEMORY(p.patterns[k].trackLen, q.patterns[k].trackLen, sizeof(p.patterns[k].trackLen));
      for (int t = 0; t < kTracks; ++t)
        TEST_ASSERT_TRUE_MESSAGE(
            memcmp(p.patterns[k].steps[t], q.patterns[k].steps[t], sizeof(Step) * p.patterns[k].length) == 0,
            demoName(i));
    }
  }
}

// The first 20 s of each song sound, without clipping, and stay well inside the voice pool (the
// demos must not lean on the CPU guard or on stealing).
void test_songs_render_clean() {
  static int16_t line[2 * kSynthRate];
  static float rv[Reverb::kBufLen];
  for (int i = 0; i < demoCount(); ++i) {
    demoBuild(i, p);
    Synth* synth = new Synth(p);
    synth->setDelayBuffer(line, sizeof(line) / 2);
    synth->setReverbBuffer(rv, Reverb::kBufLen);
    RenderSpec spec;
    spec.mode = RenderSpec::Mode::Song;
    OfflineRender::Guard g(p, spec);
    OfflineRender r(p, *synth, spec);
    int16_t out[Synth::kBlock];
    int maxV = 0, maxHeavy = 0;
    for (int b = 0; b < 5000 && r.renderBlock(out); ++b) {  // 20 s
      int h = 0;
      for (int k = 0; k < kVoices; ++k) h += heavyLoad(synth->voice(k));
      maxV = synth->activeVoices() > maxV ? synth->activeVoices() : maxV;
      maxHeavy = h > maxHeavy ? h : maxHeavy;
    }
    TEST_ASSERT_TRUE_MESSAGE(r.peak() > 8000, demoName(i));
    TEST_ASSERT_EQUAL_MESSAGE(0, r.clips(), demoName(i));
    TEST_ASSERT_TRUE_MESSAGE(maxV <= 16, demoName(i));
    TEST_ASSERT_TRUE_MESSAGE(maxHeavy <= kFmVoiceMax, demoName(i));
    delete synth;
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_are_project_file_names);
  RUN_TEST(test_every_used_track_has_its_instruments);
  RUN_TEST(test_save_load_round_trip);
  RUN_TEST(test_songs_render_clean);
  return UNITY_END();
}
