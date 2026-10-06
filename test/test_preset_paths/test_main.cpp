#include <string.h>
#include <unity.h>
#include "preset_paths.h"
#include "presets_factory.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_roots() {
  TEST_ASSERT_EQUAL_STRING("/presets/CHIP", presetRoot(InstrType::Chip));
  TEST_ASSERT_EQUAL_STRING("/presets/SAMPLE", presetRoot(InstrType::Sample));
  TEST_ASSERT_EQUAL_STRING("/presets/FM", presetRoot(InstrType::Fm));
  TEST_ASSERT_EQUAL_STRING("/presets/DRUM", presetRoot(InstrType::Drum));
  TEST_ASSERT_EQUAL_STRING("/presets/SYNTH", presetRoot(InstrType::Synth));
  TEST_ASSERT_EQUAL_STRING("SAMPLE", presetTypeName(InstrType::Sample));
  TEST_ASSERT_EQUAL_STRING("SYNTH", presetTypeName(InstrType::Synth));
}

void test_depth() {
  TEST_ASSERT_EQUAL(0, presetDepth("/presets/CHIP"));
  TEST_ASSERT_EQUAL(1, presetDepth("/presets/FM/BASS"));
  TEST_ASSERT_EQUAL(4, presetDepth("/presets/DRUM/a/b/c/d"));
  TEST_ASSERT_EQUAL(-1, presetDepth("/presets"));
  TEST_ASSERT_EQUAL(-1, presetDepth("/presets/CHIPX"));  // not the CHIP root
  TEST_ASSERT_EQUAL(-1, presetDepth("/samples/CHIP"));
  TEST_ASSERT_EQUAL(-1, presetDepth("/presets/OTHER/a"));
}

void test_sub_path() {
  TEST_ASSERT_EQUAL_STRING("", presetSubPath("/presets/SAMPLE"));
  TEST_ASSERT_EQUAL_STRING("/A/B", presetSubPath("/presets/SAMPLE/A/B"));
}

void test_join() {
  char out[32];
  TEST_ASSERT_TRUE(presetJoin(out, sizeof(out), "/presets/FM", "KICK", ".mti"));
  TEST_ASSERT_EQUAL_STRING("/presets/FM/KICK.mti", out);
  TEST_ASSERT_TRUE(presetJoin(out, sizeof(out), "/presets/FM", "SUB"));
  TEST_ASSERT_EQUAL_STRING("/presets/FM/SUB", out);
  char small[20];
  TEST_ASSERT_FALSE(presetJoin(small, sizeof(small), "/presets/FM", "KICK", ".mti"));  // needs 21
}

void test_up() {
  char dir[64] = "/presets/CHIP/A/BB";
  char left[16];
  TEST_ASSERT_TRUE(presetUp(dir, left, sizeof(left)));
  TEST_ASSERT_EQUAL_STRING("/presets/CHIP/A", dir);
  TEST_ASSERT_EQUAL_STRING("BB", left);
  TEST_ASSERT_TRUE(presetUp(dir));
  TEST_ASSERT_EQUAL_STRING("/presets/CHIP", dir);
  TEST_ASSERT_FALSE(presetUp(dir));
  TEST_ASSERT_EQUAL_STRING("/presets/CHIP", dir);
  char bad[16] = "/midi/x";
  TEST_ASSERT_FALSE(presetUp(bad));
  TEST_ASSERT_EQUAL_STRING("/midi/x", bad);
}

void test_categories() {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType type = static_cast<InstrType>(t);
    const int n = factoryCategoryCount(type);
    TEST_ASSERT_EQUAL(factoryCount(type) == 0, n == 0);
    // Unique, and every preset's category is listed.
    for (int a = 0; a < n; ++a)
      for (int b = a + 1; b < n; ++b) TEST_ASSERT_TRUE(strcmp(factoryCategory(type, a), factoryCategory(type, b)) != 0);
    for (int i = 0; i < factoryCount(type); ++i) {
      bool found = false;
      for (int a = 0; a < n && !found; ++a) found = strcmp(factoryCategory(type, a), factoryPreset(type, i).category) == 0;
      TEST_ASSERT_TRUE(found);
    }
    TEST_ASSERT_NULL(factoryCategory(type, n));
  }
  // Table order: the first preset's category comes first.
  TEST_ASSERT_EQUAL_STRING(factoryPreset(InstrType::Chip, 0).category, factoryCategory(InstrType::Chip, 0));
  TEST_ASSERT_EQUAL(0, factoryCategoryCount(InstrType::Sample));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roots);
  RUN_TEST(test_depth);
  RUN_TEST(test_sub_path);
  RUN_TEST(test_join);
  RUN_TEST(test_up);
  RUN_TEST(test_categories);
  return UNITY_END();
}
