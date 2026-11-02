#include <unity.h>
#include <cstring>
#include <initializer_list>
#include "dice.h"
#include "templates.h"
using namespace mt;
static Project p, initial, expected;
static DiceSnap snap, fresh;
void setUp() { templateBuild(0, p); }
void tearDown() {}
static DiceOpts arrange() {
  DiceOpts options; options.form = DiceForm::Arrange; options.variety = 0; options.root = 0; options.groove = 0;
  options.len = 32; options.layers |= kDiceSong;
  return options;
}
void test_pattern_count_eligibility_and_bank_boundaries() {
  auto options = arrange();
  TEST_ASSERT_EQUAL(4, kDicePats); TEST_ASSERT_EQUAL(6, kDiceMaxPats);
  TEST_ASSERT_EQUAL(6, dicePatternCount(options));
  TEST_ASSERT_EQUAL(0, diceFirstPattern(-1, 6));
  TEST_ASSERT_EQUAL(0, diceFirstPattern(0, 6));
  TEST_ASSERT_EQUAL(0, diceFirstPattern(10, 6));
  TEST_ASSERT_EQUAL(0, diceFirstPattern(15, 6));
  TEST_ASSERT_EQUAL(0, diceFirstPattern(15));
  options.targetMask = 8; TEST_ASSERT_EQUAL(4, dicePatternCount(options));
  options = arrange(); options.change = DiceChange::Normal; TEST_ASSERT_EQUAL(4, dicePatternCount(options));
  options = arrange(); options.layers = kDiceSounds; TEST_ASSERT_EQUAL(4, dicePatternCount(options));
  options = arrange(); options.form = DiceForm::Develop; TEST_ASSERT_EQUAL(4, dicePatternCount(options));
}
void test_overwrite_range_includes_only_the_requested_sections() {
  for (auto& pattern : p.patterns) pattern.clear();
  p.patterns[5].steps[0][0].note = 100;
  p.patterns[15].steps[0][0].note = 100;
  TEST_ASSERT_EQUAL(0, diceOverwrites(p, 0));
  TEST_ASSERT_EQUAL(1u << 5, diceOverwrites(p, 0, 6));
  TEST_ASSERT_EQUAL(1u << 5, diceOverwrites(p, 15, 6));
  TEST_ASSERT_EQUAL(0, diceOverwrites(p, 15));
}
void test_arrange_apply_undo_preserves_outside_patterns_and_tracks() {
  for (int edit : {0, 10, 15}) {
    templateBuild(0, p);
    for (int pattern = 0; pattern < kPatterns; ++pattern) {
      p.patterns[pattern].steps[8][0].note = 50 + pattern;
      p.patterns[pattern].steps[0][1].note = 90 + pattern;
    }
    initial = p; auto options = arrange();
    const int first = diceFirstPattern(edit, 6);
    const auto result = diceRoll(p, options, edit, 777, snap);
    TEST_ASSERT_EQUAL(first, result.first); TEST_ASSERT_EQUAL(6, snap.patCount);
    TEST_ASSERT_TRUE(memcmp(&initial, &p, sizeof(p)) != 0);
    for (int pattern = 0; pattern < kPatterns; ++pattern) {
      if (pattern < first || pattern >= first + 6)
        TEST_ASSERT_EQUAL_MEMORY(&initial.patterns[pattern], &p.patterns[pattern], sizeof(Pattern));
      else for (int role = 6; role < kTracks; ++role)
        TEST_ASSERT_EQUAL_MEMORY(initial.patterns[pattern].steps[role], p.patterns[pattern].steps[role], sizeof(p.patterns[pattern].steps[role]));
    }
    for (int track = 6; track < kTracks; ++track)
      TEST_ASSERT_EQUAL_MEMORY(&initial.tracks[track], &p.tracks[track], sizeof(TrackCfg));
    diceUndo(p, snap); TEST_ASSERT_EQUAL_MEMORY(&initial, &p, sizeof(p));
  }
}
void test_failed_midi_preflight_preserves_six_section_undo() {
  initial = p; auto options = arrange(); diceRoll(p, options, 15, 777, snap); expected = p;
  options.source = DiceSource::Midi; options.form = DiceForm::Classic;
  const auto failed = diceRoll(p, options, 0, 123, snap, 0, nullptr);
  TEST_ASSERT_EQUAL(0, failed.written); TEST_ASSERT_EQUAL(6, snap.patCount);
  TEST_ASSERT_EQUAL_MEMORY(&expected, &p, sizeof(p));
  diceUndo(p, snap); TEST_ASSERT_EQUAL_MEMORY(&initial, &p, sizeof(p));
}
void test_reused_arrange_snapshot_does_not_leak_inactive_sections_into_old_forms() {
  auto options = arrange(); diceRoll(p, options, 15, 777, snap);
  for (DiceForm form : {DiceForm::Classic, DiceForm::Develop}) {
    // Deliberately hostile inactive capacity: no legacy register, cap or sound-fit pass may read it.
    for (int index = kDicePats; index < kDiceMaxPats; ++index) {
      auto& unused = snap.parts.pats[index]; unused.length = kMaxSteps; unused.res = Resolution::Sixteenth;
      for (auto& row : unused.steps) for (auto& note : row) {
        note = Step(); note.note = 127; note.vel = 127; note.fx[0] = {Fx::GAT, gateValue(800)};
      }
    }
    templateBuild(0, p); initial = p;
    DiceOpts old; old.form = form; old.seed = 8765;
    diceRoll(p, old, 0, 1, snap); expected = p;
    TEST_ASSERT_EQUAL(4, snap.patCount);
    p = initial; diceRoll(p, old, 0, 1, fresh);
    TEST_ASSERT_EQUAL_MEMORY(&expected, &p, sizeof(p));
    diceUndo(p, fresh); TEST_ASSERT_EQUAL_MEMORY(&initial, &p, sizeof(p));
  }
}
void test_ineligible_arrange_requests_match_develop_fallback() {
  const auto start = arrange();
  for (int variant : {0, 1, 2}) {
    auto options = start;
    if (variant == 0) options.targetMask = 8;
    else if (variant == 1) options.change = DiceChange::Normal;
    else options.layers = kDiceSounds;
    templateBuild(0, p); initial = p; diceRoll(p, options, 15, 777, snap); expected = p;
    TEST_ASSERT_EQUAL(4, snap.patCount);
    p = initial; options.form = DiceForm::Develop; diceRoll(p, options, 15, 777, fresh);
    TEST_ASSERT_EQUAL_MEMORY(&expected, &p, sizeof(p));
  }
}
void test_arrange_locks_keep_all_six_destination_headers_and_rows() {
  for (int pattern = 0; pattern < kPatterns; ++pattern) {
    p.patterns[pattern].length = 4 + pattern;
    p.patterns[pattern].res = pattern & 1 ? Resolution::Eighth : Resolution::Sixteenth;
    p.patterns[pattern].steps[2][0].note = 60;
  }
  initial = p; auto options = arrange(); options.lockMask = 4;
  diceRoll(p, options, 15, 777, snap);
  for (int pattern = 0; pattern < 6; ++pattern) {
    TEST_ASSERT_EQUAL(initial.patterns[pattern].length, p.patterns[pattern].length);
    TEST_ASSERT_EQUAL(initial.patterns[pattern].res, p.patterns[pattern].res);
    TEST_ASSERT_EQUAL_MEMORY(initial.patterns[pattern].steps[2], p.patterns[pattern].steps[2], sizeof(p.patterns[pattern].steps[2]));
  }
  diceUndo(p, snap); TEST_ASSERT_EQUAL_MEMORY(&initial, &p, sizeof(p));
}
void test_selected_pattern_never_changes_the_written_project_or_header_basis() {
  for (DiceForm form : {DiceForm::Classic, DiceForm::Develop, DiceForm::Arrange})
    for (uint8_t locks : {uint8_t(0), uint8_t(4)}) for (uint8_t target : {uint8_t(0x3f), uint8_t(8)}) {
      templateBuild(0, p);
      for (int pattern = 0; pattern < kPatterns; ++pattern) {
        p.patterns[pattern].length = 16 + pattern;
        p.patterns[pattern].res = pattern & 1 ? Resolution::Eighth : Resolution::Sixteenth;
        p.patterns[pattern].steps[2][0].note = 60;
      }
      initial = p; auto options = arrange(); options.form = form; options.lockMask = locks; options.targetMask = target;
      diceRoll(p, options, 0, 777, snap); expected = p;
      for (int edit : {7, 15}) {
        p = initial; const auto result = diceRoll(p, options, edit, 777, fresh);
        TEST_ASSERT_EQUAL(0, result.first);
        TEST_ASSERT_EQUAL_MEMORY(&expected, &p, sizeof(p));
      }
    }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_selected_pattern_never_changes_the_written_project_or_header_basis);
  RUN_TEST(test_pattern_count_eligibility_and_bank_boundaries);
  RUN_TEST(test_overwrite_range_includes_only_the_requested_sections);
  RUN_TEST(test_arrange_apply_undo_preserves_outside_patterns_and_tracks);
  RUN_TEST(test_failed_midi_preflight_preserves_six_section_undo);
  RUN_TEST(test_reused_arrange_snapshot_does_not_leak_inactive_sections_into_old_forms);
  RUN_TEST(test_ineligible_arrange_requests_match_develop_fallback);
  RUN_TEST(test_arrange_locks_keep_all_six_destination_headers_and_rows);
  return UNITY_END();
}
