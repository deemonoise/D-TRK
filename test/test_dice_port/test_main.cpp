#include <unity.h>
#include "dice.h"
#include "presets_factory.h"
#include <string.h>
#include <stdio.h>
using namespace mt;
void setUp() {}
void tearDown() {}
void test_six_roles_available() { TEST_ASSERT_EQUAL(6, kDiceRoles); }
void test_expanded_compatible_preset_library() {
  int total = 0;
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) total += factoryCount(static_cast<InstrType>(t));
  TEST_ASSERT_GREATER_OR_EQUAL(500, total);
}
bool presetExists(InstrType type, const char* name) {
  for (int i = 0; i < factoryCount(type); ++i)
    if (!strcmp(factoryPreset(type, i).name, name)) return true;
  return false;
}
void test_all_recipe_sounds_are_available() {
  for (int genre = 0; genre < diceGenreCount(); ++genre) {
    for (int style = 0; style < diceStyleCount(genre); ++style) {
      const DiceRecipe& recipe = *diceRecipe(genre, style);
      for (int role = 1; role < kDiceRoles; ++role) {
        const DiceSound* sound = recipe.sounds[role];
        for (; sound && sound->name; ++sound)
          TEST_ASSERT_TRUE_MESSAGE(presetExists(sound->type, sound->name), sound->name);
      }
      for (int lane = 0; recipe.kit && lane < kDiceLanes; ++lane) {
        const DiceDrum* drum = recipe.kit->lane[lane];
        for (; drum && drum->name; ++drum)
          TEST_ASSERT_TRUE_MESSAGE(presetExists(drum->type, drum->name), drum->name);
      }
    }
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_six_roles_available);
  RUN_TEST(test_expanded_compatible_preset_library);
  RUN_TEST(test_all_recipe_sounds_are_available);
  return UNITY_END();
}
