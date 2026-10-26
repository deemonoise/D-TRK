#include <unity.h>
#include <math.h>
#include <string.h>
#include <vector>
#include "demos.h"
#include "follow.h"
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

void test_song_us_sums_slot_passes() {
  Project& p = *mkProj();
  p.patterns[1].length = 32;
  p.song[0] = 0;
  p.song[1] = 1;
  p.song[2] = 1;
  p.song[3] = 0;
  p.songLen = 4;
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
  OfflineRender::Guard g(p);
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
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_INT_WITHIN(1, blockAt(1000000), firstLoudBlock(r));
}

void test_mask_silences_other_track() {
  Project& p = *mkProj();
  p.patterns[0].steps[1][0].note = 60;
  RenderSpec s;
  s.tracksMask = 1;  // track 0 only
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p);
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
  OfflineRender::Guard g(p);
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
  OfflineRender::Guard g(p);
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
  OfflineRender::Guard g(p);
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
  p.song[0] = 0;
  p.song[1] = 1;
  p.song[2] = 1;
  p.songLen = 3;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  s.tailBlocks = 10;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(blocksFor(10000000) + 10, r.blocksTotal());
}

void test_song_plays_slots() {
  Project& p = *mkProj();
  p.patterns[1].length = 16;
  p.patterns[1].steps[0][0].note = 72;  // only P02 has a note
  p.song[0] = 0;
  p.song[1] = 1;
  p.songLen = 2;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_INT_WITHIN(1, blockAt(2000000), firstLoudBlock(r));  // P02 starts at 2 s
}

void test_guard_restores_mutes() {
  Project& p = *mkProj();
  p.tracks[2].mute = true;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  {
    OfflineRender::Guard g(p);
    p.tracks[2].mute = false;  // a slot mask during the render
    p.tracks[5].mute = true;
  }
  TEST_ASSERT_TRUE(p.tracks[2].mute);
  TEST_ASSERT_FALSE(p.tracks[5].mute);
}

// The slot masks apply during a song render; the live mutes come back after it.
void test_song_render_applies_masks_and_restores_mutes() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;  // track 0, muted by slot 0
  p.patterns[1].length = 16;
  p.patterns[1].steps[0][0].note = 72;  // track 0, slot 1 unmutes it
  p.song[0] = 0;
  p.song[1] = 1;
  p.songMute[0] = 1;
  p.songLen = 2;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  Synth& synth = *synthFor(p);
  {
    OfflineRender::Guard g(p);
    OfflineRender r(p, synth, s);
    TEST_ASSERT_INT_WITHIN(1, blockAt(2000000), firstLoudBlock(r));  // slot 0 silent, slot 1 at 2 s
  }
  TEST_ASSERT_FALSE(p.tracks[0].mute);
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
  OfflineRender::Guard g(p);
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

// The track generator renders the same bytes every time (hash flips from the seed, reset by start()).
static std::vector<int16_t> renderAll(Project& p) {
  RenderSpec s;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p);
  std::vector<int16_t> v;
  synth.reset();
  OfflineRender r(p, synth, s);
  int16_t out[Synth::kBlock], outR[Synth::kBlock];
  while (r.renderBlock(out, outR)) {
    v.insert(v.end(), out, out + Synth::kBlock);
    v.insert(v.end(), outR, outR + Synth::kBlock);
  }
  return v;
}

void test_generator_renders_identically() {
  Project& p = *mkProj();
  for (int i = 0; i < 16; i += 2) p.patterns[0].steps[0][i].note = 60;
  p.patterns[0].steps[0][4].fx[0] = {Fx::PRB, 50};
  const std::vector<int16_t> plain = renderAll(p);
  p.gen[0].mode = static_cast<uint8_t>(GenMode::Pitch);
  p.gen[0].flip = 50;
  p.gen[1].mode = static_cast<uint8_t>(GenMode::Gate);
  p.gen[1].flip = 50;
  const std::vector<int16_t> a = renderAll(p), b = renderAll(p);
  TEST_ASSERT_EQUAL(a.size(), b.size());
  TEST_ASSERT_TRUE(a == b);
  TEST_ASSERT_FALSE(a == plain);
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
  static int16_t dline[kSynthRate / 4];
  synth.setDelayBuffer(dline, kSynthRate / 4);
  OfflineRender::Guard g(p);
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
  OfflineRender::Guard g(p);
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
  static int16_t dline[kSynthRate / 4];
  synth.setDelayBuffer(dline, kSynthRate / 4);
  OfflineRender::Guard g(p);
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
  TEST_ASSERT_EQUAL_HEX32(0x181E8031u, h);  // re-pinned 2026-10-26: DRUM machines calibrated (to the hats)
}

// The synth boards render on one synth, reset between renders: drum noise and LFO Random included,
// the second render of a demo is the first one again.
void test_demo_rendered_twice_on_one_synth_identical() {
  static Project p;
  demoBuild(0, p);
  RenderSpec s;
  s.tailBlocks = 20;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p);
  uint32_t h[2];
  for (uint32_t& v : h) {
    synth.reset();
    OfflineRender r(p, synth, s);
    int16_t out[Synth::kBlock], outR[Synth::kBlock];
    v = 2166136261u;
    while (r.renderBlock(out, outR))
      for (int i = 0; i < Synth::kBlock; ++i) v = (v ^ static_cast<uint16_t>(out[i] ^ outR[i] << 1)) * 16777619u;
  }
  TEST_ASSERT_EQUAL_HEX32(h[0], h[1]);
}

// The Teensy's path: OfflineSequence's events through BlockRender == OfflineRender. (Not on a demo:
// drum noise seeds are process-global, so two drum renders in one process differ.)
static void fillSeqProj(Project& p) {
  for (int t = 0; t < 4; ++t) {
    p.tracks[t].out = TrackOut::Int;
    p.tracks[t].instr = 0;
    p.tracks[t].pan = static_cast<uint8_t>(t * 30 + 19);
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
  static int16_t dline[kSynthRate / 4];
  std::vector<int16_t> want;
  int16_t peak = 0;
  uint32_t clips = 0, total = 0;
  {
    Project& p = *mkProj();
    fillSeqProj(p);
    Synth& synth = *synthFor(p);
    synth.setDelayBuffer(dline, kSynthRate / 4);
    OfflineRender::Guard g(p);
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
  OfflineRender::Guard g(p);
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
  static int16_t dline[kSynthRate / 4];
  std::vector<int16_t> want;
  int16_t peak = 0;
  uint32_t clips = 0;
  {
    Project& p = *mkProj();
    fill(p);
    Synth& synth = *synthFor(p);
    synth.setDelayBuffer(dline, kSynthRate / 4);
    OfflineRender::Guard g(p);
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
  OfflineRender::Guard g(p);
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

// Hard right: L stays silent, the peak comes from R alone.
void test_render_hard_right_peak_from_r() {
  Project& p = *mkProj();
  p.tracks[0].pan = 127;
  p.patterns[0].steps[0][0].note = 60;
  RenderSpec s;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  int16_t l[Synth::kBlock], rr[Synth::kBlock];
  int16_t peakL = 0;
  while (r.renderBlock(l, rr))
    for (int i = 0; i < Synth::kBlock; ++i) peakL = l[i] > peakL ? l[i] : peakL;
  TEST_ASSERT_EQUAL_INT16(0, peakL);
  TEST_ASSERT_TRUE(r.peak() > 1000);
}


// ---- Export stem taps (Synth::setTaps) ----

// A synth with every send bus live: delay line, reverb, MOD line.
static Synth* fxSynth(Project& p) {
  static int16_t dline[kSynthRate / 4];
  static float rbuf[Reverb::kBufLen];
  static int16_t mline[ModFx::kLineLen];
  Synth* s = synthFor(p);
  s->setDelayBuffer(dline, kSynthRate / 4);
  s->setReverbBuffer(rbuf, Reverb::kBufLen);
  s->setModBuffer(mline, ModFx::kLineLen);
  return s;
}

static void fxProj(Project& p) {
  fillSeqProj(p);
  p.instruments[0].rsend = 70;
  p.tracks[2].modSend = 90;
  p.modType = static_cast<uint8_t>(ModType::Chorus);
  p.modLevel = 100;
  p.rvbLevel = 100;
}

struct TapBufs {
  float l[RenderTaps::kCount][Synth::kBlock], r[RenderTaps::kCount][Synth::kBlock];
  RenderTaps t;
  explicit TapBufs(uint32_t mask) {
    t.mask = mask;
    for (int i = 0; i < RenderTaps::kCount; ++i) {
      t.l[i] = l[i];
      t.r[i] = r[i];
    }
  }
};

static uint32_t renderHash(Project& p, Synth& synth, RenderTaps* taps, uint32_t* blocks = nullptr) {
  RenderSpec s;
  s.tailBlocks = 60;
  synth.reset();
  synth.setTaps(taps);
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  int16_t l[Synth::kBlock], rr[Synth::kBlock];
  uint32_t h = 2166136261u;
  if (blocks) *blocks = 0;
  while (r.renderBlock(l, rr)) {
    for (int i = 0; i < Synth::kBlock; ++i) h = (h ^ static_cast<uint16_t>(l[i] ^ rr[i] << 1)) * 16777619u;
    if (blocks) ++*blocks;
  }
  synth.setTaps(nullptr);
  return h;
}

// The live path is untouched: every tap on, or a few, the master is the same bit for bit.
void test_taps_keep_master_identical() {
  Project& p = *mkProj();
  fxProj(p);
  p.compAmt = 60;
  p.scTrack = 2;
  p.scDepth = 80;
  p.eqLow = 80;
  Synth& synth = *fxSynth(p);
  const uint32_t plain = renderHash(p, synth, nullptr);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain, renderHash(p, synth, nullptr), "replay");
  static TapBufs all(0x7FFFFu), some(0x50005u), trk(0xFFFFu), mod(1u << RenderTaps::kMod), dly(1u << RenderTaps::kDly),
      rvb(1u << RenderTaps::kRvb);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain, renderHash(p, synth, &all.t), "all");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain, renderHash(p, synth, &some.t), "some");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain, renderHash(p, synth, &trk.t), "tracks");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain, renderHash(p, synth, &mod.t), "mod");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain, renderHash(p, synth, &dly.t), "dly");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain, renderHash(p, synth, &rvb.t), "rvb");
  TEST_ASSERT_EQUAL_HEX32(plain, renderHash(p, synth, nullptr));
}

static float clip(float x) { return x >= 1.f ? 1.f : (x <= -1.f ? -1.f : x * (1.5f - 0.5f * x * x)); }

// Comp 0, EQ flat: the master is the soft clip of (sum of the track taps + the returns) x MAIN gain.
void test_taps_sum_to_master() {
  Project& p = *mkProj();
  fxProj(p);
  p.masterVol = 80;
  Synth& synth = *fxSynth(p);
  static TapBufs all(0x7FFFFu);
  synth.reset();
  synth.setTaps(&all.t);
  RenderSpec s;
  s.tailBlocks = 60;
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  int16_t l[Synth::kBlock], rr[Synth::kBlock];
  float worst = 0, loud = 0, fx[3] = {0, 0, 0};
  while (r.renderBlock(l, rr)) {
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 80 * 0.25f / 100.f, all.t.gain);
    for (int i = 0; i < Synth::kBlock; ++i) {
      float sl = 0, sr = 0;
      for (int k = 0; k < RenderTaps::kCount; ++k) {
        sl += all.l[k][i];
        sr += all.r[k][i];
      }
      for (int k = 0; k < 3; ++k) fx[k] = fmaxf(fx[k], fabsf(all.l[RenderTaps::kMod + k][i]));
      const float wl = clip(sl * all.t.gain) * 32767.f, wr = clip(sr * all.t.gain) * 32767.f;
      worst = fmaxf(worst, fmaxf(fabsf(wl - synth.masterL()[i]), fabsf(wr - synth.masterR()[i])));
      loud = fmaxf(loud, fabsf(synth.masterL()[i]));
    }
  }
  synth.setTaps(nullptr);
  TEST_ASSERT_TRUE(loud > 1000);
  TEST_ASSERT_TRUE(worst < 0.05f);
  for (float v : fx) TEST_ASSERT_TRUE(v > 0);  // every return has its own signal
}

// A tap holds one track only; a track that sends nowhere leaves the returns at zero.
void test_taps_split_tracks_and_returns() {
  Project& p = *mkProj();
  fxProj(p);
  for (int t = 0; t < kTracks; ++t) p.tracks[t].modSend = 0;
  p.instruments[0].send = 0;
  p.instruments[0].rsend = 0;
  Synth& synth = *fxSynth(p);
  static TapBufs all(0x7FFFFu);
  synth.reset();
  synth.setTaps(&all.t);
  RenderSpec s;
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  int16_t l[Synth::kBlock], rr[Synth::kBlock];
  float trk[kTracks] = {}, ret = 0;
  while (r.renderBlock(l, rr))
    for (int i = 0; i < Synth::kBlock; ++i) {
      for (int t = 0; t < kTracks; ++t) trk[t] = fmaxf(trk[t], fabsf(all.l[t][i]) + fabsf(all.r[t][i]));
      for (int k = RenderTaps::kMod; k < RenderTaps::kCount; ++k) ret = fmaxf(ret, fabsf(all.l[k][i]));
    }
  synth.setTaps(nullptr);
  for (int t = 0; t < 4; ++t) TEST_ASSERT_TRUE(trk[t] > 0);
  for (int t = 4; t < kTracks; ++t) TEST_ASSERT_EQUAL_FLOAT(0.f, trk[t]);
  TEST_ASSERT_EQUAL_FLOAT(0.f, ret);
}

// Follow actions: the render walks the path (salt 0) up to maxSlots passes.
void test_song_follow_stay_limited_by_max_slots() {
  Project& p = *mkProj();  // P01: 16 steps = 2 s
  p.song[0] = 0;
  p.song[1] = 0;
  p.songLen = 2;
  p.songFol[0].act = static_cast<uint8_t>(FollowAct::Stay);
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  s.maxSlots = 3;
  s.tailBlocks = 10;
  Synth& synth = *synthFor(p);
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(blocksFor(3 * 2000000) + 10, r.blocksTotal());
  TEST_ASSERT_EQUAL_UINT64(3 * 2000000ull, songUs(p, 3));
}

void test_song_follow_jump_path_length() {
  Project& p = *mkProj();
  p.patterns[1].length = 32;  // 4 s
  p.song[0] = 0;
  p.song[1] = 1;
  p.song[2] = 0;
  p.songLen = 3;
  p.songFol[1].act = static_cast<uint8_t>(FollowAct::Jump);
  p.songFol[1].target = 0;  // 0 1 0 1 0 ...
  TEST_ASSERT_EQUAL_UINT64(3 * 2000000ull + 2 * 4000000ull, songUs(p, 5));
  p.songFol[1].chance = 0;  // never jumps: the plain song, then the end
  TEST_ASSERT_EQUAL_UINT64(2000000ull + 4000000ull + 2000000ull, songUs(p, 5));
  TEST_ASSERT_EQUAL_UINT64(2000000ull + 4000000ull, songUs(p, 2));
}

void test_song_follow_render_repeatable() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;
  p.patterns[1].length = 16;
  p.patterns[1].steps[0][4].note = 67;
  p.song[0] = 0;
  p.song[1] = 1;
  p.song[2] = 0;
  p.songLen = 3;
  for (int i = 0; i < 3; ++i) {
    p.songFol[i].act = static_cast<uint8_t>(FollowAct::Random);
    p.songFol[i].chance = 60;
  }
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  s.maxSlots = 6;
  std::vector<int16_t> one, two;
  for (std::vector<int16_t>* v : {&one, &two}) {
    Synth& synth = *synthFor(p);
    OfflineRender::Guard g(p);
    OfflineRender r(p, synth, s);
    int16_t out[Synth::kBlock], outR[Synth::kBlock];
    while (r.renderBlock(out, outR)) {
      v->insert(v->end(), out, out + Synth::kBlock);
      v->insert(v->end(), outR, outR + Synth::kBlock);
    }
  }
  TEST_ASSERT_EQUAL(one.size(), two.size());
  TEST_ASSERT_TRUE(one == two);
}
// ---- One-shot bounce: the preview track's tap (RenderTaps::kPrev) ----

static void bounceProj(Project& p, uint8_t send) {
  Instrument& m = p.instruments[1];
  instrSetType(m, InstrType::Fm);
  m.send = send;
  m.rsend = send;
  m.release = 40;
  p.dlyLevel = 100;
  p.rvbLevel = 100;
}

struct PrevHash {
  uint32_t tap = 2166136261u, master = 2166136261u;
  float tapPeak = 0;
};

// 40 blocks of instrument 1 on kPreviewTrack through insert ins (kTracks: dry).
static PrevHash renderPreview(Project& p, Synth& synth, uint8_t ins) {
  static TapBufs tb(1u << RenderTaps::kPrev);
  synth.reset();
  synth.setTaps(&tb.t);
  const uint8_t e0[2] = {0xF7, ins}, e1[2] = {0xC0, 1}, e2[3] = {0x90, 60, 127};
  synth.event(0, kPreviewTrack, e0, 2);
  synth.event(0, kPreviewTrack, e1, 2);
  synth.event(0, kPreviewTrack, e2, 3);
  PrevHash h;
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  for (int b = 0; b < 40; ++b) {
    synth.render(l, r);
    for (int i = 0; i < Synth::kBlock; ++i) {
      uint32_t tl, tr;
      memcpy(&tl, &tb.l[RenderTaps::kPrev][i], 4);
      memcpy(&tr, &tb.r[RenderTaps::kPrev][i], 4);
      h.tap = (h.tap ^ tl ^ tr << 1) * 16777619u;
      h.master = (h.master ^ static_cast<uint16_t>(l[i] ^ r[i] << 1)) * 16777619u;
      const float a = fabsf(tb.l[RenderTaps::kPrev][i]);
      if (a > h.tapPeak) h.tapPeak = a;
    }
  }
  synth.setTaps(nullptr);
  return h;
}

// The tap is the dry strip: the sends change the master, not the tap.
void test_tap_preview_dry() {
  Project& p = *mkProj();
  bounceProj(p, 127);
  Synth& synth = *fxSynth(p);
  const PrevHash wet = renderPreview(p, synth, kTracks);
  TEST_ASSERT_TRUE(wet.tapPeak > 0.001f);
  bounceProj(p, 0);
  const PrevHash dry = renderPreview(p, synth, kTracks);
  TEST_ASSERT_EQUAL_HEX32(wet.tap, dry.tap);
  TEST_ASSERT_NOT_EQUAL(wet.master, dry.master);
}

// Through a track's insert (drive on track 3) the tap differs from dry.
void test_tap_preview_insert() {
  Project& p = *mkProj();
  bounceProj(p, 0);
  p.tracks[3].drive = 120;
  Synth& synth = *fxSynth(p);
  const PrevHash dry = renderPreview(p, synth, kTracks);
  const PrevHash ins = renderPreview(p, synth, 3);
  TEST_ASSERT_NOT_EQUAL(dry.tap, ins.tap);
}

struct TapOut {
  const RenderTaps* t;
  int16_t peak = 0;
  bool ok = true;
};

static bool tapOut(const int16_t* l, const int16_t* r, void* ctx) {
  TapOut& o = *static_cast<TapOut*>(ctx);
  for (int i = 0; i < Synth::kBlock; ++i) {
    const float v = o.t->l[RenderTaps::kPrev][i] * kPrevTapScale;
    const int16_t want = static_cast<int16_t>(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : (v < 0 ? v - 0.5f : v + 0.5f)));
    if (l[i] != want) o.ok = false;
    const int a = l[i] < 0 ? -l[i] : l[i];
    if (a > o.peak) o.peak = static_cast<int16_t>(a);
    const int b = r[i] < 0 ? -r[i] : r[i];
    if (b > o.peak) o.peak = static_cast<int16_t>(b);
  }
  return true;
}

// RenderFeeder::setTap: out() and the stats see the tap, not the master.
void test_feeder_tap_mode() {
  Project& p = *mkProj();
  bounceProj(p, 127);
  Synth& synth = *fxSynth(p);
  static TapBufs tb(1u << RenderTaps::kPrev);
  synth.reset();
  synth.setTaps(&tb.t);
  RenderFeeder fd(synth);
  fd.reset();
  fd.setTap(&tb.t, RenderTaps::kPrev);
  link::RenderBlocks m;
  m.first = 0;
  m.n = 0;
  const uint8_t ev[3][3] = {{0xF7, kTracks, 0}, {0xC0, 1, 0}, {0x90, 60, 127}};
  const uint8_t len[3] = {2, 2, 3};
  for (int k = 0; k < 3; ++k) {
    m.ev[k].off = 0;
    m.ev[k].track = kPreviewTrack;
    m.ev[k].len = len[k];
    memcpy(m.ev[k].b, ev[k], 3);
  }
  m.evN[0] = 3;
  for (int b = 1; b < 8; ++b) m.evN[b] = 0;
  m.n = 8;
  TapOut out{&tb.t};
  TEST_ASSERT_EQUAL(static_cast<int>(link::RenderResult::Ok), static_cast<int>(fd.apply(m, tapOut, &out)));
  TEST_ASSERT_TRUE(out.ok);
  TEST_ASSERT_TRUE(out.peak > kSilence);
  TEST_ASSERT_EQUAL(out.peak, fd.peak());
  TEST_ASSERT_EQUAL(8, fd.loudBlocks());
  TEST_ASSERT_TRUE(fd.monoPeak() > 0);
  synth.setTaps(nullptr);
}

// The preview tap's int16 scale undoes the preview fader: a sampler at vol 127 (kSampleGain 4 per
// 32768) plays an unnormalized one-shot back at the level of the source (KIT bake).
void test_prev_tap_unity() {
  const float back = kPrevTapScale * (kPreviewVol / 127.f) * (4.f / 32768.f) * (127 / 127.f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.f, back);
}

void test_normalize_hot() {
  int16_t b[2] = {-3000, 1500};
  normalizeSamples(b, 2, 3000, kNormPeakHot);
  TEST_ASSERT_INT_WITHIN(1, -31653, b[0]);  // -0.3 dBFS (0.966 x 32767)
  TEST_ASSERT_INT_WITHIN(1, 15827, b[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pass_us);
  RUN_TEST(test_tap_preview_dry);
  RUN_TEST(test_tap_preview_insert);
  RUN_TEST(test_feeder_tap_mode);
  RUN_TEST(test_prev_tap_unity);
  RUN_TEST(test_normalize_hot);
  RUN_TEST(test_song_us_sums_slot_passes);
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
  RUN_TEST(test_song_plays_slots);
  RUN_TEST(test_song_render_applies_masks_and_restores_mutes);
  RUN_TEST(test_guard_restores_mutes);
  RUN_TEST(test_two_renders_identical);
  RUN_TEST(test_generator_renders_identically);
  RUN_TEST(test_peak_and_clips);
  RUN_TEST(test_centred_project_renders_l_equals_r);
  RUN_TEST(test_demo_render_unchanged_by_split);
  RUN_TEST(test_demo_rendered_twice_on_one_synth_identical);
  RUN_TEST(test_sequence_plus_synth_matches_render);
  RUN_TEST(test_link_render_matches);
  RUN_TEST(test_link_render_splits_dense_block);
  RUN_TEST(test_feeder_refuses_out_of_order);
  RUN_TEST(test_render_hard_right_peak_from_r);
  RUN_TEST(test_taps_keep_master_identical);
  RUN_TEST(test_taps_sum_to_master);
  RUN_TEST(test_taps_split_tracks_and_returns);
  RUN_TEST(test_song_follow_stay_limited_by_max_slots);
  RUN_TEST(test_song_follow_jump_path_length);
  RUN_TEST(test_song_follow_render_repeatable);
  return UNITY_END();
}
