#include <unity.h>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "dice.h"
#include "dice_music.h"
#include "templates.h"
using namespace mt;
static Project p, original;
static DiceSnap snap;
void setUp() { templateBuild(0, p); }
void tearDown() {}
static int attacks(int pat, int role, int start = 0) {
  int n = 0;
  for (int i = start; i < p.patterns[pat].length; ++i) n += p.patterns[pat].steps[role][i].hasNote();
  return n;
}
struct SectionMidi : DicePhraseSource {
  bool pick(int, DiceRole role, uint32_t, DicePhrase& phrase) const override {
    phrase = DicePhrase(); phrase.bars = 1;
    for (int i = 0; i < 16; ++i) {
      if (role == DiceRole::Chords && i % 8) continue;
      if (role == DiceRole::Fx && i) continue;
      auto& e = phrase.events[phrase.count++];
      e.tick = i * 24; e.duration = role == DiceRole::Chords ? 192 : 18;
      e.note = role == DiceRole::Bass ? 36 : 60 + i % 3 * 3;
      e.vel = 90;
      e.lane = role == DiceRole::Drums ? (i % 4 ? 2 : 0) : 255;
    }
    return true;
  }
};
void test_develop_main_counterline_and_outro_have_structural_contrast_in_all_styles() {
  SectionMidi source;
  for (int genre = 0; genre < diceGenreCount(); ++genre)
    for (int style = 0; style < diceStyleCount(genre); ++style)
      for (int length : {16, 64}) for (int variety : {0, 30, 100}) for (uint32_t seed : {1u, 777u})
        for (DiceSource mode : {DiceSource::Builtin, DiceSource::Midi, DiceSource::Hybrid}) {
          templateBuild(0, p); DiceOpts o; o.genre = genre; o.style = style; o.len = length;
          o.variety = variety; o.groove = 0; o.source = mode;
          diceRoll(p, o, 0, seed, snap, 0, &source);
          const auto& recipe = *diceRecipe(genre, style);
          bool nativeContrast = false;
          for (int role : {4, 3, 2}) if ((recipe.roles & (1 << role)) && attacks(2, role) >= 2) {
            nativeContrast |= attacks(1, role) * 4 <= attacks(2, role) * 3;
          }
          char context[160];
          std::snprintf(context, sizeof(context), "%s len=%d variety=%d seed=%u source=%d MAIN(C/L/A)=%d/%d/%d VAR=%d/%d/%d",
                        recipe.style, length, variety, static_cast<unsigned>(seed), static_cast<int>(mode),
                        attacks(1, 2), attacks(1, 3), attacks(1, 4), attacks(2, 2), attacks(2, 3), attacks(2, 4));
          TEST_ASSERT_TRUE_MESSAGE(nativeContrast, context);
          // Bass and kick carry the same foundation under MAIN's reduced counterline and VAR.
          if (recipe.roles & 2)
            TEST_ASSERT_EQUAL_MEMORY(p.patterns[1].steps[1], p.patterns[2].steps[1], sizeof(p.patterns[1].steps[1]));
          int mainTops = 0, fullTops = 0;
          for (int i = 0; i < p.patterns[1].length; ++i) {
            for (int lane : {0, 1, 4})
              TEST_ASSERT_EQUAL(laneOn(p.patterns[1].steps[0][i], lane), laneOn(p.patterns[2].steps[0][i], lane));
            for (int lane : {2, 3, 5}) {
              mainTops += p.patterns[1].steps[0][i].hasNote() && laneOn(p.patterns[1].steps[0][i], lane);
              fullTops += p.patterns[2].steps[0][i].hasNote() && laneOn(p.patterns[2].steps[0][i], lane);
            }
          }
          if (fullTops >= 4 && (recipe.roles & 1))
            TEST_ASSERT_LESS_OR_EQUAL_MESSAGE((fullTops + 3) / 2, mainTops, recipe.style);
          const auto& outro = p.patterns[3]; const int cutoff = outro.length / 2;
          for (int role = 1; role < kDiceRoles; ++role) if (attacks(3, role)) {
            TEST_ASSERT_EQUAL_MESSAGE(0, attacks(3, role, cutoff), recipe.style);
            TEST_ASSERT_EQUAL_MESSAGE(kNoteOff, outro.steps[role][cutoff].note, recipe.style);
          }
          TEST_ASSERT_TRUE_MESSAGE(memcmp(p.patterns[1].steps, outro.steps, sizeof(outro.steps)) != 0, recipe.style);
          TEST_ASSERT_TRUE_MESSAGE(memcmp(p.patterns[2].steps, outro.steps, sizeof(outro.steps)) != 0, recipe.style);
        }
}
void test_develop_preserves_locks_and_scoped_rolls() {
  DiceOpts o; o.genre = 2; o.style = 0; o.variety = 30;
  diceRoll(p, o, 0, 777, snap); original = p;
  o.lockMask = 4; o.seed = 999; diceRoll(p, o, 0, 1, snap);
  for (int pat = 0; pat < 4; ++pat)
    TEST_ASSERT_EQUAL_MEMORY(original.patterns[pat].steps[2], p.patterns[pat].steps[2], sizeof(p.patterns[pat].steps[2]));
  diceUndo(p, snap); TEST_ASSERT_EQUAL_MEMORY(&original, &p, sizeof(p));
  p = original; o.lockMask = 0; o.targetMask = 8;
  diceRoll(p, o, 0, 1, snap);
  for (int pat = 0; pat < 4; ++pat) for (int role = 0; role < 6; ++role) if (role != 3)
    TEST_ASSERT_EQUAL_MEMORY(original.patterns[pat].steps[role], p.patterns[pat].steps[role], sizeof(p.patterns[pat].steps[role]));
}
void test_arrangement_edits_preserve_kept_events_and_stop_held_outro() {
  Step row[kMaxSteps], before[kMaxSteps];
  for (int i = 0; i < 16; i += 2) {
    row[i].note = 60 + i; row[i].vel = 80 + i;
    row[i].fx[0] = {Fx::GAT, gateValue(75)};
    row[i].fx[1] = {Fx::NDG, 7};
  }
  memcpy(before, row, sizeof(row));
  dice_detail::thinPhrase(row, 16, 4); int kept = 0;
  for (int i = 0; i < 16; ++i) if (row[i].hasNote()) {
    ++kept; TEST_ASSERT_EQUAL_MEMORY(&before[i], &row[i], sizeof(Step));
  }
  TEST_ASSERT_EQUAL(4, kept);
  row[0].fx[0] = {Fx::TIE, 0};
  const Step held = row[0];
  dice_detail::endPhrase(row, 16, 8);
  TEST_ASSERT_EQUAL_MEMORY(&held, &row[0], sizeof(Step));
  TEST_ASSERT_EQUAL(kNoteOff, row[8].note);
  for (int i = 9; i < kMaxSteps; ++i) TEST_ASSERT_TRUE(row[i].isEmpty());
}
void test_locked_short_eighth_sections_keep_gaps_filled_and_roles_exact() {
  const uint8_t lengths[] = {4, 7, 16, 32};
  for (int genre = 0; genre < diceGenreCount(); ++genre)
    for (int style = 0; style < diceStyleCount(genre); ++style) {
      templateBuild(0, p);
      for (int pat = 0; pat < 4; ++pat) { p.patterns[pat].length = lengths[pat]; p.patterns[pat].res = Resolution::Eighth; }
      original = p; DiceOpts o; o.genre = genre; o.style = style; o.lockMask = 32;
      diceRoll(p, o, 0, 777, snap);
      for (int pat = 0; pat < 4; ++pat) {
        TEST_ASSERT_EQUAL_MEMORY(original.patterns[pat].steps[5], p.patterns[pat].steps[5], sizeof(p.patterns[pat].steps[5]));
        int first = 6, last = -1;
        for (int role = 0; role < 6; ++role) if (attacks(pat, role)) { if (first == 6) first = role; last = role; }
        for (int role = first; role <= last; ++role) TEST_ASSERT_GREATER_THAN_MESSAGE(0, attacks(pat, role), diceStyleName(genre, style));
        for (int role = 0; role < 6; ++role) for (int i = lengths[pat]; i < kMaxSteps; ++i)
          TEST_ASSERT_TRUE(p.patterns[pat].steps[role][i].isEmpty());
      }
    }
}
struct LateMidi : DicePhraseSource {
  bool pick(int, DiceRole role, uint32_t, DicePhrase& phrase) const override {
    if (role != DiceRole::Chords) return false;
    phrase = DicePhrase(); phrase.bars = 2; phrase.count = 1;
    phrase.events[0] = {576, 384, 60, 89, 255};
    return true;
  }
};
void test_late_sustained_midi_keeps_one_source_event_then_releases_in_outro() {
  LateMidi source; DiceOpts o; o.genre = 7; o.style = 0; o.len = 16; o.variety = 0; o.groove = 0; o.root = 0; o.source = DiceSource::Midi;
  diceRoll(p, o, 0, 777, snap, 0, &source);
  TEST_ASSERT_GREATER_THAN(0, attacks(3, 2));
  TEST_ASSERT_TRUE(p.patterns[3].steps[2][0].hasNote());
  TEST_ASSERT_EQUAL(60, p.patterns[3].steps[2][0].note);
  TEST_ASSERT_EQUAL(89, p.patterns[3].steps[2][0].vel);
  TEST_ASSERT_EQUAL(kNoteOff, p.patterns[3].steps[2][8].note);
  TEST_ASSERT_EQUAL(0, attacks(3, 2, 8));
}
void test_classic_keeps_full_main_counterline_and_final_drop() {
  SectionMidi source; DiceOpts o; o.form = DiceForm::Classic; o.source = DiceSource::Midi;
  o.genre = 2; o.style = 0; o.variety = 0; o.len = 64; o.groove = 0;
  diceRoll(p, o, 0, 777, snap, 0, &source);
  TEST_ASSERT_EQUAL(attacks(0, 4), attacks(3, 4));
  TEST_ASSERT_GREATER_THAN(0, attacks(3, 1, 32));
  TEST_ASSERT_GREATER_THAN(0, attacks(3, 3, 32));
  TEST_ASSERT_NOT_EQUAL(kNoteOff, p.patterns[3].steps[1][32].note);
}
void test_sparse_removed_terminator_still_bounds_retained_tie() {
  Step row[kMaxSteps], mask[kMaxSteps];
  row[0].note = 60; row[0].fx[0] = {Fx::TIE, 0};
  row[16].note = 67;
  // The original step-8 attack terminated this tie. Arrangement and Sparse both removed it.
  mask[0].note = 60; mask[0].fx[0] = {Fx::GAT, gateValue(800)};
  mask[8].note = kNoteOff; mask[16] = row[16];
  dice_detail::retainAttacks(row, 32, mask);
  TEST_ASSERT_NULL(row[0].find(Fx::TIE));
  TEST_ASSERT_NOT_NULL(row[0].find(Fx::GAT));
  TEST_ASSERT_EQUAL(800, gatePercent(row[0].find(Fx::GAT)->val));
  TEST_ASSERT_EQUAL_MEMORY(&mask[16], &row[16], sizeof(Step));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sparse_removed_terminator_still_bounds_retained_tie);
  RUN_TEST(test_late_sustained_midi_keeps_one_source_event_then_releases_in_outro);
  RUN_TEST(test_classic_keeps_full_main_counterline_and_final_drop);
  RUN_TEST(test_arrangement_edits_preserve_kept_events_and_stop_held_outro);
  RUN_TEST(test_locked_short_eighth_sections_keep_gaps_filled_and_roles_exact);
  RUN_TEST(test_develop_main_counterline_and_outro_have_structural_contrast_in_all_styles);
  RUN_TEST(test_develop_preserves_locks_and_scoped_rolls);
  return UNITY_END();
}
