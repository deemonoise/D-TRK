#include <string.h>
#include <unity.h>
#include "model.h"
#include "mutate.h"
#include "presets_factory.h"

using namespace mt;

static Project p, before;

void setUp() {}
void tearDown() {}

void test_kit_and_zero_strength_do_nothing() {
  Instrument k;
  instrSetType(k, InstrType::Kit);
  Instrument was;
  memcpy(&was, &k, sizeof(Instrument));
  TEST_ASSERT_FALSE(mutateApplies(k, 50));
  TEST_ASSERT_FALSE(mutateInstrument(k, 50, 1));
  TEST_ASSERT_EQUAL_MEMORY(&was, &k, sizeof(Instrument));

  Instrument s;
  instrSetType(s, InstrType::Synth);
  memcpy(&was, &s, sizeof(Instrument));
  TEST_ASSERT_FALSE(mutateApplies(s, 0));
  TEST_ASSERT_FALSE(mutateInstrument(s, 0, 1));
  TEST_ASSERT_EQUAL_MEMORY(&was, &s, sizeof(Instrument));
  TEST_ASSERT_TRUE(mutateApplies(s, 1));
  TEST_ASSERT_TRUE(mutateApplies(s, 100));
}

namespace {

bool ampEnv(const Instrument& m) {
  return m.type == InstrType::Chip || m.type == InstrType::Sample || m.type == InstrType::Synth ||
         m.type == InstrType::Lofi || (m.type == InstrType::Fm && fmGated(m.machine));
}
// Macro k moves: FM, DRUM, SYNTH, LOFI (not a slot the LOFI machine leaves unused).
bool macros(const Instrument& m, int k) {
  if (m.type == InstrType::Lofi) return *lofiMacroName(m.machine, k);
  return m.type == InstrType::Fm || m.type == InstrType::Drum || m.type == InstrType::Synth;
}

// A mutated field: in lo..hi and moved by at most the strength's share, or unchanged when it was outside.
void field(int was, int now, int lo, int hi, int strength, const char* what) {
  if (was < lo || was > hi) {
    TEST_ASSERT_EQUAL_MESSAGE(was, now, what);
    return;
  }
  TEST_ASSERT_TRUE_MESSAGE(now >= lo && now <= hi, what);
  const int span = (hi - lo) * strength / 200;
  TEST_ASSERT_TRUE_MESSAGE(now - was <= span && was - now <= span, what);
}
void same(int was, int now, const char* what) { TEST_ASSERT_EQUAL_MESSAGE(was, now, what); }

// Mutates a copy of was and checks every field against the table in mutate.h.
void checkOne(const Instrument& was, int strength, uint32_t seed) {
  Instrument m;
  memcpy(&m, &was, sizeof(Instrument));
  TEST_ASSERT_TRUE(mutateInstrument(m, strength, seed));
  const int s = strength;
  if (ampEnv(was)) {
    field(was.attack, m.attack, 0, 100, s, "attack");
    field(was.decay, m.decay, 1, 127, s, "decay");
    field(was.sustain, m.sustain, 0, 127, s, "sustain");
    field(was.release, m.release, 0, 110, s, "release");
  } else {
    same(was.attack, m.attack, "attack");
    same(was.decay, m.decay, "decay");
    same(was.sustain, m.sustain, "sustain");
    same(was.release, m.release, "release");
  }
  if (was.fltMode != static_cast<uint8_t>(FltMode::Off)) {
    field(was.cutoff, m.cutoff, 10, 127, s, "cutoff");
    field(was.reso, m.reso, 0, 110, s, "reso");
    field(was.fenv, m.fenv, -64, 63, s, "fenv");
    field(was.fAtk, m.fAtk, 0, 100, s, "fAtk");
    field(was.fDec, m.fDec, 1, 127, s, "fDec");
  } else {
    same(was.cutoff, m.cutoff, "cutoff");
    same(was.reso, m.reso, "reso");
    same(was.fenv, m.fenv, "fenv");
    same(was.fAtk, m.fAtk, "fAtk");
    same(was.fDec, m.fDec, "fDec");
  }
  for (int k = 0; k < kFmMacros; ++k) {
    if (macros(was, k)) field(was.macro[k], m.macro[k], 0, 127, s, "macro");
    else same(was.macro[k], m.macro[k], "macro");
  }
  if (was.type == InstrType::Fm) field(was.fmSat, m.fmSat, 0, 127, s, "fmSat");
  else same(was.fmSat, m.fmSat, "fmSat");
  if (was.type == InstrType::Synth) {
    field(was.synSub, m.synSub, 0, 127, s, "synSub");
    field(was.synNoise, m.synNoise, 0, 80, s, "synNoise");
    field(was.synEAtk, m.synEAtk, 0, 100, s, "synEAtk");
    field(was.synEDec, m.synEDec, 1, 127, s, "synEDec");
  } else {
    same(was.synSub, m.synSub, "synSub");
    same(was.synNoise, m.synNoise, "synNoise");
    same(was.synEAtk, m.synEAtk, "synEAtk");
    same(was.synEDec, m.synEDec, "synEDec");
  }
  if (was.type == InstrType::Chip && waveHasDuty(was.wave)) {
    field(was.duty, m.duty, 5, 95, s, "duty");
    field(was.pwmDepth, m.pwmDepth, 0, 49, s, "pwmDepth");
    if (was.pwmDepth > 0) field(was.pwmRate, m.pwmRate, 0, 127, s, "pwmRate");
    else same(was.pwmRate, m.pwmRate, "pwmRate");
  } else {
    same(was.duty, m.duty, "duty");
    same(was.pwmDepth, m.pwmDepth, "pwmDepth");
    same(was.pwmRate, m.pwmRate, "pwmRate");
  }
  // Everything else stays bit for bit: put the mutable fields back and compare the bytes.
  Instrument x;
  memcpy(&x, &m, sizeof(Instrument));
  x.attack = was.attack, x.decay = was.decay, x.sustain = was.sustain, x.release = was.release;
  x.cutoff = was.cutoff, x.reso = was.reso, x.fenv = was.fenv, x.fAtk = was.fAtk, x.fDec = was.fDec;
  memcpy(x.macro, was.macro, sizeof(x.macro));
  x.fmSat = was.fmSat;
  x.synSub = was.synSub, x.synNoise = was.synNoise, x.synEAtk = was.synEAtk, x.synEDec = was.synEDec;
  x.duty = was.duty, x.pwmDepth = was.pwmDepth, x.pwmRate = was.pwmRate;
  TEST_ASSERT_EQUAL_MEMORY(&was, &x, sizeof(Instrument));
}

}  // namespace

void test_ranges_every_factory_preset() {
  static const InstrType kTypes[] = {InstrType::Chip, InstrType::Fm, InstrType::Drum, InstrType::Synth,
                                     InstrType::Lofi};
  static const int kStrengths[] = {15, 60, 100};
  for (InstrType t : kTypes)
    for (int i = 0; i < factoryCount(t); ++i) {
      Instrument was;
      factoryBuild(t, i, was);
      for (int st : kStrengths)
        for (uint32_t seed = 1; seed <= 8; ++seed) checkOne(was, st, seed);
    }
}

// LOFI: macros and envelope move; machine and formula never (also APC: its unused slot 5 stays).
void test_lofi_macros_not_machine_or_formula() {
  for (int mc = 0; mc < static_cast<int>(LofiMachine::Count); ++mc) {
    Instrument was;
    instrSetType(was, InstrType::Lofi);
    lofiSetMachine(was, static_cast<uint8_t>(mc));
    was.lofiFormula = 7;
    was.macro[4] = 50;
    int moved = 0;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      checkOne(was, 100, seed);
      Instrument m;
      memcpy(&m, &was, sizeof(Instrument));
      mutateInstrument(m, 100, seed);
      TEST_ASSERT_EQUAL(was.machine, m.machine);
      TEST_ASSERT_EQUAL(7, m.lofiFormula);
      moved += memcmp(m.macro, was.macro, sizeof(m.macro)) != 0;
    }
    TEST_ASSERT_TRUE(moved >= 18);
  }
}

void test_sample_and_reset_instruments() {
  Instrument s;
  instrSetType(s, InstrType::Sample);
  s.fltMode = static_cast<uint8_t>(FltMode::Lp);
  s.cutoff = 90;
  for (uint32_t seed = 1; seed <= 20; ++seed) checkOne(s, 100, seed);
  p.reset();  // INS1..32: FM TONE (gated: the envelope moves)
  for (uint32_t seed = 1; seed <= 20; ++seed) checkOne(p.instruments[0], 100, seed);
}

void test_moves_something_and_is_seeded() {
  Instrument was;
  factoryBuild(InstrType::Synth, 0, was);
  int moved = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Instrument a, b;
    memcpy(&a, &was, sizeof(Instrument));
    memcpy(&b, &was, sizeof(Instrument));
    mutateInstrument(a, 50, seed);
    mutateInstrument(b, 50, seed);
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(Instrument));  // same seed, same result
    moved += memcmp(&a, &was, sizeof(Instrument)) != 0;
  }
  TEST_ASSERT_TRUE(moved >= 9);
}

// SID PS / PT / PTS and AY TN have a pulse in them: their duty and PWM mutate as PULSE's do.
void test_pulse_like_waves_mutate_duty() {
  static const Wave kWaves[] = {Wave::SidPs, Wave::SidPt, Wave::SidPts, Wave::AyToneNoise};
  for (Wave w : kWaves) {
    Instrument was;
    factoryBuild(InstrType::Chip, 0, was);
    was.wave = static_cast<uint8_t>(w);
    was.duty = 50;
    was.pwmDepth = 20;
    was.pwmRate = 40;
    int moved = 0;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      Instrument m;
      memcpy(&m, &was, sizeof(Instrument));
      mutateInstrument(m, 60, seed);
      moved += m.duty != was.duty || m.pwmDepth != was.pwmDepth || m.pwmRate != was.pwmRate;
      checkOne(was, 60, seed);
    }
    TEST_ASSERT_TRUE(moved >= 8);
  }
}

void test_slot_undo_bit_exact() {
  p.reset();
  factoryBuild(InstrType::Synth, 3, p.instruments[5]);
  memcpy(&before, &p, sizeof(Project));
  MutateUndo u;
  TEST_ASSERT_FALSE(mutateRestore(p, u));  // empty
  TEST_ASSERT_TRUE(mutateSlot(p, 5, 60, 1234, u));
  TEST_ASSERT_EQUAL(5, u.slot);
  TEST_ASSERT_TRUE(memcmp(&before, &p, sizeof(Project)) != 0);
  TEST_ASSERT_TRUE(mutateRestore(p, u));
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  TEST_ASSERT_EQUAL(-1, u.slot);
  TEST_ASSERT_FALSE(mutateRestore(p, u));  // one step

  // A refused Mutate keeps the previous undo.
  TEST_ASSERT_TRUE(mutateSlot(p, 5, 30, 7, u));
  instrSetType(p.instruments[6], InstrType::Kit);
  TEST_ASSERT_FALSE(mutateSlot(p, 6, 30, 7, u));
  TEST_ASSERT_FALSE(mutateSlot(p, kInstruments, 30, 7, u));
  TEST_ASSERT_EQUAL(5, u.slot);
}

// A later edit of the slot (a preset load, a type change, a slice edit, a knob) wins: the undo is
// refused and the slot stays as edited.
void test_slot_undo_refused_after_edit() {
  p.reset();
  factoryBuild(InstrType::Synth, 3, p.instruments[5]);
  MutateUndo u;
  TEST_ASSERT_TRUE(mutateSlot(p, 5, 60, 99, u));
  TEST_ASSERT_TRUE(mutateCanRestore(p, u));
  TEST_ASSERT_FALSE(mutateCanRestore(p, MutateUndo()));  // empty
  factoryBuild(InstrType::Chip, 1, p.instruments[5]);  // a preset loaded since
  memcpy(&before, &p, sizeof(Project));
  TEST_ASSERT_FALSE(mutateCanRestore(p, u));
  TEST_ASSERT_FALSE(mutateRestore(p, u));
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  TEST_ASSERT_EQUAL(-1, u.slot);  // dropped
  // Another slot's edit leaves it alone.
  TEST_ASSERT_TRUE(mutateSlot(p, 5, 60, 99, u));
  p.instruments[6].vol = 12;
  TEST_ASSERT_TRUE(mutateCanRestore(p, u));
  TEST_ASSERT_TRUE(mutateRestore(p, u));
}

// FM: SAT moves with the macros; DRUM keeps it.
void test_fm_sat_moves() {
  Instrument fm;
  instrSetType(fm, InstrType::Fm);
  fm.fmSat = 64;
  int moved = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    checkOne(fm, 100, seed);
    Instrument a;
    memcpy(&a, &fm, sizeof(Instrument));
    mutateInstrument(a, 100, seed);
    moved += a.fmSat != fm.fmSat;
  }
  TEST_ASSERT_TRUE(moved >= 8);
  Instrument d;
  instrSetType(d, InstrType::Drum);
  d.fmSat = 64;
  for (uint32_t seed = 1; seed <= 5; ++seed) checkOne(d, 100, seed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_kit_and_zero_strength_do_nothing);
  RUN_TEST(test_ranges_every_factory_preset);
  RUN_TEST(test_lofi_macros_not_machine_or_formula);
  RUN_TEST(test_pulse_like_waves_mutate_duty);
  RUN_TEST(test_sample_and_reset_instruments);
  RUN_TEST(test_moves_something_and_is_seeded);
  RUN_TEST(test_slot_undo_bit_exact);
  RUN_TEST(test_slot_undo_refused_after_edit);
  RUN_TEST(test_fm_sat_moves);
  return UNITY_END();
}
