// BounceRender: a one-shot through the preview tap with a real Synth (storage::bounce's two passes).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>
#include "bounce.h"
#include "render.h"
#include "synth.h"

using namespace mt;

static Project* p;
static Synth* s;
static BounceRender* gR;  // the last run's (its destructor takes the tap off s)

constexpr uint32_t kMaxFrames = 2500 * Synth::kBlock;
static int16_t a[kMaxFrames], b[kMaxFrames];

void setUp() {
  p = new Project();
  p->masterVol = 100;
  for (int t = 0; t < kTracks; ++t) {
    p->tracks[t].out = TrackOut::Int;
    p->tracks[t].vol = 127;
  }
  instrSetType(p->instruments[0], InstrType::Drum);  // a BD
  Instrument& c = p->instruments[1];                 // CHIP saw, Random LFO (Retrig OFF) on the pitch
  instrSetType(c, InstrType::Chip);
  c.wave = static_cast<uint8_t>(Wave::Saw);
  c.attack = 0;
  c.decay = 0;
  c.sustain = 127;
  c.release = 10;
  c.lfoWave = static_cast<uint8_t>(LfoWave::Random);
  c.lfoRate = 100;
  c.lfoDepth = 50;
  c.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  c.lfoSync = kLfoFree;
  Instrument& t = p->instruments[2];  // the same with a TEMPO Retrig OFF Saw LFO (a pause holds it)
  t = c;
  t.lfoWave = static_cast<uint8_t>(LfoWave::Saw);
  t.lfoRate = 3;
  t.lfoSync = kLfoTempo | kLfoFree;
  s = new Synth(*p);
}
void tearDown() {
  delete gR;
  gR = nullptr;
  delete s;
  delete p;
}

// Renders spec into out; blocks rendered.
static uint32_t run(const BounceSpec& spec, int16_t* out, BounceRender** keep = nullptr) {
  delete gR;
  BounceRender* r = gR = new BounceRender(*s, spec);
  uint32_t n = 0;
  while (r->next(out + n * Synth::kBlock)) ++n;
  TEST_ASSERT_EQUAL_UINT32(n, r->blocks());
  TEST_ASSERT_FALSE(r->failed());
  if (keep) *keep = r;
  return n;
}

static int16_t peakOf(const int16_t* x, uint32_t n) {
  int pk = 0;
  for (uint32_t i = 0; i < n; ++i) pk = abs(x[i]) > pk ? abs(x[i]) : pk;
  return static_cast<int16_t>(pk);
}

// A DRUM hit: loud, ends on silence after the note-off (not at maxBlocks), the kept frames hold the
// loud part only.
void test_drum_renders_and_stops() {
  BounceSpec spec;
  spec.instr = 0;
  spec.note = 36;
  BounceRender* r;
  const uint32_t n = run(spec, a, &r);
  TEST_ASSERT_FALSE(r->silent());
  TEST_ASSERT_FALSE(r->hitMax());
  TEST_ASSERT_TRUE(n > spec.holdBlocks);
  TEST_ASSERT_TRUE(n < spec.maxBlocks);
  TEST_ASSERT_TRUE(r->peak() > 1000);
  TEST_ASSERT_EQUAL_INT16(peakOf(a, n * Synth::kBlock), r->peak());
  TEST_ASSERT_TRUE(r->keepFrames() <= n * Synth::kBlock);
  TEST_ASSERT_TRUE(r->keepFrames() >= Synth::kBlock);
  const uint32_t k = r->keepFrames();
  TEST_ASSERT_TRUE(peakOf(a + k, n * Synth::kBlock - k) <= kSilence);  // the cut tail is silent
  TEST_ASSERT_TRUE(peakOf(a + k - Synth::kBlock, Synth::kBlock) > kSilence);
}

// Two passes sample-identical, also with a Random Retrig OFF LFO and other playing in between
// (reset + reseed in the constructor).
void test_two_passes_identical() {
  BounceSpec spec;
  spec.instr = 1;
  spec.holdBlocks = 60;
  const uint32_t n1 = run(spec, a);
  int16_t blk[Synth::kBlock];
  const uint8_t on[3] = {0x90, 64, 127};
  s->event(0, 0, on, 3);
  for (int i = 0; i < 77; ++i) s->render(blk);  // the free LFO and the generator move on
  const uint32_t n2 = run(spec, b);
  TEST_ASSERT_EQUAL_UINT32(n1, n2);
  TEST_ASSERT_EQUAL_MEMORY(a, b, n1 * Synth::kBlock * sizeof(int16_t));
}

// Pause then stop leaves the synth paused (0xFD alone): the bounce still starts stopped, as from a
// new Synth.
void test_after_pause_stop_as_fresh() {
  BounceSpec spec;
  spec.instr = 2;
  spec.holdBlocks = 60;
  const uint32_t n1 = run(spec, a);
  int16_t blk[Synth::kBlock];
  const uint8_t on[3] = {0x90, 64, 127};
  const uint8_t rts[3] = {0xFA, 0xFC, 0xFD};
  for (uint8_t rt : rts) {
    s->event(0, 0, &rt, 1);
    if (rt == 0xFA) s->event(0, 0, on, 3);
    for (int i = 0; i < 30; ++i) s->render(blk);
  }
  const uint32_t n2 = run(spec, b);
  TEST_ASSERT_EQUAL_UINT32(n1, n2);
  TEST_ASSERT_EQUAL_MEMORY(a, b, n1 * Synth::kBlock * sizeof(int16_t));
}

// Without reseed the Random LFO would differ: the check above means something.
void test_lfo_random_differs_without_reseed() {
  int16_t x[40][Synth::kBlock], y[40][Synth::kBlock], blk[Synth::kBlock];
  const uint8_t prog[2] = {0xC0, 1}, on[3] = {0x90, 60, 127};
  s->event(0, kPreviewTrack, prog, 2);
  s->event(0, kPreviewTrack, on, 3);
  for (auto& q : x) s->render(q);
  s->reset();
  for (int i = 0; i < 77; ++i) s->render(blk);
  s->event(0, kPreviewTrack, prog, 2);
  s->event(0, kPreviewTrack, on, 3);
  for (auto& q : y) s->render(q);
  TEST_ASSERT_TRUE(memcmp(x, y, sizeof x) != 0);
}

// Pass 2 normalized by pass 1's peak lands at -0.3 dBFS.
void test_normalize_peak() {
  BounceSpec spec;
  spec.instr = 0;
  spec.note = 36;
  BounceRender* r;
  const uint32_t n = run(spec, a, &r);
  const int16_t pk = r->peak();
  run(spec, b);
  for (uint32_t i = 0; i < n; ++i) normalizePeak(b + i * Synth::kBlock, Synth::kBlock, pk, kNormPeakHot);
  TEST_ASSERT_INT_WITHIN(2, static_cast<int>(kNormPeakHot), peakOf(b, n * Synth::kBlock));
}

// Through a track's insert the render differs from dry.
void test_insert_differs_from_dry() {
  p->tracks[3].drive = 120;
  p->tracks[3].crushBits = 100;
  BounceSpec spec;
  spec.instr = 0;
  spec.note = 36;
  const uint32_t n1 = run(spec, a);
  spec.insTrack = 3;
  const uint32_t n2 = run(spec, b);
  const uint32_t n = n1 < n2 ? n1 : n2;
  TEST_ASSERT_TRUE(memcmp(a, b, n * Synth::kBlock * sizeof(int16_t)) != 0);
}

// Volume 0: silent, ended right after the note-off block.
void test_silent() {
  p->instruments[0].vol = 0;
  BounceSpec spec;
  spec.instr = 0;
  BounceRender* r;
  const uint32_t n = run(spec, a, &r);
  TEST_ASSERT_TRUE(r->silent());
  TEST_ASSERT_EQUAL_UINT32(spec.holdBlocks + 1, n);
  TEST_ASSERT_EQUAL_UINT32(Synth::kBlock, r->keepFrames());
}

// A held drone runs to maxBlocks.
void test_hits_max() {
  BounceSpec spec;
  spec.instr = 1;
  spec.holdBlocks = 30;
  spec.maxBlocks = 20;
  BounceRender* r;
  TEST_ASSERT_EQUAL_UINT32(20, run(spec, a, &r));
  TEST_ASSERT_TRUE(r->hitMax());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_drum_renders_and_stops);
  RUN_TEST(test_two_passes_identical);
  RUN_TEST(test_after_pause_stop_as_fresh);
  RUN_TEST(test_lfo_random_differs_without_reseed);
  RUN_TEST(test_normalize_peak);
  RUN_TEST(test_insert_differs_from_dry);
  RUN_TEST(test_silent);
  RUN_TEST(test_hits_max);
  return UNITY_END();
}
