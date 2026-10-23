// The sequencer side of VAR (var_gen.h): generations at pass ends, playing, order VAR -> EVO -> GEN, rewind,
// reset, Capture.
#include <string.h>
#include <unity.h>
#include <set>
#include <vector>
#include "sequencer.h"
#include "var_gen.h"

using namespace mt;

struct On {
  uint64_t t;
  uint8_t ch, note, vel;
};

struct Sink : MidiSink {
  std::vector<On> on;
  uint64_t now = 0;
  void send(const uint8_t* b, uint8_t len) override {
    if (len == 3 && (b[0] & 0xF0) == 0x90 && b[2]) on.push_back({now, static_cast<uint8_t>(b[0] & 15), b[1], b[2]});
  }
};

static Project* p;
static Sequencer* seq;
static Sink* sink;
static constexpr uint64_t kPassUs = 2000000;  // 16 steps of 1/16 at 120 BPM
static constexpr uint64_t kStepUs = kPassUs / 16;

static void run(uint64_t from, uint64_t to) {
  uint64_t t = from;
  while (t <= to) {
    sink->now = t;
    const uint64_t next = seq->process(t, *sink);
    if (next == kNever) break;
    t = next > t ? next : t + 1;
  }
}

// Note-ons of pass k, times relative to the pass start.
static std::vector<On> pass(const std::vector<On>& v, int k) {
  std::vector<On> out;
  for (On o : v)
    if (o.t >= k * kPassUs && o.t < (k + 1) * kPassUs) {
      o.t -= k * kPassUs;
      out.push_back(o);
    }
  return out;
}

static bool same(const std::vector<On>& x, const std::vector<On>& y) {
  if (x.size() != y.size()) return false;
  for (size_t i = 0; i < x.size(); ++i)
    if (x[i].t != y[i].t || x[i].ch != y[i].ch || x[i].note != y[i].note || x[i].vel != y[i].vel) return false;
  return true;
}

// Steps of a pass with a note-on.
static std::set<int> steps(const std::vector<On>& q) {
  std::set<int> s;
  for (const On& o : q) s.insert(static_cast<int>((o.t + kStepUs / 2) / kStepUs));
  return s;
}

// Track 0 (MIDI): a C major line on every step.
static void melody() {
  p->scaleType = static_cast<uint8_t>(ScaleType::Major);
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[0][i].note = static_cast<uint8_t>(60 + (i % 8));
}

// A fresh sequencer on p, played from 0 for `passes` passes; its note-ons.
static std::vector<On> play(int passes, uint32_t rngSeed = 1, uint32_t salt = 0) {
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(rngSeed);
  seq->setFollowSalt(salt);
  sink = new Sink();
  seq->start(0, *sink);
  run(0, passes * kPassUs - 1);
  return sink->on;
}

static VarCfg ca1d(uint8_t rule = 90) {
  VarCfg c;
  c.mode = static_cast<uint8_t>(VarMode::Ca1d);
  c.every = 1;
  c.rule = rule;
  c.reset = 0;
  c.noise = 0;
  return c;
}

// Reference CA1D run of 16 cells from bits g0: generation k (back to g0 when dead or unchanged).
static std::vector<uint16_t> ca1dRef(uint16_t g0, uint8_t rule, int gens) {
  std::vector<uint16_t> out{g0};
  uint16_t cur = g0;
  for (int k = 1; k < gens; ++k) {
    uint16_t nx = 0;
    for (int i = 0; i < 16; ++i) {
      const int l = (cur >> ((i + 15) % 16)) & 1, c = (cur >> i) & 1, r = (cur >> ((i + 1) % 16)) & 1;
      if ((rule >> (l << 2 | c << 1 | r)) & 1) nx = static_cast<uint16_t>(nx | 1u << i);
    }
    cur = !nx || nx == cur ? g0 : nx;
    out.push_back(cur);
  }
  return out;
}

static std::set<int> bits(uint16_t v) {
  std::set<int> s;
  for (int i = 0; i < 16; ++i)
    if ((v >> i) & 1) s.insert(i);
  return s;
}

void setUp() {
  p = new Project();
  for (TrackCfg& c : p->tracks) c.out = TrackOut::Midi;
  p->seed = 1234u;
  seq = nullptr;
  sink = nullptr;
}
void tearDown() {
  delete sink;
  delete seq;
  delete p;
}

// VAR off (any other field) plays the pattern, with EVO exactly as without VAR.
void test_off_is_the_pattern() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 10, kEvoMaskAll};
  const std::vector<On> ref = play(6);
  p->var[0] = ca1d(30);
  p->var[0].mode = static_cast<uint8_t>(VarMode::Off);
  p->var[0].noise = 100;
  const std::vector<On> v = play(6);
  TEST_ASSERT_TRUE(same(ref, v));
  TEST_ASSERT_EQUAL(0, seq->heardVar(0).sinceReset);
}

// Rule 90 from one note, every pass: pass k plays the alive cells of generation k, each the note.
void test_ca1d_plays_alive_cells() {
  p->patterns[0].steps[0][3].note = 64;
  p->patterns[0].steps[0][3].vel = 90;
  p->var[0] = ca1d(90);
  const std::vector<On> v = play(12);
  const std::vector<uint16_t> ref = ca1dRef(1u << 3, 90, 12);
  for (int k = 0; k < 12; ++k) {
    const std::vector<On> q = pass(v, k);
    TEST_ASSERT_TRUE_MESSAGE(bits(ref[k]) == steps(q), "alive cells");
    for (const On& o : q) {
      TEST_ASSERT_EQUAL(64, o.note);
      TEST_ASSERT_EQUAL(90, o.vel);
    }
  }
  TEST_ASSERT_TRUE(steps(pass(v, 1)) == (std::set<int>{2, 4}));
}

// Every counts pattern passes.
void test_every_counts_passes() {
  p->patterns[0].steps[0][3].note = 64;
  p->var[0] = ca1d(90);
  p->var[0].every = 3;
  const std::vector<On> v = play(7);
  TEST_ASSERT_TRUE(same(pass(v, 0), pass(v, 2)));
  TEST_ASSERT_TRUE(steps(pass(v, 3)) == (std::set<int>{2, 4}));
  TEST_ASSERT_TRUE(same(pass(v, 3), pass(v, 5)));
  TEST_ASSERT_TRUE(steps(pass(v, 6)) == (std::set<int>{1, 5}));
}

// KIT CA2D: a Life blinker over lanes 0..2 at step 1 turns into lane 1 at steps 0..2.
void test_kit_ca2d_masks() {
  instrSetType(p->instruments[0], InstrType::Kit);
  p->tracks[0].instr = 0;
  for (int l = 0; l < kKitLanes; ++l) p->instruments[0].kit[l].note = static_cast<uint8_t>(36 + l);
  p->patterns[0].steps[0][1].note = 100;
  p->patterns[0].steps[0][1].vel = 0x07;
  p->var[0] = ca1d();
  p->var[0].mode = static_cast<uint8_t>(VarMode::Ca2d);
  p->var[0].rule2 = static_cast<uint8_t>(VarRule2::Life);
  const std::vector<On> v = play(3);
  for (int k = 0; k < 3; ++k) {
    std::set<std::pair<int, int>> got, want;
    for (const On& o : pass(v, k)) got.insert({static_cast<int>((o.t + kStepUs / 2) / kStepUs), o.note});
    if (k % 2 == 0)
      for (int l = 0; l < 3; ++l) want.insert({1, 36 + l});
    else
      for (int s = 0; s < 3; ++s) want.insert({s, 37});
    TEST_ASSERT_TRUE_MESSAGE(got == want, "blinker");
  }
}

// Track 0: a line with branching transitions (a Markov chain over it has choices).
static void branching() {
  static const uint8_t kLine[16] = {60, 62, 60, 64, 60, 67, 62, 64, 60, 62, 67, 60, 64, 62, 60, 67};
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[0][i].note = kLine[i];
}

void test_markov_amount_0_is_the_pattern() {
  branching();
  const std::vector<On> ref = play(4);
  p->var[0].mode = static_cast<uint8_t>(VarMode::Markov);
  p->var[0].every = 1;
  p->var[0].amount = 0;
  p->var[0].rhythm = 1;
  TEST_ASSERT_TRUE(same(ref, play(4)));
  p->var[0].amount = 100;
  const std::vector<On> v = play(4);
  bool changed = false;
  for (int k = 1; k < 4; ++k) changed |= !same(pass(v, 0), pass(v, k));
  TEST_ASSERT_TRUE(changed);
}

// The pass end of pass 1 is planned (its generation made) but step 15 not heard yet: a replan undoes the
// generation with the steps and the run goes on exactly as without it.
void test_rewind_returns_the_earlier_generation() {
  melody();
  p->var[0] = ca1d(30);
  p->var[0].noise = 50;
  const std::vector<On> ref = play(6);
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new Sink();
  seq->start(0, *sink);
  const uint64_t t = 2 * kPassUs - 130000;  // before step 15 of pass 1
  run(0, t);
  sink->now = t;
  seq->releaseTies(t, *sink);
  run(t, 6 * kPassUs - 1);
  TEST_ASSERT_TRUE(same(ref, sink->on));
}

// Markov too: a replan over its generations is exact.
void test_rewind_markov() {
  branching();  // melody() has one successor per note: Markov would replay the pattern
  p->var[0].mode = static_cast<uint8_t>(VarMode::Markov);
  p->var[0].every = 1;
  p->var[0].amount = 80;
  const std::vector<On> ref = play(6);
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new Sink();
  seq->start(0, *sink);
  for (int k = 1; k < 5; ++k) {
    const uint64_t t = (k + 1) * kPassUs - 130000;
    run(sink->now, t);
    sink->now = t;
    seq->varEdited(t, *sink);
  }
  run(sink->now, 6 * kPassUs - 1);
  TEST_ASSERT_TRUE(same(ref, sink->on));
  bool changed = false;
  for (int k = 1; k < 6; ++k) changed |= !same(pass(ref, 0), pass(ref, k));
  TEST_ASSERT_TRUE(changed);
}

void test_stop_resets() {
  p->patterns[0].steps[0][3].note = 64;
  p->var[0] = ca1d(90);
  const std::vector<On> a = play(4);
  TEST_ASSERT_TRUE(seq->heardVar(0).sinceReset > 0);
  seq->stop(4 * kPassUs, *sink);
  TEST_ASSERT_EQUAL(0, seq->heardVar(0).sinceReset);
  TEST_ASSERT_EQUAL_UINT32(0, seq->heardVar(0).n);
  TEST_ASSERT_EQUAL(0, seq->varPasses(0));
  sink->on.clear();
  seq->start(0, *sink);
  run(0, 4 * kPassUs - 1);
  TEST_ASSERT_TRUE(same(a, sink->on));
}

// Render (salt 0) twice: the same events; another salt (live Play) another run.
void test_render_twice_identical() {
  melody();
  p->var[0] = ca1d(110);
  p->var[0].noise = 100;
  const std::vector<On> a = play(8, 1, 0);
  const std::vector<On> b = play(8, 99, 0);
  TEST_ASSERT_TRUE(same(a, b));
  const std::vector<On> c = play(8, 1, 77);
  TEST_ASSERT_FALSE(same(a, c));
  branching();
  p->var[0].mode = static_cast<uint8_t>(VarMode::Markov);
  p->var[0].amount = 70;
  const std::vector<On> m = play(8, 1, 0);
  TEST_ASSERT_TRUE(same(m, play(8, 5, 0)));
  TEST_ASSERT_FALSE(same(m, play(8, 1, 77)));
}

// EVO works on VAR's output: NOTE alone moves pitches only at VAR's steps.
void test_var_and_evo_stack() {
  p->patterns[0].steps[0][3].note = 64;
  p->scaleType = static_cast<uint8_t>(ScaleType::Major);
  p->var[0] = ca1d(90);
  const std::vector<On> varOnly = play(8);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoNote};
  const std::vector<On> v = play(8);
  bool moved = false;
  for (int k = 0; k < 8; ++k) {
    TEST_ASSERT_TRUE(steps(pass(v, k)) == steps(pass(varOnly, k)));
    for (const On& o : pass(v, k)) moved |= o.note != 64;
  }
  TEST_ASSERT_TRUE(moved);
  // RHYTHM drops / ghosts on top of VAR's steps.
  p->evo[0] = EvoCfg{100, 1, 0, kEvoRhythm};
  const std::vector<On> r = play(8);
  bool differs = false;
  for (int k = 1; k < 8; ++k) differs |= steps(pass(r, k)) != steps(pass(varOnly, k));
  TEST_ASSERT_TRUE(differs);
}

// GEN PITCH over a VAR track: VAR's rhythm, GEN's notes.
void test_gen_pitch_takes_var_rhythm() {
  p->scaleType = static_cast<uint8_t>(ScaleType::Major);
  p->patterns[0].steps[0][3].note = 64;
  p->var[0] = ca1d(90);
  p->gen[0].mode = static_cast<uint8_t>(GenMode::Pitch);
  p->gen[0].flip = 30;
  const std::vector<On> v = play(6);
  const std::vector<uint16_t> ref = ca1dRef(1u << 3, 90, 6);
  bool other = false;
  for (int k = 0; k < 6; ++k) {
    TEST_ASSERT_TRUE(bits(ref[k]) == steps(pass(v, k)));
    for (const On& o : pass(v, k)) other |= o.note != 64;
  }
  TEST_ASSERT_TRUE(other);
}

// varClear: the heard state back to generation 0, n goes on.
void test_var_clear() {
  p->patterns[0].steps[0][3].note = 64;
  p->var[0] = ca1d(90);
  play(3);
  const uint64_t t = 3 * kPassUs - 1;
  TEST_ASSERT_TRUE(seq->heardVar(0).sinceReset > 0);
  const uint32_t n = seq->heardVar(0).n;
  sink->now = t;
  seq->varClear(t, *sink, 0);
  TEST_ASSERT_EQUAL(0, seq->heardVar(0).sinceReset);
  TEST_ASSERT_EQUAL_UINT32(n, seq->heardVar(0).n);
}

// Capture: the heard state (VAR, then EVO) written into the pattern plays as heard without VAR / EVO.
void test_capture_writes_var_then_evo() {
  melody();
  p->var[0] = ca1d(90);
  p->patterns[0].steps[0][5].note = kNoteEmpty;
  p->patterns[0].steps[0][9].note = kNoteEmpty;
  p->patterns[0].steps[0][10].note = kNoteEmpty;
  p->evo[0] = EvoCfg{100, 1, 0, kEvoNote | kEvoRtrgVol};
  const std::vector<On> ref = play(4);
  play(3);  // the heard state at the end of pass 2: the one pass 3 plays
  static VarTrack vt;
  static EvoTrack et;
  vt = seq->heardVar(0);
  et = seq->heardEvo(0);
  TEST_ASSERT_TRUE(vt.sinceReset > 0);
  static Step scratch[kMaxSteps];
  varCapture(p->patterns[0], 0, vt, varApplyOf(*p, 0), et, evoApplyOf(*p, 0), scratch);
  p->var[0].mode = 0;
  p->evo[0].mask = 0;
  const std::vector<On> v = play(1);
  TEST_ASSERT_TRUE(same(pass(ref, 3), v));
}

// Another pattern next restarts VAR at its generation 0 (the pass count too); the same pattern again goes on.
static std::vector<On> playQueued(int passes, uint8_t queue, bool replanAtEnd, int at = 1) {
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new Sink();
  seq->start(0, *sink);
  const uint64_t q = at * kPassUs + kPassUs / 2;  // queued in pass `at`
  run(0, q);
  seq->queuePattern(queue);
  uint64_t t = q + 1;
  if (replanAtEnd) {  // the pass end (the switch, the restart) planned, step 15 not heard: undone, again
    t = (at + 1) * kPassUs - 130000;
    run(q + 1, t);
    sink->now = t;
    seq->releaseTies(t, *sink);
  }
  run(t, passes * kPassUs - 1);
  return sink->on;
}

void test_pattern_change_restarts() {
  p->patterns[0].steps[0][3].note = 64;
  p->patterns[1].steps[0][5].note = 67;
  p->var[0] = ca1d(90);
  const std::vector<On> v = playQueued(5, 1, false);
  TEST_ASSERT_TRUE(steps(pass(v, 1)) == (std::set<int>{2, 4}));  // A, generation 1
  TEST_ASSERT_TRUE(steps(pass(v, 2)) == (std::set<int>{5}));     // B's own steps
  TEST_ASSERT_EQUAL(67, pass(v, 2)[0].note);
  TEST_ASSERT_TRUE(steps(pass(v, 3)) == (std::set<int>{4, 6}));  // B, generation 1
  TEST_ASSERT_TRUE(same(v, playQueued(5, 1, true)));             // rewind-safe
  // every 2, queued in pass 2 (A's count at 1 there): B's generation 1 after two passes of B.
  p->var[0].every = 2;
  const std::vector<On> e = playQueued(6, 1, false, 2);
  TEST_ASSERT_TRUE(steps(pass(e, 2)) == (std::set<int>{2, 4}));  // A, generation 1
  TEST_ASSERT_TRUE(steps(pass(e, 3)) == (std::set<int>{5}));
  TEST_ASSERT_TRUE(steps(pass(e, 4)) == (std::set<int>{5}));     // the count restarted with B
  TEST_ASSERT_TRUE(steps(pass(e, 5)) == (std::set<int>{4, 6}));
  TEST_ASSERT_TRUE(same(e, playQueued(6, 1, true, 2)));
}

void test_same_pattern_queued_goes_on() {
  p->patterns[0].steps[0][3].note = 64;
  p->var[0] = ca1d(90);
  const std::vector<On> ref = play(5);
  TEST_ASSERT_TRUE(same(ref, playQueued(5, 0, false)));
}

void test_pattern_untouched() {
  melody();
  p->var[0] = ca1d(30);
  static Pattern before;
  before = p->patterns[0];
  play(6);
  TEST_ASSERT_EQUAL_MEMORY(&before, &p->patterns[0], sizeof(Pattern));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_off_is_the_pattern);
  RUN_TEST(test_ca1d_plays_alive_cells);
  RUN_TEST(test_every_counts_passes);
  RUN_TEST(test_kit_ca2d_masks);
  RUN_TEST(test_markov_amount_0_is_the_pattern);
  RUN_TEST(test_rewind_returns_the_earlier_generation);
  RUN_TEST(test_rewind_markov);
  RUN_TEST(test_stop_resets);
  RUN_TEST(test_render_twice_identical);
  RUN_TEST(test_var_and_evo_stack);
  RUN_TEST(test_gen_pitch_takes_var_rhythm);
  RUN_TEST(test_var_clear);
  RUN_TEST(test_capture_writes_var_then_evo);
  RUN_TEST(test_pattern_change_restarts);
  RUN_TEST(test_same_pattern_queued_goes_on);
  RUN_TEST(test_pattern_untouched);
  return UNITY_END();
}
