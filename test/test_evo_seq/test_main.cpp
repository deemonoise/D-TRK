// The sequencer side of evolve (evolve.h): applying the state, mutating at pass ends, reset, rewind.
#include <string.h>
#include <unity.h>
#include <vector>
#include "sequencer.h"

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

void test_off_plays_the_pattern() {
  melody();
  const std::vector<On> v = play(4);
  TEST_ASSERT_EQUAL(16, pass(v, 0).size());
  for (int k = 1; k < 4; ++k) TEST_ASSERT_TRUE(same(pass(v, 0), pass(v, k)));
}

// A state set by hand plays.
void test_state_applies() {
  melody();
  p->evo[0].amount = 100;
  seq = new Sequencer(*p);
  sink = new Sink();
  seq->start(0, *sink);
  seq->evoDebugState(0).step[0].deg = 1;  // test hook: C -> D on step 1
  run(0, kPassUs - 1);
  TEST_ASSERT_EQUAL(62, sink->on[0].note);
  TEST_ASSERT_EQUAL(61, sink->on[1].note);
}

void test_stop_resets_state() {
  melody();
  p->evo[0].amount = 100;
  seq = new Sequencer(*p);
  sink = new Sink();
  seq->start(0, *sink);
  seq->evoDebugState(0).step[0].deg = 1;
  seq->stop(0, *sink);
  TEST_ASSERT_TRUE(evoIdle(seq->heardEvo(0)));
  seq->start(0, *sink);
  run(0, kPassUs - 1);
  TEST_ASSERT_EQUAL(60, sink->on[0].note);
}

void test_first_pass_is_the_pattern() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  const std::vector<On> ref = [] {
    EvoCfg keep = p->evo[0];
    p->evo[0].mask = 0;
    std::vector<On> v = play(1);
    p->evo[0] = keep;
    return v;
  }();
  const std::vector<On> v = play(1);
  TEST_ASSERT_TRUE(same(ref, pass(v, 0)));
}

void test_mutates_pattern_untouched() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 0, kEvoNote | kEvoRhythm | kEvoRtrgVol};
  static Pattern before;
  before = p->patterns[0];
  const std::vector<On> v = play(8);
  bool changed = false;
  for (int k = 1; k < 8; ++k) changed |= !same(pass(v, 0), pass(v, k));
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL_MEMORY(&before, &p->patterns[0], sizeof(Pattern));
}

void test_every_counts_pattern_passes() {
  melody();
  p->evo[0] = EvoCfg{100, 3, 0, kEvoNote};
  const std::vector<On> v = play(12);
  TEST_ASSERT_TRUE(same(pass(v, 0), pass(v, 1)));
  TEST_ASSERT_TRUE(same(pass(v, 0), pass(v, 2)));
  bool changed = false;
  for (int k = 3; k < 12; ++k) changed |= !same(pass(v, 0), pass(v, k));
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_TRUE(same(pass(v, 3), pass(v, 4)));  // one mutation per 3 passes
  TEST_ASSERT_TRUE(same(pass(v, 3), pass(v, 5)));
}

void test_short_track_same_state_each_loop() {
  melody();
  p->patterns[0].trackLen[0] = 4;  // 4 loops per pass
  p->evo[0] = EvoCfg{100, 1, 0, kEvoNote};
  const std::vector<On> v = play(6);
  for (int k = 0; k < 6; ++k) {
    const std::vector<On> q = pass(v, k);
    TEST_ASSERT_EQUAL(16, q.size());
    for (int i = 4; i < 16; ++i) TEST_ASSERT_EQUAL(q[i % 4].note, q[i].note);
  }
}

void test_deterministic_from_project_seed_only() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 10, kEvoMaskAll};
  const std::vector<On> a = play(8, 1);
  const std::vector<On> b = play(8, 99);  // the sequencer's rng (PRB etc.) does not matter
  TEST_ASSERT_TRUE(same(a, b));
  p->seed = 4321u;
  const std::vector<On> c = play(8, 1);
  TEST_ASSERT_FALSE(same(a, c));
}

// The run seed is the follow one: a new salt (live Play) drifts another way, the same salt the same.
void test_follow_salt_drifts_another_way() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 10, kEvoMaskAll};
  const std::vector<On> a = play(8, 1, 0);
  const std::vector<On> b = play(8, 1, 77);
  const std::vector<On> c = play(8, 1, 77);
  TEST_ASSERT_FALSE(same(a, b));
  TEST_ASSERT_TRUE(same(b, c));
}

void test_restart_replays_the_same_drift() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  const std::vector<On> a = play(4);
  seq->stop(4 * kPassUs, *sink);
  sink->on.clear();
  seq->start(0, *sink);
  run(0, 4 * kPassUs - 1);
  TEST_ASSERT_TRUE(same(a, sink->on));
}

// The pass end of pass 1 is planned (its mutation done) but step 15 not heard yet: a replan undoes
// the mutation with the steps, and the drift goes on exactly as without it.
void test_replan_over_a_mutation_is_exact() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  const std::vector<On> ref = play(6);
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new Sink();
  seq->start(0, *sink);
  const uint64_t t = 2 * kPassUs - 130000;  // before step 15 of pass 1 (1 875 000 us)
  run(0, t);
  sink->now = t;
  seq->releaseTies(t, *sink);  // replans: rewinds steps 15 and 0 of the next pass
  run(t, 6 * kPassUs - 1);
  TEST_ASSERT_TRUE(same(ref, sink->on));
}

void test_pause_resume_keeps_the_drift() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  const std::vector<On> ref = play(4);
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new Sink();
  seq->start(0, *sink);
  run(0, 2 * kPassUs - 130000);
  const size_t n = sink->on.size();
  seq->pause(2 * kPassUs - 130000, *sink);
  seq->resume(10 * kPassUs, *sink);  // later: compare the notes only
  run(10 * kPassUs, 12 * kPassUs);
  // The notes after the resume continue the reference (pitch / velocity).
  const std::vector<On>& v = sink->on;
  TEST_ASSERT_TRUE(v.size() > n);
  for (size_t i = n; i < v.size() && i < ref.size(); ++i) {
    TEST_ASSERT_EQUAL(ref[i].note, v[i].note);
    TEST_ASSERT_EQUAL(ref[i].vel, v[i].vel);
  }
}

void test_evo_clear_zeroes_heard_state() {
  melody();
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  play(3);
  const uint64_t t = 3 * kPassUs - 1;
  TEST_ASSERT_FALSE(evoIdle(seq->heardEvo(0)));
  const uint32_t n = seq->heardEvo(0).n;
  TEST_ASSERT_TRUE(n >= 1);
  sink->now = t;
  seq->evoClear(t, *sink, 0);
  TEST_ASSERT_TRUE(evoIdle(seq->heardEvo(0)));
  TEST_ASSERT_EQUAL(n, seq->heardEvo(0).n);
}

void test_drum_track_note_mask_is_silent() {
  instrSetType(p->instruments[0], InstrType::Kit);
  p->tracks[0].instr = 0;
  for (int i = 0; i < 16; ++i) {
    p->patterns[0].steps[0][i].note = 100;  // velocity
    p->patterns[0].steps[0][i].vel = 0x01;  // lane 1
  }
  p->evo[0] = EvoCfg{100, 1, 0, kEvoNote};
  const std::vector<On> v = play(6);
  TEST_ASSERT_EQUAL(16, pass(v, 0).size());
  for (int k = 1; k < 6; ++k) TEST_ASSERT_TRUE(same(pass(v, 0), pass(v, k)));
}

void test_drum_track_rhythm_drops() {
  instrSetType(p->instruments[0], InstrType::Kit);
  p->tracks[0].instr = 0;
  for (int i = 0; i < 16; i += 2) {
    p->patterns[0].steps[0][i].note = 100;
    p->patterns[0].steps[0][i].vel = 0x01;
  }
  p->evo[0] = EvoCfg{100, 1, 0, kEvoRhythm};
  const std::vector<On> v = play(8);
  bool changed = false;
  for (int k = 1; k < 8; ++k) changed |= !same(pass(v, 0), pass(v, k));
  TEST_ASSERT_TRUE(changed);
}

void test_gen_track_ignores_note_mask() {
  melody();
  p->gen[0].mode = static_cast<uint8_t>(GenMode::Pitch);
  const std::vector<On> a = play(6);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoNote};
  const std::vector<On> b = play(6);
  TEST_ASSERT_TRUE(same(a, b));
}

// GEN GATE makes the rhythm: RHYTHM does nothing there.
void test_gen_gate_ignores_rhythm_mask() {
  melody();
  p->gen[0].mode = static_cast<uint8_t>(GenMode::Gate);
  const std::vector<On> a = play(6);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoNote | kEvoRhythm};
  const std::vector<On> b = play(6);
  TEST_ASSERT_TRUE(same(a, b));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_off_plays_the_pattern);
  RUN_TEST(test_state_applies);
  RUN_TEST(test_stop_resets_state);
  RUN_TEST(test_first_pass_is_the_pattern);
  RUN_TEST(test_mutates_pattern_untouched);
  RUN_TEST(test_every_counts_pattern_passes);
  RUN_TEST(test_short_track_same_state_each_loop);
  RUN_TEST(test_deterministic_from_project_seed_only);
  RUN_TEST(test_follow_salt_drifts_another_way);
  RUN_TEST(test_restart_replays_the_same_drift);
  RUN_TEST(test_replan_over_a_mutation_is_exact);
  RUN_TEST(test_pause_resume_keeps_the_drift);
  RUN_TEST(test_evo_clear_zeroes_heard_state);
  RUN_TEST(test_drum_track_note_mask_is_silent);
  RUN_TEST(test_drum_track_rhythm_drops);
  RUN_TEST(test_gen_track_ignores_note_mask);
  RUN_TEST(test_gen_gate_ignores_rhythm_mask);
  return UNITY_END();
}
