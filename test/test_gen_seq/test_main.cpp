// The sequencer side of the track generator (shift_gen.h): substitution, clocking, reset, rewind.
#include <unity.h>
#include <string.h>
#include <map>
#include <vector>
#include "scale.h"
#include "edit_ops.h"
#include "sequencer.h"

using namespace mt;

struct Rec {
  uint64_t t;
  uint8_t b[3];
  uint8_t len;
};
struct SynRec {
  uint64_t t;
  uint8_t track;
  uint8_t b[3];
};

struct FakeSink : MidiSink {
  std::vector<Rec> log;
  std::vector<SynRec> syn;
  uint64_t now = 0;
  void send(const uint8_t* b, uint8_t len) override {
    Rec r{now, {0, 0, 0}, len};
    memcpy(r.b, b, len);
    log.push_back(r);
  }
  void synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) override {
    SynRec r{t, track, {0, 0, 0}};
    memcpy(r.b, b, len > 3 ? 3 : len);
    syn.push_back(r);
  }
};

static Project* p;
static Sequencer* seq;
static FakeSink* sink;
constexpr uint64_t kStep = 125000;  // a 1/16 at 120 BPM

static void run(uint64_t from, uint64_t to) {
  uint64_t t = from;
  while (t <= to) {
    sink->now = t;
    const uint64_t next = seq->process(t, *sink);
    if (next == kNever) break;
    t = next > t ? next : t + 1;
  }
}

void setUp() {
  p = new Project();
  for (TrackCfg& c : p->tracks) c.out = TrackOut::Midi;
  p->bpm = 120;
  p->patterns[0].length = 16;
  p->scaleRoot = 0;
  p->scaleType = static_cast<uint8_t>(ScaleType::Major);
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new FakeSink();
}
void tearDown() {
  delete sink;
  delete seq;
  delete p;
}

static GenCfg& gen(uint8_t mode, uint8_t flip, uint8_t len = 8) {
  GenCfg& g = p->gen[0];
  g.mode = mode;
  g.flip = flip;
  g.len = len;
  return g;
}
static constexpr uint8_t kPitch = static_cast<uint8_t>(GenMode::Pitch);
static constexpr uint8_t kGate = static_cast<uint8_t>(GenMode::Gate);

// Note-ons of channel 1 (track 0) as step index -> notes, from their times.
static std::map<int, std::vector<uint8_t>> onsByStep(uint64_t t0 = 0) {
  std::map<int, std::vector<uint8_t>> m;
  for (const Rec& r : sink->log)
    if (r.b[0] == 0x90 && r.b[2] > 0 && r.t >= t0) m[static_cast<int>((r.t - t0 + kStep / 2) / kStep)].push_back(r.b[1]);
  return m;
}

// The expected register run: notes after each of n shifts (flip per shift: flips[i] or cfg).
static std::vector<uint8_t> expectNotes(const GenCfg& g, int n, std::vector<GenState>* states = nullptr,
                                        const std::map<int, uint8_t>& flips = {}) {
  GenState s;
  genReset(s, g, 0);
  std::vector<uint8_t> out;
  for (int i = 0; i < n; ++i) {
    auto it = flips.find(i);
    genShift(s, g, 0, it == flips.end() ? g.flip : it->second);
    out.push_back(genNote(s, g, 0, ScaleType::Major));
    if (states) states->push_back(s);
  }
  return out;
}

static void allNotes(int pat = 0) {
  for (int i = 0; i < p->patterns[pat].length; ++i) p->patterns[pat].steps[0][i].note = 60;
}

void test_pitch_replaces_only_the_pitch() {
  const GenCfg& g = gen(kPitch, 0);
  for (int i = 0; i < 16; i += 4) {
    p->patterns[0].steps[0][i].note = 60;
    p->patterns[0].steps[0][i].vel = 90;
  }
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  const auto want = expectNotes(g, 4);
  const auto got = onsByStep();
  TEST_ASSERT_EQUAL(4, got.size());
  for (int k = 0; k < 4; ++k) {
    TEST_ASSERT_EQUAL(1, got.at(k * 4).size());
    TEST_ASSERT_EQUAL(want[k], got.at(k * 4)[0]);
  }
  for (const Rec& r : sink->log)
    if (r.b[0] == 0x90 && r.b[2]) TEST_ASSERT_EQUAL(90, r.b[2]);
  TEST_ASSERT_EQUAL(4, seq->heardGen(0).n);  // empty steps do not clock PITCH
}

void test_pitch_chord_built_on_the_generated_note() {
  const GenCfg& g = gen(kPitch, 0);
  const uint8_t chd = chordValue(static_cast<uint8_t>(chordTypeByName("MAJ", 3)), 0);
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::CHD, chd};
  seq->start(0, *sink);
  run(0, 2 * kStep);
  uint8_t want[4];
  const int n = chordNotes(expectNotes(g, 1)[0], chd, 0, ScaleType::Major, want);
  const auto got = onsByStep();
  TEST_ASSERT_EQUAL(n, got.at(0).size());
  for (int i = 0; i < n; ++i) TEST_ASSERT_EQUAL(want[i], got.at(0)[i]);
}

void test_gate_plays_empty_steps_and_off_holds() {
  GenCfg& g = gen(kGate, 25);
  g.density = 50;
  p->patterns[0].steps[0][6].note = kNoteOff;
  p->patterns[0].steps[0][9].note = 72;  // a note without a gate is dropped
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  std::vector<GenState> st;
  const auto notes = expectNotes(g, 16, &st);
  const auto got = onsByStep();
  int hits = 0;
  for (int k = 0; k < 16; ++k) {
    const bool want = k != 6 && genGate(st[k], g);
    TEST_ASSERT_EQUAL_MESSAGE(want, got.count(k) == 1, "gate");
    if (want) {
      TEST_ASSERT_EQUAL(notes[k], got.at(k)[0]);
      ++hits;
    }
  }
  TEST_ASSERT_TRUE(hits > 2 && hits < 14);
  TEST_ASSERT_EQUAL(16, seq->heardGen(0).n);
}

void test_gate_step_prb_silences_a_hit() {
  GenCfg& g = gen(kGate, 0);
  g.density = 100;
  p->patterns[0].steps[0][5].fx[0] = {Fx::PRB, 0};
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  const auto got = onsByStep();
  for (int k = 0; k < 16; ++k) TEST_ASSERT_EQUAL(k == 5 ? 0 : 1, got.count(k));
}

void test_muted_track_keeps_clocking() {
  const GenCfg& g = gen(kPitch, 30);
  allNotes();
  p->tracks[0].mute = true;
  seq->start(0, *sink);
  run(0, 8 * kStep - kStep / 2);
  p->tracks[0].mute = false;
  seq->genEdited(sink->now, *sink);  // the unheard steps again, unmuted
  run(sink->now, 16 * kStep - 1);
  const auto want = expectNotes(g, 16);
  const auto got = onsByStep();
  TEST_ASSERT_TRUE(got.size() >= 7);
  for (const auto& kv : got) {
    TEST_ASSERT_TRUE(kv.first >= 8);
    TEST_ASSERT_EQUAL(want[kv.first], kv.second[0]);
  }
}

void test_skipped_steps_clock_the_register() {
  const GenCfg& g = gen(kPitch, 30);
  allNotes();
  seq->start(0, *sink);
  sink->now = 0;
  seq->process(0, *sink);
  run(6 * kStep + 1000, 16 * kStep - 1);  // a stall: steps before 6 are skipped
  const auto want = expectNotes(g, 16);
  const auto got = onsByStep();
  TEST_ASSERT_TRUE(got.size() >= 9);
  for (const auto& kv : got)
    if (kv.first >= 6) TEST_ASSERT_EQUAL(want[kv.first], kv.second[0]);
}

void test_fast_track_clocks_per_own_step() {
  const GenCfg& g = gen(kPitch, 20);
  allNotes();
  p->patterns[0].trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  std::vector<uint8_t> got;
  for (const Rec& r : sink->log)
    if (r.b[0] == 0x90 && r.b[2]) got.push_back(r.b[1]);
  TEST_ASSERT_EQUAL(32, got.size());
  TEST_ASSERT_TRUE(got == expectNotes(g, 32));
}

void test_flp_lock_overrides_flip() {
  const GenCfg& g = gen(kPitch, 0);
  allNotes();
  p->patterns[0].steps[0][3].fx[0] = {Fx::FLP, 100};
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  const auto want = expectNotes(g, 16, nullptr, {{3, 100}});
  const auto plain = expectNotes(g, 16);
  const auto got = onsByStep();
  bool differs = false;
  for (int k = 0; k < 16; ++k) {
    TEST_ASSERT_EQUAL(want[k], got.at(k)[0]);
    differs |= want[k] != plain[k];
  }
  TEST_ASSERT_TRUE(differs);
}

void test_pattern_change_goes_on_start_resets() {
  const GenCfg& g = gen(kPitch, 40);
  allNotes(0);
  p->patterns[1] = p->patterns[0];
  seq->start(0, *sink);
  run(0, kStep);
  seq->queuePattern(1);
  run(kStep, 32 * kStep - 1);
  const auto want = expectNotes(g, 32);
  auto got = onsByStep();
  for (int k = 0; k < 32; ++k) TEST_ASSERT_EQUAL(want[k], got.at(k)[0]);
  // Pause / resume goes on, stop + Play restarts the loop.
  seq->stop(sink->now, *sink);
  sink->log.clear();
  const uint64_t t0 = 40 * kStep;
  sink->now = t0;
  seq->start(t0, *sink);
  run(t0, t0 + 8 * kStep - 1);
  got = onsByStep(t0);
  for (int k = 0; k < 8; ++k) TEST_ASSERT_EQUAL(want[k], got.at(k)[0]);
}

void test_replan_gives_the_same_events() {
  gen(kPitch, 50);
  allNotes();
  p->patterns[0].steps[0][2].note = kNoteEmpty;
  seq->start(0, *sink);
  run(0, 32 * kStep);
  const std::vector<Rec> a = sink->log;
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new FakeSink();
  seq->start(0, *sink);
  for (uint64_t t = 0; t <= 32 * kStep; t += kStep / 3) {
    run(t, t + kStep / 3 - 1);
    seq->genEdited(sink->now, *sink);
  }
  run(32 * kStep - kStep / 3, 32 * kStep);
  std::vector<uint8_t> na, nb;
  for (const Rec& r : a)
    if (r.b[0] == 0x90) na.push_back(r.b[1]);
  for (const Rec& r : sink->log)
    if (r.b[0] == 0x90) nb.push_back(r.b[1]);
  TEST_ASSERT_TRUE(na.size() > 20);
  TEST_ASSERT_TRUE(na == nb);
}

void test_reseed_restarts_from_the_unheard_step() {
  GenCfg& g = gen(kPitch, 0);
  allNotes();
  seq->start(0, *sink);
  run(0, 4 * kStep + 1000);
  g.seed = 999;
  seq->genReseed(sink->now, *sink, 0);
  run(sink->now, 16 * kStep - 1);
  const auto fresh = expectNotes(g, 16);
  const auto got = onsByStep();
  // The first unheard step plays the new seed's first note.
  int first = -1;
  for (int k = 5; k < 16; ++k)
    if (got.count(k)) {
      first = k;
      break;
    }
  TEST_ASSERT_TRUE(first >= 5);
  for (int k = first; k < 16; ++k) TEST_ASSERT_EQUAL(fresh[k - first], got.at(k)[0]);
}

void test_drum_track_untouched() {
  instrSetType(p->instruments[p->tracks[0].instr], InstrType::Kit);
  for (int i = 0; i < 16; i += 2) {
    p->patterns[0].steps[0][i].note = 100;
    p->patterns[0].steps[0][i].vel = 1;
  }
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  const std::vector<Rec> a = sink->log;
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new FakeSink();
  gen(kGate, 50);
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  TEST_ASSERT_EQUAL(a.size(), sink->log.size());
  for (size_t i = 0; i < a.size(); ++i) TEST_ASSERT_EQUAL_MEMORY(a[i].b, sink->log[i].b, 3);
}

void test_heard_gen_follows_the_heard_step() {
  const GenCfg& g = gen(kPitch, 30);
  allNotes();
  GenState r;
  genReset(r, g, 0);
  TEST_ASSERT_EQUAL(r.reg, seq->heardGen(0).reg);  // stopped: what Play starts from
  TEST_ASSERT_EQUAL(0, seq->genFromStep(0, 0));
  seq->start(0, *sink);
  std::vector<GenState> st;
  expectNotes(g, 16, &st);
  run(0, 5 * kStep + 1000);
  TEST_ASSERT_EQUAL(6, seq->heardGen(0).n);
  TEST_ASSERT_EQUAL(st[5].reg, seq->heardGen(0).reg);
  // The next planned step is ahead of the heard one (lookahead).
  const int from = seq->genFromStep(0, 0);
  TEST_ASSERT_TRUE(from >= 6);
  TEST_ASSERT_EQUAL(static_cast<uint32_t>(from), seq->genNext(0).n);
  TEST_ASSERT_EQUAL(0, seq->genFromStep(0, 1));  // another pattern
}

void test_int_track_gets_generated_notes() {
  const GenCfg& g = gen(kPitch, 0);
  p->tracks[0].out = TrackOut::Int;
  allNotes();
  seq->start(0, *sink);
  run(0, 8 * kStep - 1);
  std::vector<uint8_t> got;
  for (const SynRec& r : sink->syn)
    if ((r.b[0] & 0xF0) == 0x90 && r.track == 0) got.push_back(r.b[1]);
  const auto want = expectNotes(g, 8);
  TEST_ASSERT_TRUE(got.size() >= 8);
  for (int k = 0; k < 8; ++k) TEST_ASSERT_EQUAL(want[k], got[k]);
}

static constexpr uint8_t kBytePitch = static_cast<uint8_t>(GenMode::BytePitch);
static constexpr uint8_t kByteGate = static_cast<uint8_t>(GenMode::ByteGate);

// BYTE PITCH: clocked like PITCH (steps with a note), the note from the formula at n.
void test_byte_pitch_clocked_by_note_steps() {
  GenCfg& g = gen(kBytePitch, 50);
  g.formula = 1;
  for (int i = 0; i < 16; i += 4) p->patterns[0].steps[0][i].note = 60;
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  const auto got = onsByStep();
  TEST_ASSERT_EQUAL(4, got.size());
  GenState s;
  for (int k = 0; k < 4; ++k) {
    s.n = static_cast<uint32_t>(k + 1);
    TEST_ASSERT_EQUAL(genNote(s, g, 0, ScaleType::Major), got.at(k * 4)[0]);
  }
  TEST_ASSERT_EQUAL(4, seq->heardGen(0).n);
}

// BYTE GATE: clocked like GATE (every step), OFF holds, gate and note from the formula.
void test_byte_gate_plays_empty_steps() {
  GenCfg& g = gen(kByteGate, 0);
  g.formula = 2;
  g.density = 60;
  p->patterns[0].steps[0][6].note = kNoteOff;
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  const auto got = onsByStep();
  int hits = 0;
  GenState s;
  for (int k = 0; k < 16; ++k) {
    s.n = static_cast<uint32_t>(k + 1);
    const bool want = k != 6 && genGate(s, g);
    TEST_ASSERT_EQUAL_MESSAGE(want, got.count(k) == 1, "gate");
    if (want) {
      TEST_ASSERT_EQUAL(genNote(s, g, 0, ScaleType::Major), got.at(k)[0]);
      ++hits;
    }
  }
  TEST_ASSERT_EQUAL(16, seq->heardGen(0).n);
  TEST_ASSERT_TRUE(hits > 0);
}

// Capture of BYTE GATE writes empty steps too (gate mode), as heard.
void test_capture_byte_gate() {
  GenCfg& g = gen(kByteGate, 0);
  g.formula = 2;
  g.density = 60;
  seq->start(0, *sink);
  run(0, 16 * kStep - 1);
  const auto got = onsByStep();
  TEST_ASSERT_TRUE(got.size() > 0);
  GenState st;
  genReset(st, g, 0);
  captureGen(p->patterns[0], 0, g, st, 0, 0, ScaleType::Major);
  for (int k = 0; k < 16; ++k) {
    const Step& s = p->patterns[0].steps[0][k];
    TEST_ASSERT_EQUAL(got.count(k) == 1, s.hasNote());
    if (s.hasNote()) TEST_ASSERT_EQUAL(got.at(k)[0], s.note);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pitch_replaces_only_the_pitch);
  RUN_TEST(test_pitch_chord_built_on_the_generated_note);
  RUN_TEST(test_gate_plays_empty_steps_and_off_holds);
  RUN_TEST(test_gate_step_prb_silences_a_hit);
  RUN_TEST(test_muted_track_keeps_clocking);
  RUN_TEST(test_skipped_steps_clock_the_register);
  RUN_TEST(test_fast_track_clocks_per_own_step);
  RUN_TEST(test_flp_lock_overrides_flip);
  RUN_TEST(test_pattern_change_goes_on_start_resets);
  RUN_TEST(test_replan_gives_the_same_events);
  RUN_TEST(test_reseed_restarts_from_the_unheard_step);
  RUN_TEST(test_drum_track_untouched);
  RUN_TEST(test_heard_gen_follows_the_heard_step);
  RUN_TEST(test_int_track_gets_generated_notes);
  RUN_TEST(test_byte_pitch_clocked_by_note_steps);
  RUN_TEST(test_byte_gate_plays_empty_steps);
  RUN_TEST(test_capture_byte_gate);
  return UNITY_END();
}
