// One-shot bounce: BounceSeq's events, its stop rules, HOLD in blocks, the names, and a bounce through
// RenderFeeder's tap mode end to end.
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "bounce.h"
#include "render.h"
#include "render_link.h"
#include "sample_set.h"

using namespace mt;
using link::RenderBlocks;

void setUp() {}
void tearDown() {}

static Project* mkProj() {  // static storage: a Project is large
  static Project p;
  p.reset();
  return &p;
}

// The events of block b in frame f (nullptr when b is not in it), their count in n.
static const link::RenderEv* blockEv(const RenderBlocks& f, uint32_t b, int& n) {
  const link::RenderEv* e = f.ev;
  for (int i = 0; i < f.n; ++i) {
    n = f.evN[i] & ~RenderBlocks::kCont;
    if (f.first + i == b) return e;
    e += n;
  }
  n = 0;
  return nullptr;
}

static void checkEv(const link::RenderEv& e, uint8_t a, uint8_t b, uint8_t c, uint8_t len) {
  TEST_ASSERT_EQUAL(0, e.off);
  TEST_ASSERT_EQUAL(kPreviewTrack, e.track);
  TEST_ASSERT_EQUAL(len, e.len);
  TEST_ASSERT_EQUAL_HEX8(a, e.b[0]);
  TEST_ASSERT_EQUAL(b, e.b[1]);
  if (len > 2) TEST_ASSERT_EQUAL(c, e.b[2]);
}

// Block 0: insert, program, note-on; note-off on holdBlocks (inside a frame); the rest empty.
void test_events_block0_and_note_off() {
  BounceSpec s;
  s.instr = 5;
  s.note = 64;
  s.vel = 100;
  s.insTrack = 3;
  s.holdBlocks = 11;
  BounceSeq q(s);
  RenderBlocks f;
  TEST_ASSERT_TRUE(q.next(f, 0));
  TEST_ASSERT_EQUAL_UINT32(0, f.first);
  TEST_ASSERT_EQUAL(8, f.n);
  int n = 0;
  const link::RenderEv* e = blockEv(f, 0, n);
  TEST_ASSERT_EQUAL(3, n);
  checkEv(e[0], 0xF7, 3, 0, 2);
  checkEv(e[1], 0xC0, 5, 0, 2);
  checkEv(e[2], 0x90, 64, 100, 3);
  for (uint32_t b = 1; b < 8; ++b) {
    blockEv(f, b, n);
    TEST_ASSERT_EQUAL(0, n);
  }
  TEST_ASSERT_TRUE(q.next(f, 0));
  TEST_ASSERT_EQUAL_UINT32(8, f.first);
  e = blockEv(f, 11, n);
  TEST_ASSERT_EQUAL(1, n);
  checkEv(e[0], 0x80, 64, 0, 3);
  blockEv(f, 10, n);
  TEST_ASSERT_EQUAL(0, n);
  TEST_ASSERT_EQUAL_UINT32(16, q.blocksSent());
}

// HOLD on a frame's border: the note-off is the first block of the next frame.
void test_note_off_on_frame_border() {
  BounceSpec s;
  s.holdBlocks = 8;
  BounceSeq q(s);
  RenderBlocks f;
  TEST_ASSERT_TRUE(q.next(f, 0));
  int n = 0;
  for (uint32_t b = 1; b < 8; ++b) {
    blockEv(f, b, n);
    TEST_ASSERT_EQUAL(0, n);
  }
  TEST_ASSERT_TRUE(q.next(f, 99));  // quiet before the note-off: ignored
  const link::RenderEv* e = blockEv(f, 8, n);
  TEST_ASSERT_EQUAL(1, n);
  checkEv(e[0], 0x80, 60, 0, 3);
}

// Quiet stops the render only once the note-off has been rendered.
void test_quiet_stops_after_note_off() {
  BounceSpec s;
  s.holdBlocks = 20;
  BounceSeq q(s);
  RenderBlocks f;
  TEST_ASSERT_TRUE(q.next(f, 0));    // 0..7
  TEST_ASSERT_TRUE(q.next(f, 100));  // 8..15: quiet ignored
  TEST_ASSERT_TRUE(q.next(f, 100));  // 16..23 (note-off on 20)
  TEST_ASSERT_TRUE(q.next(f, 17));   // below quietStop: on
  TEST_ASSERT_FALSE(q.next(f, 18));
  TEST_ASSERT_EQUAL(0, f.n);
  TEST_ASSERT_EQUAL_UINT32(32, q.blocksSent());
  TEST_ASSERT_FALSE(q.hitMax());
}

// maxBlocks ends it (the last frame cut short) and says so.
void test_stops_at_max() {
  BounceSpec s;
  s.holdBlocks = 3;
  s.maxBlocks = 13;
  BounceSeq q(s);
  RenderBlocks f;
  TEST_ASSERT_TRUE(q.next(f, 0));
  TEST_ASSERT_TRUE(q.next(f, 0));
  TEST_ASSERT_EQUAL(5, f.n);
  TEST_ASSERT_FALSE(q.next(f, 0));
  TEST_ASSERT_TRUE(q.hitMax());
  TEST_ASSERT_EQUAL_UINT32(13, q.blocksSent());
}

void test_defaults_match_times() {
  BounceSpec s;
  TEST_ASSERT_EQUAL_UINT32(172, s.holdBlocks);
  TEST_ASSERT_EQUAL_UINT32(3446, s.maxBlocks);  // 10 s
  TEST_ASSERT_EQUAL_UINT32(18, s.quietStop);    // ~50 ms
  TEST_ASSERT_EQUAL_UINT32(172, bounceHoldBlocks(0.5f));
  TEST_ASSERT_EQUAL_UINT32(bounceHoldBlocks(0.05f), bounceHoldBlocks(0.f));
  TEST_ASSERT_EQUAL_UINT32(17, bounceHoldBlocks(0.05f));
  TEST_ASSERT_EQUAL_UINT32(bounceHoldBlocks(5.f), bounceHoldBlocks(60.f));
  TEST_ASSERT_EQUAL_UINT32(1723, bounceHoldBlocks(5.f));
}

void test_names() {
  Project& p = *mkProj();
  char n[kSampleNameMax + 1];
  nextBounceName(p, "BD", n);
  TEST_ASSERT_EQUAL_STRING("BD_1", n);
  TEST_ASSERT_EQUAL(0, projSampleSet(p, "bd_1", 1, 100));  // taken, any case
  nextBounceName(p, "BD", n);
  TEST_ASSERT_EQUAL_STRING("BD_2", n);
  nextBounceName(p, "", n);
  TEST_ASSERT_EQUAL_STRING("SHOT_1", n);
  nextBounceName(p, nullptr, n);
  TEST_ASSERT_EQUAL_STRING("SHOT_1", n);
  nextBounceName(p, "E.PNO 2", n);  // not a sample name's characters
  TEST_ASSERT_EQUAL_STRING("E_PNO_2_1", n);
  nextBounceName(p, "ABCDEFGHIJKLMNOPQRS", n);  // longer than a name: cut
  TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMN_1", n);
  TEST_ASSERT_TRUE(strlen(n) <= static_cast<size_t>(kSampleNameMax));
  for (int i = 1; i <= 9; ++i) {
    char t[kSampleNameMax + 1];
    snprintf(t, sizeof t, "ABCDEFGHIJKLMN_%d", i);
    TEST_ASSERT_TRUE(projSampleSet(p, t, static_cast<uint32_t>(i + 10), 100) >= 0);
  }
  nextBounceName(p, "ABCDEFGHIJKLMNOPQRS", n);
  TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLM_10", n);
}

// ---- End to end: BounceSeq -> RenderFeeder (tap mode) -> Synth ----

struct Shot {
  uint32_t blocks = 0;
  int16_t peak = 0;
  bool max = false;
};

static bool noOut(const int16_t*, const int16_t*, int, void*) { return true; }

static Shot bounce(Project& p, Synth& synth, const BounceSpec& s) {
  static float l[Synth::kBlock], r[Synth::kBlock];
  RenderTaps taps;
  taps.mask = 1u << RenderTaps::kPrev;
  taps.l[RenderTaps::kPrev] = l;
  taps.r[RenderTaps::kPrev] = r;
  synth.reset();
  synth.setTaps(&taps);
  RenderFeeder fd(synth);
  fd.reset();
  fd.setTap(&taps, RenderTaps::kPrev);
  BounceSeq q(s);
  RenderBlocks f;
  uint32_t quiet = 0;
  while (q.next(f, quiet)) {
    TEST_ASSERT_EQUAL(static_cast<int>(link::RenderResult::Ok), static_cast<int>(fd.apply(f, noOut, nullptr)));
    quiet = fd.blocks() - fd.loudBlocks();  // as RenderRep::quiet
  }
  synth.setTaps(nullptr);
  (void)p;
  return Shot{fd.blocks(), fd.peak(), q.hitMax()};
}

static Synth* fxSynth(Project& p) {
  static int16_t dline[kSynthRate / 4];
  static float rbuf[Reverb::kBufLen];
  static Synth* s = nullptr;
  delete s;
  s = new Synth(p);
  s->setDelayBuffer(dline, kSynthRate / 4);
  s->setReverbBuffer(rbuf, Reverb::kBufLen);
  return s;
}

// A kick ends on its own silence well before 10 s; the sends (delay, reverb) neither sound in it nor
// keep it going.
void test_bounce_kick_ends_on_silence() {
  Project& p = *mkProj();
  Instrument& m = p.instruments[2];
  instrSetType(m, InstrType::Drum);
  drumSetMachine(m, kDrum2First);  // BDA
  p.dlyLevel = 127;
  p.rvbLevel = 127;
  Synth& synth = *fxSynth(p);
  BounceSpec s;
  s.instr = 2;
  s.note = 36;
  s.holdBlocks = bounceHoldBlocks(0.1f);
  const Shot dry = bounce(p, synth, s);
  TEST_ASSERT_FALSE(dry.max);
  TEST_ASSERT_TRUE(dry.blocks < s.maxBlocks);
  TEST_ASSERT_TRUE(dry.blocks > s.holdBlocks);
  TEST_ASSERT_TRUE(dry.peak > kSilence);
  m.send = 127;
  m.rsend = 127;
  const Shot wet = bounce(p, synth, s);
  TEST_ASSERT_EQUAL_UINT32(dry.blocks, wet.blocks);
  TEST_ASSERT_EQUAL(dry.peak, wet.peak);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_events_block0_and_note_off);
  RUN_TEST(test_note_off_on_frame_border);
  RUN_TEST(test_quiet_stops_after_note_off);
  RUN_TEST(test_stops_at_max);
  RUN_TEST(test_defaults_match_times);
  RUN_TEST(test_names);
  RUN_TEST(test_bounce_kick_ends_on_silence);
  return UNITY_END();
}
