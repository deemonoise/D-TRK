#include <unity.h>
#include <cstring>
#include <initializer_list>
#include "dice.h"
#include "dice_music.h"
#include "templates.h"
using namespace mt;
static Project p, reference;
static DiceSnap snap;
void setUp() { templateBuild(0, p); }
void tearDown() {}
static int count(int pat, int role) {
  int n = 0;
  for (int i = 0; i < p.patterns[pat].length; ++i) n += p.patterns[pat].steps[role][i].hasNote();
  return n;
}
static int kicks(int pat) {
  int n = 0;
  for (int i = 0; i < p.patterns[pat].length; ++i) {
    const auto& st = p.patterns[pat].steps[0][i];
    n += st.hasNote() && laneOn(st, kDiceKitLane[kDiceKick]);
  }
  return n;
}
struct DrumSource : DicePhraseSource {
  bool kick = false;
  bool pick(int, DiceRole role, uint32_t, DicePhrase& ph) const override {
    if (role != DiceRole::Drums) return false;
    ph = DicePhrase(); ph.bars = 1; ph.count = 4;
    for (int i = 0; i < 4; ++i)
      ph.events[i] = {static_cast<uint16_t>(24 + i * 96), 12, 42, 90,
                      static_cast<uint8_t>(kick ? 0 : 2)};
    return true;
  }
};
void test_implicit_support_is_sparse_safe_in_every_style() {
  for (int g = 0; g < diceGenreCount(); ++g) for (int style = 0; style < diceStyleCount(g); ++style)
    for (DiceForm form : {DiceForm::Classic, DiceForm::Develop})
      for (int variety : {0, 100}) for (int length : {16, 64, 128})
        for (uint32_t seed : {1u, 777u}) for (Resolution res : {Resolution::Sixteenth, Resolution::Eighth}) {
      templateBuild(0, p); DiceOpts o; o.genre = g; o.style = style; o.variety = variety; o.form = form; o.len = length;
      if (res == Resolution::Eighth) {
        o.lockMask = 32;
        for (auto& pat : p.patterns) { pat.res = res; pat.length = length; }
      }
      diceRoll(p, o, 0, seed, snap);
      const int roles = diceRecipe(g, style)->roles;
      for (int role = 1; role < 5; ++role) if (!(roles & (1u << role)))
        for (int pat = 0; pat < 4; ++pat) {
          const auto& pt = p.patterns[pat];
          const int bars = (pt.length * ticksPerStep(pt.res) + 383) / 384;
          TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(bars, count(pat, role), diceStyleName(g, style));
          for (int i = 0; i < pt.length; ++i) if (pt.steps[role][i].hasNote()) {
            const Step& st = pt.steps[role][i];
            TEST_ASSERT_NULL(st.find(Fx::TIE)); TEST_ASSERT_NULL(st.find(Fx::SLD));
            TEST_ASSERT_NOT_NULL(st.find(Fx::GAT));
            TEST_ASSERT_LESS_OR_EQUAL(100, gatePercent(st.find(Fx::GAT)->val));
          }
        }
    }
}
void test_importing_only_drums_does_not_rewrite_builtin_harmony() {
  DiceOpts o; o.genre = 2; o.style = 0; o.variety = 0; o.groove = 0;
  diceRoll(p, o, 0, 777, snap); reference = p;
  templateBuild(0, p); o.source = DiceSource::Midi; DrumSource source;
  diceRoll(p, o, 0, 777, snap, 0, &source);
  for (int pat = 0; pat < 4; ++pat) for (int role : {1, 3, 4})
    TEST_ASSERT_EQUAL_MEMORY(reference.patterns[pat].steps[role], p.patterns[pat].steps[role], sizeof(p.patterns[pat].steps[role]));
}
void test_midi_hats_keep_recipe_kick_and_syncopated_kicks_survive_intro() {
  DrumSource source;
  for (bool ownKick : {false, true}) {
    templateBuild(0, p); source.kick = ownKick;
    DiceOpts o; o.source = DiceSource::Midi; o.genre = 3; o.style = 1; o.variety = 0; o.groove = 0;
    diceRoll(p, o, 0, 777, snap, 0, &source);
    for (int pat : {0, 1, 2}) TEST_ASSERT_GREATER_THAN(0, kicks(pat));
    if (ownKick) for (int i = 0; i < p.patterns[1].length; ++i) {
      const auto& st = p.patterns[1].steps[0][i];
      TEST_ASSERT_EQUAL(i % 4 == 1, st.hasNote() && laneOn(st, 0));
    }
  }
}
void test_canonical_psy_bass_keeps_rolling_offbeat_and_root() {
  DiceOpts o; o.genre = 2; o.style = 1; o.variety = 0; o.root = 0; o.form = DiceForm::Classic; o.fillGaps = false;
  diceRoll(p, o, 0, 777, snap);
  const auto& pt = p.patterns[0];
  for (int i = 0; i < pt.length; ++i) {
    const auto& st = pt.steps[1][i];
    TEST_ASSERT_EQUAL_MESSAGE(i % 4 != 0, st.hasNote(), "canonical psy bass must retain its kick gap");
    if (st.hasNote()) TEST_ASSERT_TRUE(st.note % 12 == 0 || st.note % 12 == 1);
  }
}
void test_acid_arp_is_a_low_mono_sequence_with_slides() {
  DiceOpts o; o.genre = 1; o.style = 4; o.variety = 0; o.root = 0; o.form = DiceForm::Classic;
  diceRoll(p, o, 0, 777, snap); int slides = 0;
  const auto& sound = p.instruments[p.tracks[4].instr];
  for (int pat = 0; pat < 4; ++pat) for (int i = 0; i < p.patterns[pat].length; ++i) {
    const auto& st = p.patterns[pat].steps[4][i];
    if (!st.hasNote()) continue;
    TEST_ASSERT_INT_WITHIN(18, 42, st.note + sound.transpose);
    TEST_ASSERT_NULL(st.find(Fx::CHD)); slides += st.find(Fx::SLD) != nullptr;
  }
  TEST_ASSERT_GREATER_THAN(0, slides);
}
void test_builtin_fills_keep_the_recipe_kick_anchors_in_every_rhythmic_style() {
  for (int g = 0; g < diceGenreCount(); ++g) for (int style = 0; style < diceStyleCount(g); ++style) {
    const DiceRecipe& recipe = *diceRecipe(g, style);
    if (!(recipe.roles & 1) || !recipe.drums[kDiceKick]) continue;
    const DiceName* chosen = recipe.drums[kDiceKick];
    for (const DiceName* option = chosen; option->w; ++option) if (option->w > chosen->w) chosen = option;
    const int size = static_cast<int>(strlen(chosen->name));
    templateBuild(0, p); DiceOpts o; o.genre = g; o.style = style; o.variety = 0; o.form = DiceForm::Classic;
    diceRoll(p, o, 0, 777, snap);
    for (int pat : {0, 1, 3}) for (int i = 0; i < p.patterns[pat].length; ++i) {
      const char hit = chosen->name[i % size];
      if (hit != 'x' && hit != 'X' && hit != 'o') continue;
      TEST_ASSERT_TRUE_MESSAGE(p.patterns[pat].steps[0][i].hasNote() && laneOn(p.patterns[pat].steps[0][i], 0), diceStyleName(g, style));
    }
    TEST_ASSERT_EQUAL(0, kicks(2));  // the explicit BREAK remains kick-free
  }
}

struct DenseSource : DicePhraseSource {
  bool pick(int, DiceRole role, uint32_t, DicePhrase& ph) const override {
    ph = DicePhrase(); ph.bars = 1;
    if (role == DiceRole::Fx) return false;
    if (role == DiceRole::Chords) {
      ph.count = 3;
      ph.events[0] = {0, 192, 60, 90, 255};
      ph.events[1] = {0, 192, 63, 80, 255};
      ph.events[2] = {0, 192, 67, 80, 255};
      return true;
    }
    ph.count = 16;
    for (int i = 0; i < 16; ++i) {
      const bool drums = role == DiceRole::Drums;
      const int base = role == DiceRole::Bass ? 36 : 72;
      ph.events[i] = {static_cast<uint16_t>(i * 24), 12,
                     static_cast<uint8_t>(drums ? 36 : base + (i % 3) * 3), 90,
                     static_cast<uint8_t>(drums ? (i % 4 ? 2 : 0) : 255)};
    }
    return true;
  }
};
void test_style_foreground_budgets_apply_to_builtin_midi_and_hybrid() {
  DenseSource source;
  for (int g = 0; g < diceGenreCount(); ++g) for (int style = 0; style < diceStyleCount(g); ++style)
    for (DiceSource mode : {DiceSource::Builtin, DiceSource::Midi, DiceSource::Hybrid})
      for (DiceForm form : {DiceForm::Classic, DiceForm::Develop}) {
        templateBuild(0, p); DiceOpts o; o.genre = g; o.style = style; o.variety = 30; o.source = mode; o.form = form;
        diceRoll(p, o, 0, 777, snap, 0, &source);
        const auto& profile = diceMusicProfile(g, style);
        for (int role : {3, 4}) {
          const int budget = role == 3 ? profile.leadPerBar : profile.arpPerBar;
          if (!budget) continue;
          for (int pat = 0; pat < 4; ++pat) {
            const auto& pt = p.patterns[pat]; const int barSteps = 384 / ticksPerStep(pt.res);
            for (int start = 0; start < pt.length; start += barSteps) {
              int attacks = 0;
              for (int i = start; i < start + barSteps && i < pt.length; ++i) attacks += pt.steps[role][i].hasNote();
              TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(budget, attacks, diceStyleName(g, style));
            }
          }
        }
      }
}
void test_profile_processing_preserves_target_locks_and_undo() {
  DiceOpts o; o.genre = 2; o.style = 0; diceRoll(p, o, 0, 777, snap); reference = p;
  o.genre = 1; o.style = 4; o.targetMask = 16;
  diceRoll(p, o, 0, 123, snap);
  for (int pat = 0; pat < 4; ++pat) for (int role = 0; role < 6; ++role) if (role != 4)
    TEST_ASSERT_EQUAL_MEMORY(reference.patterns[pat].steps[role], p.patterns[pat].steps[role], sizeof(p.patterns[pat].steps[role]));
  diceUndo(p, snap); TEST_ASSERT_EQUAL_MEMORY(&reference, &p, sizeof(p));
}

void test_limiter_preserves_unaffected_ties_and_note_offs() {
  Step row[kMaxSteps];
  row[0].note = 60; row[0].fx[0] = {Fx::TIE, 0}; row[0].fx[1] = {Fx::GAT, gateValue(125)};
  row[3].note = kNoteOff;
  row[16].note = 64; row[16].fx[0] = {Fx::TIE, 0};
  row[17].note = 65; row[18].note = 67; row[19].note = 69;
  const Step tied = row[0], off = row[3];
  dice_detail::limitAttacks(row, 32, Resolution::Sixteenth, 2, false);
  TEST_ASSERT_EQUAL_MEMORY(&tied, &row[0], sizeof(Step));
  TEST_ASSERT_EQUAL_MEMORY(&off, &row[3], sizeof(Step));
  TEST_ASSERT_NULL(row[16].find(Fx::TIE));
  TEST_ASSERT_NOT_NULL(row[16].find(Fx::GAT));
  TEST_ASSERT_INT_WITHIN(4, 300, gatePercent(row[16].find(Fx::GAT)->val));
  Step unchanged[kMaxSteps]; memcpy(unchanged, row, sizeof(row));
  dice_detail::limitAttacks(row, 32, Resolution::Sixteenth, 2, false);
  TEST_ASSERT_EQUAL_MEMORY(unchanged, row, sizeof(row));
}

void test_canonical_pulse_and_hold_basses_preserve_factory_attacks() {
  for (int g = 0; g < diceGenreCount(); ++g) for (int style = 0; style < diceStyleCount(g); ++style) {
    const auto motion = diceMusicProfile(g, style).bassMotion;
    if (motion != DiceBassMotion::Pulse && motion != DiceBassMotion::Hold) continue;
    const auto& recipe = *diceRecipe(g, style);
    if (!(recipe.roles & 2)) continue;
    const DiceName* chosen = recipe.basses;
    for (const DiceName* option = chosen; option->w; ++option) if (option->w > chosen->w) chosen = option;
    const char* text = nullptr;
    for (int i = 0; i < bassFactoryCount(); ++i) if (!strcmp(chosen->name, bassFactoryName(i))) text = bassFactoryText(i);
    BassPattern expected; TEST_ASSERT_TRUE(parseBassPattern(text, expected));
    templateBuild(0, p); DiceOpts o; o.genre = g; o.style = style; o.variety = 0; o.form = DiceForm::Classic;
    diceRoll(p, o, 0, 777, snap);
    for (int i = 0; i < p.patterns[0].length; ++i)
      TEST_ASSERT_EQUAL_MESSAGE(expected.steps[i % expected.len].kind == ArpKind::Note,
                                p.patterns[0].steps[1][i].hasNote(), recipe.style);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_canonical_pulse_and_hold_basses_preserve_factory_attacks);
  RUN_TEST(test_limiter_preserves_unaffected_ties_and_note_offs);
  RUN_TEST(test_style_foreground_budgets_apply_to_builtin_midi_and_hybrid);
  RUN_TEST(test_profile_processing_preserves_target_locks_and_undo);
  RUN_TEST(test_builtin_fills_keep_the_recipe_kick_anchors_in_every_rhythmic_style);
  RUN_TEST(test_implicit_support_is_sparse_safe_in_every_style);
  RUN_TEST(test_importing_only_drums_does_not_rewrite_builtin_harmony);
  RUN_TEST(test_midi_hats_keep_recipe_kick_and_syncopated_kicks_survive_intro);
  RUN_TEST(test_canonical_psy_bass_keeps_rolling_offbeat_and_root);
  RUN_TEST(test_acid_arp_is_a_low_mono_sequence_with_slides);
  return UNITY_END();
}
