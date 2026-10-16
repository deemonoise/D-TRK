#include <math.h>
#include <string.h>
#include <unity.h>
#include "tuning.h"
#include "tuning_factory.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

TuningTable edo(int n) {
  TuningTable t;
  t.count = static_cast<uint8_t>(n);
  for (int k = 1; k <= n; ++k) t.mc[k - 1] = static_cast<int32_t>(1200000LL * k / n);
  strcpy(t.name, "EDO");
  return t;
}

TuningTable factory(const char* name) {
  TuningTable t;
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind(name), t));
  return t;
}

}  // namespace

void test_off_is_identity() {
  TuneMap m;
  TEST_ASSERT_FALSE(m.on());
  TEST_ASSERT_EQUAL_FLOAT(61.25f, m.map(61.25f));
  TuningTable t;  // 12-TET
  m.build(t, 0);
  TEST_ASSERT_FALSE(m.on());
  TEST_ASSERT_EQUAL_FLOAT(-3.5f, m.map(-3.5f));
  t.count = 2;  // invalid: period 0
  m.build(t, 0);
  TEST_ASSERT_FALSE(m.on());
}

void test_24_edo_at_c4() {
  TuneMap m;
  TuningTable t = edo(24);
  m.build(t, 0);  // AUTO, scale root C: key 60
  TEST_ASSERT_TRUE(m.on());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.f, m.map(60.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.5f, m.map(61.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 72.f, m.map(84.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 48.f, m.map(36.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 59.5f, m.map(59.f));  // negative j
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.25f, m.map(60.5f));  // between keys: halfway
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 30.f, m.map(0.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 93.5f, m.map(127.f));
  // Outside 0..127: by the period.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.f, m.map(-20.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 105.f, m.map(150.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 104.75f, m.map(149.5f));
}

void test_just_root_follows_scale_root() {
  TuneMap m;
  TuningTable t = factory("JUST 5-LIMIT");
  m.build(t, 2);  // AUTO with scale root D: key 62 keeps its pitch
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 62.f, m.map(62.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 62.f + 3.86314f, m.map(66.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 62.f - 12.f + 7.01955f, m.map(57.f));
  t.root = 60;  // explicit
  m.build(t, 2);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.f + 3.86314f, m.map(64.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 72.f, m.map(72.f));
}

void test_short_and_long_periods() {
  TuneMap m;
  TuningTable t = factory("SLENDRO");
  m.build(t, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 62.31f, m.map(61.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 72.f, m.map(65.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 48.f + 9.55f, m.map(59.f));
  t = factory("BOHLEN-PIERCE");
  m.build(t, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.f + 19.01955f, m.map(73.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.f - 19.01955f, m.map(47.f));
}

void test_rebuild_to_12tet_is_identity() {
  TuneMap m;
  TuningTable t = edo(31);
  m.build(t, 0);
  TEST_ASSERT_TRUE(m.on());
  tuningClear(t);
  m.build(t, 0);
  TEST_ASSERT_FALSE(m.on());
  TEST_ASSERT_EQUAL_FLOAT(64.f, m.map(64.f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_off_is_identity);
  RUN_TEST(test_24_edo_at_c4);
  RUN_TEST(test_just_root_follows_scale_root);
  RUN_TEST(test_short_and_long_periods);
  RUN_TEST(test_rebuild_to_12tet_is_identity);
  return UNITY_END();
}
