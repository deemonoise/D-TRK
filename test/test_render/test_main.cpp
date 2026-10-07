#include <unity.h>
#include <vector>
#include "render.h"
#include "sample_set.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Project* mkProj() {  // static storage: a Project is ~460 KB
  static Project p;
  p.reset();
  p.bpm = 120;
  Instrument& m = p.instruments[0];
  instrSetType(m, InstrType::Chip);
  m.wave = static_cast<uint8_t>(Wave::Saw);
  m.attack = 0;
  m.sustain = 127;
  m.release = 0;
  p.tracks[0].out = TrackOut::Int;
  p.tracks[0].instr = 0;
  p.tracks[1].out = TrackOut::Int;
  p.tracks[1].instr = 0;
  p.patterns[0].length = 16;
  return &p;
}

void test_pass_us() {
  Project& p = *mkProj();
  p.patterns[2].length = 16;  // 16 x 24 ticks x 625000 / 120 = 2 000 000 us
  TEST_ASSERT_EQUAL_UINT64(2000000ull, passUs(p, 2));
  p.patterns[2].res = Resolution::Eighth;
  TEST_ASSERT_EQUAL_UINT64(4000000ull, passUs(p, 2));
}

void test_song_us_sums_chain_with_repeats() {
  Project& p = *mkProj();
  p.patterns[1].length = 32;
  p.chain[0] = 0;
  p.chain[1] = 1;
  p.chain[2] = 0;
  p.chainLen = 3;
  p.chainRep[1] = 2;
  TEST_ASSERT_EQUAL_UINT64(2000000ull + 2 * 4000000ull + 2000000ull, songUs(p));
}

void test_normalize_peak() {
  int16_t b[4] = {100, -3000, 1500, 0};
  normalizePeak(b, 4, 3000);
  TEST_ASSERT_INT_WITHIN(1, -29196, b[1]);  // -1 dBFS (0.891 x 32767)
  TEST_ASSERT_EQUAL(0, b[3]);
  int16_t z[2] = {0, 0};
  normalizePeak(z, 2, 0);  // silence: unchanged
  TEST_ASSERT_EQUAL(0, z[0]);
}

void test_trim_tail() {
  int16_t b[400] = {0};
  b[10] = 1000;
  b[300] = 40;  // above -60 dBFS (33)
  b[350] = 20;  // below
  TEST_ASSERT_EQUAL(384, trimTail(b, 400, 128));  // 301 rounded up to a block
  int16_t s[256] = {0};
  TEST_ASSERT_EQUAL(128, trimTail(s, 256, 128));  // never less than one block
}

void test_next_resample_name() {
  Project& p = *mkProj();
  char nm[kSampleNameMax + 1];
  nextResampleName(p, nm);
  TEST_ASSERT_EQUAL_STRING("RS1", nm);
  projSampleSet(p, "rs1", 1, 100);
  nextResampleName(p, nm);
  TEST_ASSERT_EQUAL_STRING("RS2", nm);
}

static Synth* synthFor(Project& p) {
  static Synth* s = nullptr;
  delete s;
  s = new Synth(p);
  return s;
}

static int firstLoudBlock(OfflineRender& r) {
  int16_t out[Synth::kBlock];
  int first = -1, n = 0;
  while (r.renderBlock(out)) {
    for (int i = 0; i < Synth::kBlock; ++i)
      if (first < 0 && (out[i] > 50 || out[i] < -50)) first = n;
    ++n;
  }
  return first;
}

void test_note_lands_in_its_block() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][4].note = 60;  // step 4 = 500 000 us = block 125
  RenderSpec s;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(500, r.blocksTotal());  // 2 s / 4 ms, no tail
  const int first = firstLoudBlock(r);
  TEST_ASSERT_INT_WITHIN(1, 125, first);
  TEST_ASSERT_TRUE(r.peak() > 1000);
  TEST_ASSERT_EQUAL(500, r.blocksDone());
  int16_t out[Synth::kBlock];
  TEST_ASSERT_FALSE(r.renderBlock(out));
}

void test_pattern_spec_picks_pattern() {
  Project& p = *mkProj();
  p.patterns[3].length = 16;
  p.patterns[3].steps[0][8].note = 60;  // step 8 of P04 = block 250
  RenderSpec s;
  s.pattern = 3;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_INT_WITHIN(1, 250, firstLoudBlock(r));
}

void test_mask_silences_other_track() {
  Project& p = *mkProj();
  p.patterns[0].steps[1][0].note = 60;
  RenderSpec s;
  s.tracksMask = 1;  // track 0 only
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(-1, firstLoudBlock(r));
  TEST_ASSERT_EQUAL(0, r.peak());
}

void test_next_pass_never_sounds() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;  // step 0, gate 50 %: ends at 62.5 ms (release 0)
  RenderSpec s;
  s.tailBlocks = 100;  // 2 s .. 2.4 s: the next pass would play the note again at 2 s
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  int16_t out[Synth::kBlock];
  int n = 0, loudBody = 0, loudTail = 0;
  while (r.renderBlock(out)) {
    bool loud = false;
    for (int16_t x : out) loud |= x > 50 || x < -50;
    (n < 500 ? loudBody : loudTail) += loud;
    ++n;
  }
  TEST_ASSERT_EQUAL(600, n);
  TEST_ASSERT_TRUE(loudBody > 0);
  TEST_ASSERT_EQUAL(0, loudTail);
}

// Step 0 sounds in block 0, not one block late.
void test_first_step_on_time() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;
  RenderSpec s;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(0, firstLoudBlock(r));
}

// A negative NDG on step 0: the next pass's hit (before the body end) is never planned.
void test_next_pass_negative_nudge_stays_out() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;
  p.patterns[0].steps[0][0].fx[0] = {Fx::NDG, static_cast<uint8_t>(-40)};
  RenderSpec s;
  s.tailBlocks = 100;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  int16_t out[Synth::kBlock];
  int n = 0, loudLate = 0;
  while (r.renderBlock(out)) {
    bool loud = false;
    for (int16_t x : out) loud |= x > 50 || x < -50;
    if (n >= 450) loudLate += loud;  // 1.8 s on: the next pass's nudged hit would be at 1.95 s
    ++n;
  }
  TEST_ASSERT_EQUAL(0, loudLate);
}

void test_song_total_and_tail() {
  Project& p = *mkProj();
  p.patterns[1].length = 32;
  p.chain[0] = 0;
  p.chain[1] = 1;
  p.chainLen = 2;
  p.chainRep[1] = 2;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  s.tailBlocks = 10;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  TEST_ASSERT_TRUE(p.songMode);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(500 + 2 * 1000 + 10, r.blocksTotal());
}

void test_song_plays_chain_items() {
  Project& p = *mkProj();
  p.patterns[1].length = 16;
  p.patterns[1].steps[0][0].note = 72;  // only P02 has a note
  p.chain[0] = 0;
  p.chain[1] = 1;
  p.chainLen = 2;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_INT_WITHIN(1, 500, firstLoudBlock(r));  // P02 starts at 2 s
}

void test_guard_restores_flags_and_mutes() {
  Project& p = *mkProj();
  p.songMode = false;
  p.tracks[2].mute = true;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  {
    OfflineRender::Guard g(p, s);
    TEST_ASSERT_TRUE(p.songMode);
    p.tracks[2].mute = false;  // a chain scene during the render
    p.tracks[5].mute = true;
  }
  TEST_ASSERT_FALSE(p.songMode);
  TEST_ASSERT_TRUE(p.tracks[2].mute);
  TEST_ASSERT_FALSE(p.tracks[5].mute);
}

void test_two_renders_identical() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;
  for (int i = 2; i < 16; i += 2) {
    p.patterns[0].steps[0][i].note = static_cast<uint8_t>(60 + i);
    p.patterns[0].steps[0][i].fx[0] = {Fx::PRB, 50};
  }
  RenderSpec s;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  std::vector<int16_t> a, b;
  for (std::vector<int16_t>* v : {&a, &b}) {
    synth.reset();
    OfflineRender r(p, synth, s);
    int16_t out[Synth::kBlock];
    while (r.renderBlock(out)) v->insert(v->end(), out, out + Synth::kBlock);
  }
  TEST_ASSERT_EQUAL(a.size(), b.size());
  TEST_ASSERT_TRUE(a == b);
}

void test_peak_and_clips() {
  Project& p = *mkProj();
  p.masterVol = 200;
  for (int t = 0; t < 8; ++t) {
    p.tracks[t].out = TrackOut::Int;
    p.tracks[t].instr = 0;
    p.tracks[t].vol = 127;
    p.patterns[0].steps[t][0].note = static_cast<uint8_t>(40 + t);
  }
  p.instruments[0].vol = 127;
  RenderSpec s;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  firstLoudBlock(r);
  TEST_ASSERT_TRUE(r.peak() > 30000);
  TEST_ASSERT_TRUE(r.clips() > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pass_us);
  RUN_TEST(test_song_us_sums_chain_with_repeats);
  RUN_TEST(test_normalize_peak);
  RUN_TEST(test_trim_tail);
  RUN_TEST(test_next_resample_name);
  RUN_TEST(test_note_lands_in_its_block);
  RUN_TEST(test_pattern_spec_picks_pattern);
  RUN_TEST(test_mask_silences_other_track);
  RUN_TEST(test_next_pass_never_sounds);
  RUN_TEST(test_first_step_on_time);
  RUN_TEST(test_next_pass_negative_nudge_stays_out);
  RUN_TEST(test_song_total_and_tail);
  RUN_TEST(test_song_plays_chain_items);
  RUN_TEST(test_guard_restores_flags_and_mutes);
  RUN_TEST(test_two_renders_identical);
  RUN_TEST(test_peak_and_clips);
  return UNITY_END();
}
