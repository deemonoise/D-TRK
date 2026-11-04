#include <cstring>
#include <initializer_list>
#include <unity.h>
#include "dice.h"
#include "templates.h"

using namespace mt;

static Project project;
static DiceSnap snap;

void setUp() { templateBuild(0, project); }
void tearDown() {}

void test_canonical_bass_uses_the_style_voice() {
  struct Canon { int genre, style; const char* name; };
  const Canon cases[] = {{2, 0, "TRANCEBS"}, {2, 1, "PSYBASS"}, {2, 2, "PSYBASS"},
                         {2, 3, "GOABASS"}, {2, 4, "TRANCEBS"}, {3, 1, "TONEBASS"}, {3, 2, "NEURO"}};
  for (const auto& c : cases) {
    templateBuild(0, project);
    DiceOpts options;
    options.genre = c.genre;
    options.style = c.style;
    options.variety = 0;
    options.root = 0;
    diceRoll(project, options, 0, 777, snap);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.name, project.instruments[project.tracks[1].instr].name,
                                     diceStyleName(c.genre, c.style));
  }
}

void test_aggressive_dnb_mix_retains_the_bass_character() {
  const DiceTone& neuro = diceRecipe(3, 2)->mix->role[1];
  const DiceTone& jump = diceRecipe(3, 1)->mix->role[1];
  for (const DiceTone* tone : {&neuro, &jump}) {
    TEST_ASSERT_EQUAL(kDiceKeep, tone->cut);
    TEST_ASSERT_EQUAL(kDiceKeep, tone->res);
    TEST_ASSERT_EQUAL(kDiceKeep, tone->fenv);
    TEST_ASSERT_GREATER_OR_EQUAL(60, tone->drv);
  }
  const DiceTone& liquid = diceRecipe(3, 0)->mix->role[1];
  TEST_ASSERT_EQUAL(74, liquid.cut);
  TEST_ASSERT_EQUAL(25, liquid.res);
  TEST_ASSERT_EQUAL(16, liquid.fenv);
}

void test_profiles_cover_every_style_with_physical_bar_budgets() {
  int styles = 0;
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      const DiceMusicProfile& profile = diceMusicProfile(g, s);
      TEST_ASSERT_LESS_OR_EQUAL(16, profile.leadPerBar);
      TEST_ASSERT_LESS_OR_EQUAL(16, profile.arpPerBar);
      TEST_ASSERT_LESS_OR_EQUAL(16, profile.bassPerBar);
      TEST_ASSERT_LESS_OR_EQUAL(profile.arpHigh, profile.arpLow);
      TEST_ASSERT_LESS_OR_EQUAL(127, profile.arpHigh);
      ++styles;
    }
  TEST_ASSERT_EQUAL(39, styles);
  const auto& invalid = diceMusicProfile(-1, 0);
  TEST_ASSERT_EQUAL(0, invalid.leadPerBar);
  TEST_ASSERT_EQUAL(127, invalid.arpHigh);
  TEST_ASSERT_EQUAL_PTR(&invalid, &diceMusicProfile(0, 99));
}

void test_bass_and_foreground_policies_distinguish_the_styles() {
  TEST_ASSERT_EQUAL(DiceBassMotion::Pulse, diceMusicProfile(2, 1).bassMotion);
  TEST_ASSERT_EQUAL(12, diceMusicProfile(2, 1).bassPerBar);
  TEST_ASSERT_EQUAL(DiceBassMotion::Hold, diceMusicProfile(3, 2).bassMotion);
  TEST_ASSERT_EQUAL(DiceBassMotion::Hold, diceMusicProfile(7, 2).bassMotion);
  TEST_ASSERT_EQUAL(DiceBassMotion::Acid, diceMusicProfile(0, 2).bassMotion);
  TEST_ASSERT_TRUE(diceMusicProfile(1, 4).acidArp);
  TEST_ASSERT_EQUAL(36, diceMusicProfile(1, 4).arpLow);
  TEST_ASSERT_EQUAL(60, diceMusicProfile(1, 4).arpHigh);
  TEST_ASSERT_EQUAL(2, diceMusicProfile(3, 2).leadPerBar);
  TEST_ASSERT_EQUAL(2, diceMusicProfile(3, 2).arpPerBar);
  TEST_ASSERT_EQUAL(8, diceMusicProfile(0, 4).leadPerBar);
}

void test_protected_styles_keep_unrestricted_native_foregrounds() {
  for (const auto& c : {std::initializer_list<int>{1, 0}, {1, 1}, {3, 0}, {6, 0}, {6, 2}}) {
    auto i = c.begin();
    const int genre = *i++, style = *i;
    const auto& profile = diceMusicProfile(genre, style);
    TEST_ASSERT_EQUAL(0, profile.leadPerBar);
    TEST_ASSERT_EQUAL(0, profile.arpPerBar);
    TEST_ASSERT_FALSE(profile.acidArp);
  }
}

void test_new_subgenres_append_distinct_recipes_and_profiles() {
  struct Style { int genre, style, bpm; const char* name; DiceBassMotion motion; int bass; };
  const Style additions[] = {{1, 6, 128, "EBM", DiceBassMotion::Pulse, 16},
                             {4, 3, 140, "DEEP DUB", DiceBassMotion::Hold, 2},
                             {6, 3, 160, "FOOTWORK", DiceBassMotion::Pulse, 6}};
  for (const auto& addition : additions) {
    const auto* recipe = diceRecipe(addition.genre, addition.style);
    TEST_ASSERT_NOT_NULL(recipe);
    TEST_ASSERT_EQUAL_STRING(addition.name, recipe->style);
    TEST_ASSERT_EQUAL(addition.bpm, recipe->bpm);
    const auto& profile = diceMusicProfile(addition.genre, addition.style);
    TEST_ASSERT_EQUAL(addition.motion, profile.bassMotion);
    TEST_ASSERT_EQUAL(addition.bass, profile.bassPerBar);
    TEST_ASSERT_LESS_OR_EQUAL(4, profile.leadPerBar);
    TEST_ASSERT_LESS_OR_EQUAL(8, profile.arpPerBar);
  }
}

void test_new_subgenres_choose_appropriate_canonical_bass_voices() {
  struct Canon { int genre, style; const char* name; };
  const Canon additions[] = {{1, 6, "DARKBASS"}, {4, 3, "DEEPSUB"}, {6, 3, "808 GLD"}};
  for (const auto& addition : additions) {
    templateBuild(0, project);
    DiceOpts options;
    options.genre = addition.genre;
    options.style = addition.style;
    options.variety = 0;
    options.root = 0;
    diceRoll(project, options, 0, 777, snap);
    TEST_ASSERT_EQUAL_STRING(addition.name, project.instruments[project.tracks[1].instr].name);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_canonical_bass_uses_the_style_voice);
  RUN_TEST(test_aggressive_dnb_mix_retains_the_bass_character);
  RUN_TEST(test_profiles_cover_every_style_with_physical_bar_budgets);
  RUN_TEST(test_bass_and_foreground_policies_distinguish_the_styles);
  RUN_TEST(test_protected_styles_keep_unrestricted_native_foregrounds);
  RUN_TEST(test_new_subgenres_append_distinct_recipes_and_profiles);
  RUN_TEST(test_new_subgenres_choose_appropriate_canonical_bass_voices);
  return UNITY_END();
}
