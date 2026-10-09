#include <unity.h>
#include <vector>
#include "demos.h"
#include "render.h"
#include "render_link.h"
#include "sample_set.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Rate-dependent block numbers: blocks covering us (body length) and the block holding time us.
static int blocksFor(uint32_t us) { return static_cast<int>((us + kRenderBlockUs - 1) / kRenderBlockUs); }
static int blockAt(uint32_t us) { return static_cast<int>(us / kRenderBlockUs); }

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
  int16_t b[4] = {100, -3000, 1500, 0};  // 2 frames, L R
  normalizePeak(b, 2, 3000);
  TEST_ASSERT_INT_WITHIN(1, -29196, b[1]);  // -1 dBFS (0.891 x 32767)
  TEST_ASSERT_INT_WITHIN(1, 14598, b[2]);
  TEST_ASSERT_EQUAL(0, b[3]);
  int16_t z[2] = {0, 0};
  normalizePeak(z, 1, 0);  // silence: unchanged
  TEST_ASSERT_EQUAL(0, z[0]);
}

void test_trim_tail() {
  int16_t b[800] = {0};  // 400 frames, interleaved
  b[2 * 10] = 1000;
  b[2 * 300 + 1] = 40;  // R, above -60 dBFS (33)
  b[2 * 350] = 20;      // below
  TEST_ASSERT_EQUAL(384, trimTail(b, 400, 128));  // 301 rounded up to a block
  int16_t s[512] = {0};
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
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  int first = -1, n = 0;
  while (r.renderBlock(out, outR)) {
    for (int i = 0; i < Synth::kBlock; ++i)
      if (first < 0 && (out[i] > 50 || out[i] < -50)) first = n;
    ++n;
  }
  return first;
}

void test_note_lands_in_its_block() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][4].note = 60;  // step 4 = 500 000 us
  RenderSpec s;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(blocksFor(2000000), r.blocksTotal());  // 2 s, no tail
  const int first = firstLoudBlock(r);
  TEST_ASSERT_INT_WITHIN(1, blockAt(500000), first);
  TEST_ASSERT_TRUE(r.peak() > 1000);
  TEST_ASSERT_EQUAL(blocksFor(2000000), r.blocksDone());
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  TEST_ASSERT_FALSE(r.renderBlock(out, outR));
}

void test_pattern_spec_picks_pattern() {
  Project& p = *mkProj();
  p.patterns[3].length = 16;
  p.patterns[3].steps[0][8].note = 60;  // step 8 of P04 = 1 s
  RenderSpec s;
  s.pattern = 3;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_INT_WITHIN(1, blockAt(1000000), firstLoudBlock(r));
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
  s.tailBlocks = 100;  // 2 s .. 2.29 s: the next pass would play the note again at 2 s
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  int n = 0, loudBody = 0, loudTail = 0;
  while (r.renderBlock(out, outR)) {
    bool loud = false;
    for (int16_t x : out) loud |= x > 50 || x < -50;
    (n < blocksFor(2000000) ? loudBody : loudTail) += loud;
    ++n;
  }
  TEST_ASSERT_EQUAL(blocksFor(2000000) + 100, n);
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
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  int n = 0, loudLate = 0;
  while (r.renderBlock(out, outR)) {
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
  TEST_ASSERT_EQUAL(blocksFor(10000000) + 10, r.blocksTotal());
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
  TEST_ASSERT_INT_WITHIN(1, blockAt(2000000), firstLoudBlock(r));  // P02 starts at 2 s
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
    int16_t out[Synth::kBlock], outR[Synth::kBlock];
    while (r.renderBlock(out, outR)) {
      v->insert(v->end(), out, out + Synth::kBlock);
      v->insert(v->end(), outR, outR + Synth::kBlock);
    }
  }
  TEST_ASSERT_EQUAL(a.size(), b.size());
  TEST_ASSERT_TRUE(a == b);
}

// All pans at the centre (the default): L == R, the delay included (the reverb spreads by design).
void test_centred_project_renders_l_equals_r() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;
  p.patterns[0].steps[0][8].note = 67;
  p.instruments[0].send = 100;
  p.dlyLevel = 100;
  RenderSpec s;
  s.tailBlocks = 50;
  Synth& synth = *synthFor(p);
  static int16_t dline[2 * kSynthRate / 4];
  synth.setDelayBuffer(dline, kSynthRate / 4);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  while (r.renderBlock(out, outR)) TEST_ASSERT_EQUAL_INT16_ARRAY(out, outR, Synth::kBlock);
  TEST_ASSERT_TRUE(r.peak() > 1000);
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

// FNV-1a over a demo render (pattern 0 + a tail), pinned before OfflineRender was split into
// OfflineSequence + synth: the split must not change a sample.
static uint32_t demoRenderHash(uint32_t* blocks, int16_t* peak) {
  static Project p;
  demoBuild(0, p);
  RenderSpec s;
  s.tailBlocks = 20;
  Synth& synth = *synthFor(p);
  static int16_t dline[2 * kSynthRate / 4];
  synth.setDelayBuffer(dline, kSynthRate / 4);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  uint32_t h = 2166136261u;
  *blocks = 0;
  while (r.renderBlock(out, outR)) {
    for (int i = 0; i < Synth::kBlock; ++i)
      for (int16_t v : {out[i], outR[i]}) {
        h = (h ^ static_cast<uint16_t>(v & 0xFF)) * 16777619u;
        h = (h ^ static_cast<uint16_t>((v >> 8) & 0xFF)) * 16777619u;
      }
    ++*blocks;
  }
  *peak = r.peak();
  return h;
}

void test_demo_render_unchanged_by_split() {
  uint32_t blocks = 0;
  int16_t peak = 0;
  const uint32_t h = demoRenderHash(&blocks, &peak);
  TEST_ASSERT_TRUE(peak > 1000);
  TEST_ASSERT_EQUAL_HEX32(0xABF36BA1u, h);
}

// The Teensy's path: OfflineSequence's events through BlockRender == OfflineRender. (Not on a demo:
// drum noise seeds are process-global, so two drum renders in one process differ.)
static void fillSeqProj(Project& p) {
  for (int t = 0; t < 4; ++t) {
    p.tracks[t].out = TrackOut::Int;
    p.tracks[t].instr = 0;
    p.tracks[t].pan = static_cast<int8_t>(t * 30 - 45);
    for (int i = t; i < 16; i += 3) p.patterns[0].steps[t][i].note = static_cast<uint8_t>(48 + t * 5 + i);
  }
  p.patterns[0].steps[1][4].fx[0] = {Fx::PRB, 50};
  p.instruments[0].send = 80;
  p.instruments[0].release = 60;
  p.dlyLevel = 90;
}

void test_sequence_plus_synth_matches_render() {
  RenderSpec s;
  s.tailBlocks = 40;
  static int16_t dline[2 * kSynthRate / 4];
  std::vector<int16_t> want;
  int16_t peak = 0;
  uint32_t clips = 0, total = 0;
  {
    Project& p = *mkProj();
    fillSeqProj(p);
    Synth& synth = *synthFor(p);
    synth.setDelayBuffer(dline, kSynthRate / 4);
    OfflineRender::Guard g(p, s);
    OfflineRender r(p, synth, s);
    int16_t l[Synth::kBlock], rr[Synth::kBlock];
    while (r.renderBlock(l, rr)) {
      want.insert(want.end(), l, l + Synth::kBlock);
      want.insert(want.end(), rr, rr + Synth::kBlock);
    }
    peak = r.peak();
    clips = r.clips();
    total = r.blocksTotal();
  }
  Project& p = *mkProj();
  fillSeqProj(p);
  Synth& synth = *synthFor(p);
  synth.setDelayBuffer(dline, kSynthRate / 4);
  OfflineRender::Guard g(p, s);
  OfflineSequence q(p, s);
  BlockRender br;
  static SeqEv ev[OfflineSequence::kMaxEvents];
  int16_t l[Synth::kBlock], rr[Synth::kBlock];
  int n = 0, events = 0;
  uint32_t blocks = 0;
  while (q.nextBlock(ev, n)) {
    events += n;
    br.render(synth, ev, n, l, rr);
    TEST_ASSERT_EQUAL_INT16_ARRAY(&want[blocks * 2 * Synth::kBlock], l, Synth::kBlock);
    TEST_ASSERT_EQUAL_INT16_ARRAY(&want[blocks * 2 * Synth::kBlock + Synth::kBlock], rr, Synth::kBlock);
    ++blocks;
  }
  TEST_ASSERT_EQUAL(0, n);
  TEST_ASSERT_EQUAL(total, blocks);
  TEST_ASSERT_TRUE(events > 10);
  TEST_ASSERT_TRUE(peak > 1000);
  TEST_ASSERT_EQUAL(peak, br.peak);
  TEST_ASSERT_EQUAL(clips, br.clips);
}

// Over the link: RenderPacker frames, encoded and decoded, through a RenderFeeder == OfflineRender.
struct FeedOut {
  std::vector<int16_t> pcm;
};
static bool feedOut(const int16_t* l, const int16_t* r, void* ctx) {
  FeedOut& o = *static_cast<FeedOut*>(ctx);
  o.pcm.insert(o.pcm.end(), l, l + Synth::kBlock);
  o.pcm.insert(o.pcm.end(), r, r + Synth::kBlock);
  return true;
}

static void linkRenderMatches(void (*fill)(Project&), uint32_t tail, int* contFrames) {
  RenderSpec s;
  s.tailBlocks = tail;
  static int16_t dline[2 * kSynthRate / 4];
  std::vector<int16_t> want;
  int16_t peak = 0;
  uint32_t clips = 0;
  {
    Project& p = *mkProj();
    fill(p);
    Synth& synth = *synthFor(p);
    synth.setDelayBuffer(dline, kSynthRate / 4);
    OfflineRender::Guard g(p, s);
    OfflineRender r(p, synth, s);
    int16_t l[Synth::kBlock], rr[Synth::kBlock];
    while (r.renderBlock(l, rr)) {
      want.insert(want.end(), l, l + Synth::kBlock);
      want.insert(want.end(), rr, rr + Synth::kBlock);
    }
    peak = r.peak();
    clips = r.clips();
  }
  Project& p = *mkProj();
  fill(p);
  Synth& synth = *synthFor(p);
  synth.setDelayBuffer(dline, kSynthRate / 4);
  OfflineRender::Guard g(p, s);
  static OfflineSequence* q = nullptr;
  delete q;
  q = new OfflineSequence(p, s);
  RenderPacker pk(*q);
  RenderFeeder fd(synth);
  fd.reset();
  FeedOut out;
  static link::RenderBlocks m, m2;
  uint8_t buf[link::kMaxPayload];
  *contFrames = 0;
  while (pk.next(m)) {
    link::Writer w(buf, sizeof buf);
    encode(m, w);
    TEST_ASSERT_TRUE(w.ok());
    link::Reader rd(buf, w.size());
    TEST_ASSERT_TRUE(decode(rd, m2));
    if (m2.evN[m2.n - 1] & link::RenderBlocks::kCont) ++*contFrames;
    TEST_ASSERT_EQUAL(static_cast<int>(link::RenderResult::Ok), static_cast<int>(fd.apply(m2, feedOut, &out)));
  }
  TEST_ASSERT_EQUAL(q->blocksTotal(), fd.blocks());
  TEST_ASSERT_EQUAL(q->blocksTotal(), pk.blocksPacked());
  TEST_ASSERT_EQUAL(want.size(), out.pcm.size());
  TEST_ASSERT_TRUE(want == out.pcm);
  TEST_ASSERT_EQUAL(peak, fd.peak());
  TEST_ASSERT_EQUAL(clips, fd.clips());
  TEST_ASSERT_TRUE(fd.loudBlocks() > 0 && fd.loudBlocks() <= fd.blocks());
  TEST_ASSERT_TRUE(fd.monoPeak() > 0 && fd.monoPeak() <= fd.peak());
}

void test_link_render_matches() {
  int cont = 0;
  linkRenderMatches(fillSeqProj, 40, &cont);
}

// 16 tracks of 4-note chords on one step: more events in a block than fit one frame.
static void fillDense(Project& p) {
  for (int t = 0; t < kTracks; ++t) {
    p.tracks[t].out = TrackOut::Int;
    p.tracks[t].instr = 0;
    for (int k = 0; k < 4; ++k) p.patterns[0].steps[t][0].fx[k] = {Fx::CHD, static_cast<uint8_t>(k + 1)};
    p.patterns[0].steps[t][0].note = static_cast<uint8_t>(40 + t);
    p.patterns[0].steps[t][1].note = static_cast<uint8_t>(52 + t);
  }
}

void test_link_render_splits_dense_block() {
  int cont = 0;
  linkRenderMatches(fillDense, 4, &cont);
  TEST_ASSERT_TRUE(cont > 0);
}

// A frame not starting at the block being filled is refused, nothing applied.
void test_feeder_refuses_out_of_order() {
  Project& p = *mkProj();
  Synth& synth = *synthFor(p);
  RenderFeeder fd(synth);
  fd.reset();
  link::RenderBlocks m;
  m.first = 1;
  m.n = 1;
  FeedOut out;
  TEST_ASSERT_EQUAL(static_cast<int>(link::RenderResult::BadOrder), static_cast<int>(fd.apply(m, feedOut, &out)));
  TEST_ASSERT_EQUAL(0, fd.blocks());
  m.first = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(link::RenderResult::Ok), static_cast<int>(fd.apply(m, feedOut, &out)));
  TEST_ASSERT_EQUAL(1, fd.blocks());
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
  RUN_TEST(test_centred_project_renders_l_equals_r);
  RUN_TEST(test_demo_render_unchanged_by_split);
  RUN_TEST(test_sequence_plus_synth_matches_render);
  RUN_TEST(test_link_render_matches);
  RUN_TEST(test_link_render_splits_dense_block);
  RUN_TEST(test_feeder_refuses_out_of_order);
  return UNITY_END();
}
