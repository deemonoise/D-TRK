// MUT (mut.h): one mutator mode per track (EVO or a VAR mode), mutFix, the sequencer playing only the
// active mode, a mode change resetting the state, Capture live.
#include <string.h>
#include <unity.h>
#include <vector>
#include "mut.h"
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

static std::vector<On> pass(const std::vector<On>& v, int k) {
  std::vector<On> out;
  for (On o : v)
    if (o.t >= k * kPassUs && o.t < (k + 1) * kPassUs) {
      o.t -= k * kPassUs;
      out.push_back(o);
    }
  return out;
}

// Note-ons at or after t0 and before t1.
static std::vector<On> span(const std::vector<On>& v, uint64_t t0, uint64_t t1) {
  std::vector<On> out;
  for (const On& o : v)
    if (o.t >= t0 && o.t < t1) out.push_back(o);
  return out;
}

static bool same(const std::vector<On>& x, const std::vector<On>& y) {
  if (x.size() != y.size()) return false;
  for (size_t i = 0; i < x.size(); ++i)
    if (x[i].t != y[i].t || x[i].ch != y[i].ch || x[i].note != y[i].note || x[i].vel != y[i].vel) return false;
  return true;
}

static void melody() {
  p->scaleType = static_cast<uint8_t>(ScaleType::Major);
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[0][i].note = static_cast<uint8_t>(60 + (i % 8));
}

static void fresh() {
  delete seq;
  delete sink;
  seq = new Sequencer(*p);
  seq->seed(1);
  seq->setFollowSalt(0);
  sink = new Sink();
}

static std::vector<On> play(int passes) {
  fresh();
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

static void kit(int tr) {
  instrSetType(p->instruments[tr], InstrType::Kit);
  p->tracks[tr].instr = static_cast<uint8_t>(tr);
}

static uint8_t m(VarMode v) { return static_cast<uint8_t>(v); }

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

// ---- modes ----

void test_mode_of() {
  VarCfg c;
  c.mode = m(VarMode::Evo);
  TEST_ASSERT_EQUAL(m(VarMode::Evo), m(mutModeOf(c, false)));
  TEST_ASSERT_EQUAL(m(VarMode::Off), m(varModeOf(c, false)));  // the VAR engine is off in EVO
  c.mode = m(VarMode::Ca2d);
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), m(mutModeOf(c, false)));
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), m(mutModeOf(c, true)));
  c.mode = m(VarMode::Count);
  TEST_ASSERT_EQUAL(m(VarMode::Off), m(mutModeOf(c, true)));
  p->var[3].mode = m(VarMode::Markov);
  TEST_ASSERT_EQUAL(m(VarMode::Markov), m(mutMode(*p, 3)));
  TEST_ASSERT_TRUE(mutOn(*p, 3));
  TEST_ASSERT_FALSE(mutOn(*p, 2));
  TEST_ASSERT_FALSE(p->evoOn(3));
  p->var[3].mode = m(VarMode::Evo);
  TEST_ASSERT_TRUE(p->evoOn(3));
}

void test_clamp_keeps_evo() {
  VarCfg c;
  c.mode = m(VarMode::Evo);
  varClamp(c);
  TEST_ASSERT_EQUAL(m(VarMode::Evo), c.mode);
  c.mode = m(VarMode::Count);
  varClamp(c);
  TEST_ASSERT_EQUAL(0, c.mode);
}

void test_mode_lists() {
  VarMode v[kMutModesMax];
  TEST_ASSERT_EQUAL(4, mutModes(false, v));
  TEST_ASSERT_EQUAL(m(VarMode::Off), m(v[0]));
  TEST_ASSERT_EQUAL(m(VarMode::Evo), m(v[1]));
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), m(v[2]));
  TEST_ASSERT_EQUAL(m(VarMode::Markov), m(v[3]));
  TEST_ASSERT_EQUAL(5, mutModes(true, v));
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), m(v[3]));
  TEST_ASSERT_EQUAL(m(VarMode::Markov), m(v[4]));
}

// Entering EVO with nothing to do (amount 0 / mask 0) takes the defaults; CA2D on a melodic track is CA1D.
void test_set_mode() {
  mutSetMode(*p, 0, VarMode::Evo);
  TEST_ASSERT_EQUAL(m(VarMode::Evo), p->var[0].mode);
  TEST_ASSERT_EQUAL(kEvoAmountOn, p->evo[0].amount);
  TEST_ASSERT_EQUAL(EvoCfg().mask, p->evo[0].mask);
  p->evo[1] = EvoCfg{70, 2, 30, kEvoShift};
  mutSetMode(*p, 1, VarMode::Evo);
  TEST_ASSERT_EQUAL(70, p->evo[1].amount);
  TEST_ASSERT_EQUAL(kEvoShift, p->evo[1].mask);
  mutSetMode(*p, 1, VarMode::Off);
  TEST_ASSERT_EQUAL(m(VarMode::Off), p->var[1].mode);
  TEST_ASSERT_EQUAL(70, p->evo[1].amount);  // kept for coming back
  mutSetMode(*p, 2, VarMode::Ca2d);
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), p->var[2].mode);
  kit(4);
  mutSetMode(*p, 4, VarMode::Ca2d);
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), p->var[4].mode);
}

// ---- mutFix ----

void test_fix_both_on_keeps_var() {
  p->var[0] = ca1d(30);
  p->evo[0] = EvoCfg{50, 2, 10, kEvoNote | kEvoRhythm};
  mutFix(*p);
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), p->var[0].mode);
  TEST_ASSERT_EQUAL(30, p->var[0].rule);
  TEST_ASSERT_EQUAL(0, p->evo[0].mask);
  TEST_ASSERT_EQUAL(50, p->evo[0].amount);
}

void test_fix_only_evo_becomes_evo() {
  p->evo[2] = EvoCfg{40, 1, 0, kEvoShift};
  p->evo[3] = EvoCfg{0, 1, 0, kEvoShift};       // amount 0: was off
  p->evo[4] = EvoCfg{40, 1, 0, 0};              // nothing in the mask: was off
  mutFix(*p);
  TEST_ASSERT_EQUAL(m(VarMode::Evo), p->var[2].mode);
  TEST_ASSERT_EQUAL(kEvoShift, p->evo[2].mask);
  TEST_ASSERT_EQUAL(m(VarMode::Off), p->var[3].mode);
  TEST_ASSERT_EQUAL(m(VarMode::Off), p->var[4].mode);
  TEST_ASSERT_EQUAL(m(VarMode::Off), p->var[5].mode);
  // Again: nothing more changes (EVO stays EVO with its mask).
  mutFix(*p);
  TEST_ASSERT_EQUAL(m(VarMode::Evo), p->var[2].mode);
  TEST_ASSERT_EQUAL(kEvoShift, p->evo[2].mask);
}

void test_fix_ca2d_melodic_is_ca1d_kit_kept() {
  p->var[0].mode = m(VarMode::Ca2d);
  p->var[0].rule = 110;
  p->var[0].rule2 = 2;
  kit(1);
  p->var[1].mode = m(VarMode::Ca2d);
  mutFix(*p);
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), p->var[0].mode);
  TEST_ASSERT_EQUAL(110, p->var[0].rule);
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), p->var[1].mode);
}

// The Mut row: one mode per turn whatever the delta, with wrap, from the mode shown (CA2D on a melodic
// track shows and steps as CA1D).
void test_cycle_steps_one_from_shown() {
  p->var[0].mode = m(VarMode::Ca2d);  // melodic: shown CA1D (OFF EVO CA1D MARKOV)
  TEST_ASSERT_EQUAL(m(VarMode::Markov), m(mutCycle(*p, 0, 1)));
  TEST_ASSERT_EQUAL(m(VarMode::Evo), m(mutCycle(*p, 0, -1)));
  TEST_ASSERT_EQUAL(m(VarMode::Markov), m(mutCycle(*p, 0, 10)));  // Shift: still one
  TEST_ASSERT_EQUAL(m(VarMode::Evo), m(mutCycle(*p, 0, -10)));
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), m(mutCycle(*p, 0, 0)));
  p->var[0].mode = m(VarMode::Markov);
  TEST_ASSERT_EQUAL(m(VarMode::Off), m(mutCycle(*p, 0, 1)));  // wrap
  kit(1);
  p->var[1].mode = m(VarMode::Ca1d);
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), m(mutCycle(*p, 1, 1)));
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), m(mutCycle(*p, 1, 2)));  // a fast turn: one too
  p->var[1].mode = m(VarMode::Off);
  TEST_ASSERT_EQUAL(m(VarMode::Markov), m(mutCycle(*p, 1, -10)));
}

// ---- the sequencer: one mode per track ----

// CA1D with an EvoCfg that would drift: plays exactly as CA1D alone (no EVO on top).
void test_var_mode_ignores_evo_cfg() {
  p->patterns[0].steps[0][3].note = 64;
  p->var[0] = ca1d(90);
  const std::vector<On> varOnly = play(8);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  TEST_ASSERT_TRUE(same(varOnly, play(8)));
}

// EVO with VAR fields set: exactly as EVO; the same EvoCfg without the mode plays the pattern.
void test_evo_mode_alone() {
  melody();
  const std::vector<On> plain = play(6);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  TEST_ASSERT_TRUE(same(plain, play(6)));
  p->var[0].mode = m(VarMode::Evo);
  const std::vector<On> evo = play(6);
  TEST_ASSERT_FALSE(same(plain, evo));
  p->var[0].every = 3;
  p->var[0].rule = 30;
  p->var[0].amount = 100;
  TEST_ASSERT_TRUE(same(evo, play(6)));
}

// A mode change (MutEdit) resets the state: EVO without its changes, VAR at generation 0.
void test_mode_change_resets_state() {
  melody();
  p->patterns[0].steps[0][5].note = kNoteEmpty;  // CA1D: not all cells alive (rule 90 would kill them all)
  p->patterns[0].steps[0][9].note = kNoteEmpty;
  p->var[0].mode = m(VarMode::Evo);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  play(3);
  TEST_ASSERT_FALSE(evoIdle(seq->heardEvo(0)));
  uint64_t t = 3 * kPassUs - 1;
  p->var[0] = ca1d(90);
  sink->now = t;
  seq->mutEdited(t, *sink);
  TEST_ASSERT_TRUE(evoIdle(seq->heardEvo(0)));
  run(t, 6 * kPassUs - 1);
  TEST_ASSERT_TRUE(seq->heardVar(0).sinceReset > 0);
  TEST_ASSERT_TRUE(evoIdle(seq->heardEvo(0)));  // not mutated in CA1D
  t = 6 * kPassUs - 1;
  p->var[0].mode = m(VarMode::Markov);
  sink->now = t;
  seq->mutEdited(t, *sink);
  TEST_ASSERT_EQUAL(0, seq->heardVar(0).sinceReset);
  // Back to EVO: from no changes, not the old drift.
  run(t, 7 * kPassUs - 1);
  t = 7 * kPassUs - 1;
  p->var[0].mode = m(VarMode::Evo);
  sink->now = t;
  seq->mutEdited(t, *sink);
  TEST_ASSERT_TRUE(evoIdle(seq->heardEvo(0)));
  TEST_ASSERT_EQUAL(0, seq->heardVar(0).sinceReset);
}

// An edit without a mode change keeps the state.
void test_edit_same_mode_keeps_state() {
  melody();
  p->var[0].mode = m(VarMode::Evo);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoMaskAll};
  play(3);
  const uint64_t t = 3 * kPassUs - 1;
  p->evo[0].home = 50;
  sink->now = t;
  seq->mutEdited(t, *sink);
  TEST_ASSERT_FALSE(evoIdle(seq->heardEvo(0)));
}

// A track's instrument changes type under a CA2D track (no MutEdit): the stored mode stays CA2D, the state
// built as CA2D plays the pattern and the next generation is CA1D's from generation 0. A later edit of
// the same mode keeps that state (no reset).
void test_kit_change_keeps_mode_no_needless_reset() {
  kit(0);
  for (int l = 0; l < kKitLanes; ++l) p->instruments[0].kit[l].note = static_cast<uint8_t>(36 + l);
  p->patterns[0].steps[0][1].note = 100;
  p->patterns[0].steps[0][1].vel = 0x07;
  p->var[0] = ca1d(90);
  p->var[0].mode = m(VarMode::Ca2d);
  p->var[0].rule2 = static_cast<uint8_t>(VarRule2::Life);
  play(2);
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), seq->heardVar(0).mode);
  instrSetType(p->instruments[0], InstrType::Chip);  // the track leaves KIT
  run(2 * kPassUs - 1, 6 * kPassUs - 1);
  TEST_ASSERT_EQUAL(m(VarMode::Ca2d), p->var[0].mode);  // stored as is
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), m(mutMode(*p, 0)));
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), seq->heardVar(0).mode);
  const uint8_t since = seq->heardVar(0).sinceReset;
  TEST_ASSERT_TRUE(since > 0);
  const uint64_t t = 6 * kPassUs - 1;
  p->var[0].rule = 30;  // an edit, same mode
  sink->now = t;
  seq->mutEdited(t, *sink);
  TEST_ASSERT_EQUAL(since, seq->heardVar(0).sinceReset);
}

// ---- Capture live ----

static Step scratch[kMaxSteps];

void test_capture_off_and_other_pattern() {
  melody();
  fresh();
  TEST_ASSERT_EQUAL(static_cast<int>(LiveCapture::Off), static_cast<int>(seq->captureLive(0, 0, 0, scratch)));
  p->var[0] = ca1d(90);
  seq->start(0, *sink);
  run(0, kPassUs / 2);
  TEST_ASSERT_EQUAL(static_cast<int>(LiveCapture::OtherPattern),
                    static_cast<int>(seq->captureLive(kPassUs / 2, 0, 1, scratch)));
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), p->var[0].mode);
}

// GEN only, stopped: the loop from the reset register is written; GEN off; plays as GEN did.
void test_capture_gen_stopped() {
  melody();
  p->gen[0].mode = static_cast<uint8_t>(GenMode::Pitch);
  p->gen[0].flip = 0;
  const std::vector<On> ref = play(4);
  fresh();
  TEST_ASSERT_EQUAL(static_cast<int>(LiveCapture::Ok), static_cast<int>(seq->captureLive(0, 0, 0, scratch)));
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::Off), p->gen[0].mode);
  TEST_ASSERT_TRUE(same(ref, play(4)));
}

// Mid-pass with GEN PITCH (flip 0) over EVO: the rest plays as before, the pattern holds MUT + GEN, GEN
// is off, EVO stays on with no changes.
void test_capture_live_evo_gen_no_jump() {
  melody();
  p->var[0].mode = m(VarMode::Evo);
  p->evo[0] = EvoCfg{100, 1, 0, kEvoRhythm | kEvoRtrgVol | kEvoShift};
  p->gen[0].mode = static_cast<uint8_t>(GenMode::Pitch);
  p->gen[0].flip = 0;
  p->gen[0].len = 5;
  const std::vector<On> ref = play(4);
  fresh();
  seq->start(0, *sink);
  const uint64_t t = 2 * kPassUs + kPassUs / 3;
  run(0, t);
  TEST_ASSERT_FALSE(evoIdle(seq->heardEvo(0)));
  static Pattern before;
  before = p->patterns[0];
  TEST_ASSERT_EQUAL(static_cast<int>(LiveCapture::Ok), static_cast<int>(seq->captureLive(t, 0, 0, scratch)));
  TEST_ASSERT_FALSE(0 == memcmp(&before.steps[0], &p->patterns[0].steps[0], sizeof(before.steps[0])));
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::Off), p->gen[0].mode);
  TEST_ASSERT_EQUAL(m(VarMode::Evo), p->var[0].mode);
  TEST_ASSERT_TRUE(evoIdle(seq->heardEvo(0)));
  run(t, 3 * kPassUs - 1);
  TEST_ASSERT_TRUE(same(span(ref, t + 1, 3 * kPassUs), span(sink->on, t + 1, 3 * kPassUs)));
  TEST_ASSERT_TRUE(span(ref, t + 1, 3 * kPassUs).size() > 0);
}

// The same over CA1D (every 2: the generation that plays goes on for the next pass too).
void test_capture_live_var_no_jump() {
  melody();
  p->patterns[0].steps[0][5].note = kNoteEmpty;
  p->patterns[0].steps[0][9].note = kNoteEmpty;
  p->var[0] = ca1d(30);
  p->var[0].every = 3;
  const std::vector<On> ref = play(6);
  fresh();
  seq->start(0, *sink);
  const uint64_t t = 3 * kPassUs + kPassUs / 4;  // generation 1 plays passes 3..5
  run(0, t);
  TEST_ASSERT_TRUE(seq->heardVar(0).sinceReset > 0);
  TEST_ASSERT_EQUAL(static_cast<int>(LiveCapture::Ok), static_cast<int>(seq->captureLive(t, 0, 0, scratch)));
  TEST_ASSERT_EQUAL(0, seq->heardVar(0).sinceReset);
  TEST_ASSERT_EQUAL(m(VarMode::Ca1d), p->var[0].mode);
  run(t, 6 * kPassUs - 1);
  TEST_ASSERT_TRUE(same(span(ref, t + 1, 6 * kPassUs), span(sink->on, t + 1, 6 * kPassUs)));
}

// Markov on a stopped transport: the reset state is the pattern, nothing changes but the call works.
void test_capture_stopped_mut_is_pattern() {
  melody();
  p->var[0].mode = m(VarMode::Markov);
  static Pattern before;
  before = p->patterns[0];
  fresh();
  TEST_ASSERT_EQUAL(static_cast<int>(LiveCapture::Ok), static_cast<int>(seq->captureLive(0, 0, 0, scratch)));
  TEST_ASSERT_EQUAL_MEMORY(&before, &p->patterns[0], sizeof(Pattern));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mode_of);
  RUN_TEST(test_clamp_keeps_evo);
  RUN_TEST(test_mode_lists);
  RUN_TEST(test_set_mode);
  RUN_TEST(test_fix_both_on_keeps_var);
  RUN_TEST(test_fix_only_evo_becomes_evo);
  RUN_TEST(test_fix_ca2d_melodic_is_ca1d_kit_kept);
  RUN_TEST(test_cycle_steps_one_from_shown);
  RUN_TEST(test_var_mode_ignores_evo_cfg);
  RUN_TEST(test_evo_mode_alone);
  RUN_TEST(test_mode_change_resets_state);
  RUN_TEST(test_edit_same_mode_keeps_state);
  RUN_TEST(test_kit_change_keeps_mode_no_needless_reset);
  RUN_TEST(test_capture_off_and_other_pattern);
  RUN_TEST(test_capture_gen_stopped);
  RUN_TEST(test_capture_live_evo_gen_no_jump);
  RUN_TEST(test_capture_live_var_no_jump);
  RUN_TEST(test_capture_stopped_mut_is_pattern);
  return UNITY_END();
}
