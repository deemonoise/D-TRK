#include <unity.h>
#include <math.h>
#include "battery_gauge.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_table_points() {
  TEST_ASSERT_EQUAL_INT(0, lipoPercent(3.30f));
  TEST_ASSERT_EQUAL_INT(5, lipoPercent(3.50f));
  TEST_ASSERT_EQUAL_INT(10, lipoPercent(3.61f));
  TEST_ASSERT_EQUAL_INT(50, lipoPercent(3.80f));
  TEST_ASSERT_EQUAL_INT(90, lipoPercent(4.06f));
  TEST_ASSERT_EQUAL_INT(100, lipoPercent(4.18f));
}

void test_interpolation() {
  TEST_ASSERT_EQUAL_INT(45, lipoPercent(3.785f));  // halfway 3.77..3.80
  TEST_ASSERT_EQUAL_INT(95, lipoPercent(4.12f));   // halfway 4.06..4.18
  TEST_ASSERT_EQUAL_INT(65, lipoPercent(3.87f));   // halfway 3.84..3.90
}

void test_clamping() {
  TEST_ASSERT_EQUAL_INT(0, lipoPercent(3.0f));
  TEST_ASSERT_EQUAL_INT(0, lipoPercent(-1.f));
  TEST_ASSERT_EQUAL_INT(100, lipoPercent(4.30f));
}

void test_percent_before_update() {
  BatteryGauge g;
  TEST_ASSERT_EQUAL_INT(-1, g.percent());
  TEST_ASSERT_FALSE(g.charging());
  TEST_ASSERT_FALSE(g.takeLowAlert());
}

void test_ir_compensation() {
  BatteryGauge g;
  g.update(3.70f, 0.3f, 1.f);  // 3.70 + 0.3 * 0.15 = 3.745 V
  TEST_ASSERT_EQUAL_INT(lipoPercent(3.745f), g.percent());
  TEST_ASSERT_EQUAL_INT(32, g.percent());  // 30 + 10 * 0.005 / 0.03
}

void test_first_sample_sets_value() {
  BatteryGauge g;
  g.update(3.80f, 0.f, 1.f);
  TEST_ASSERT_EQUAL_INT(50, g.percent());
}

void test_smoothing_partial_step() {
  // Charging so the shown value may move up. Charging current lowers OCV: feed volts that undo it.
  const float comp = 0.5f * BatteryGauge::kRint;
  BatteryGauge g;
  g.update(3.77f + comp, -0.5f, 1.f);
  TEST_ASSERT_EQUAL_INT(40, g.percent());
  // A 30 s step on a 30 s tau moves halfway: 3.77 -> (3.77 + 3.83) / 2 = 3.80 V = 50 %.
  g.update(3.83f + comp, -0.5f, 30.f);
  TEST_ASSERT_EQUAL_INT(50, g.percent());
}

void test_no_rise_while_discharging() {
  BatteryGauge g;
  g.update(3.80f, 0.2f, 1.f);
  int p = g.percent();
  for (int i = 0; i < 200; ++i) g.update(3.70f, 0.2f, 1.f);
  int low = g.percent();
  TEST_ASSERT_TRUE(low < p);
  // Load drops: voltage recovers but the shown percent must not rise.
  for (int i = 0; i < 200; ++i) g.update(3.85f, 0.2f, 1.f);
  TEST_ASSERT_EQUAL_INT(low, g.percent());
  TEST_ASSERT_FALSE(g.charging());
}

void test_rises_while_charging() {
  BatteryGauge g;
  g.update(3.70f, -0.5f, 1.f);
  TEST_ASSERT_TRUE(g.charging());
  int p = g.percent();
  for (int i = 0; i < 200; ++i) g.update(4.00f, -0.5f, 1.f);
  TEST_ASSERT_TRUE(g.percent() > p);
}

void test_charging_detection_uses_current_ema() {
  BatteryGauge g;
  for (int i = 0; i < 100; ++i) g.update(3.80f, 0.3f, 1.f);
  TEST_ASSERT_FALSE(g.charging());
  g.update(3.80f, -0.5f, 0.5f);  // one short negative blip
  TEST_ASSERT_FALSE(g.charging());
  for (int i = 0; i < 60; ++i) g.update(3.80f, -0.5f, 1.f);
  TEST_ASSERT_TRUE(g.charging());
  for (int i = 0; i < 60; ++i) g.update(3.80f, 0.3f, 1.f);
  TEST_ASSERT_FALSE(g.charging());
}

void test_low_alert_once() {
  BatteryGauge g;
  g.update(3.65f, 0.f, 1.f);  // 15 %
  TEST_ASSERT_FALSE(g.takeLowAlert());
  for (int i = 0; i < 300; ++i) g.update(3.55f, 0.f, 1.f);
  TEST_ASSERT_TRUE(g.percent() <= BatteryGauge::kLowPct);
  TEST_ASSERT_TRUE(g.takeLowAlert());
  TEST_ASSERT_FALSE(g.takeLowAlert());  // cleared
  for (int i = 0; i < 100; ++i) g.update(3.50f, 0.f, 1.f);
  TEST_ASSERT_FALSE(g.takeLowAlert());  // still low: no repeat
}

void test_low_alert_not_while_charging() {
  BatteryGauge g;
  g.update(3.40f, -0.5f, 1.f);
  TEST_ASSERT_TRUE(g.charging());
  TEST_ASSERT_TRUE(g.percent() <= BatteryGauge::kLowPct);
  TEST_ASSERT_FALSE(g.takeLowAlert());
}

void test_low_alert_rearms_after_charge() {
  BatteryGauge g;
  for (int i = 0; i < 61; ++i) g.update(3.50f, 0.f, 1.f);  // 5 %: fires after warm-up
  TEST_ASSERT_TRUE(g.takeLowAlert());
  // Charge to 3.69 V OCV (20 %) and go back to discharge.
  for (int i = 0; i < 300; ++i) g.update(3.69f + 0.5f * BatteryGauge::kRint, -0.5f, 1.f);
  TEST_ASSERT_TRUE(g.percent() > BatteryGauge::kRearmPct);
  TEST_ASSERT_FALSE(g.takeLowAlert());
  for (int i = 0; i < 300; ++i) g.update(3.50f, 0.f, 1.f);
  TEST_ASSERT_TRUE(g.takeLowAlert());
}

void test_low_first_sample_recovers_in_warmup() {
  BatteryGauge g;
  g.update(3.40f, 1.0f, 1.f);  // boot surge: OCV reads 3.55 V, ~7 %
  int p0 = g.percent();
  TEST_ASSERT_TRUE(p0 <= BatteryGauge::kLowPct);
  for (int i = 0; i < 59; ++i) g.update(3.80f, 0.f, 1.f);  // still in warm-up
  TEST_ASSERT_TRUE(g.percent() > p0 + 20);
  TEST_ASSERT_FALSE(g.takeLowAlert());
}

void test_no_alert_during_warmup() {
  BatteryGauge g;
  g.update(3.50f, 0.f, 1.f);  // 5 %
  for (int i = 0; i < 58; ++i) g.update(3.50f, 0.f, 1.f);
  TEST_ASSERT_FALSE(g.takeLowAlert());
  for (int i = 0; i < 3; ++i) g.update(3.50f, 0.f, 1.f);  // past 60 s
  TEST_ASSERT_TRUE(g.takeLowAlert());
}

void test_nan_sample_ignored() {
  BatteryGauge g;
  g.update(NAN, 0.f, 1.f);
  TEST_ASSERT_EQUAL_INT(-1, g.percent());
  g.update(3.80f, 0.f, 1.f);
  g.update(NAN, 0.f, 1.f);
  g.update(3.80f, NAN, 1.f);
  g.update(3.80f, 0.f, INFINITY);
  g.update(INFINITY, 0.f, 1.f);
  TEST_ASSERT_EQUAL_INT(50, g.percent());
  TEST_ASSERT_FALSE(g.charging());
  TEST_ASSERT_EQUAL_INT(0, lipoPercent(NAN));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_table_points);
  RUN_TEST(test_interpolation);
  RUN_TEST(test_clamping);
  RUN_TEST(test_percent_before_update);
  RUN_TEST(test_ir_compensation);
  RUN_TEST(test_first_sample_sets_value);
  RUN_TEST(test_smoothing_partial_step);
  RUN_TEST(test_no_rise_while_discharging);
  RUN_TEST(test_rises_while_charging);
  RUN_TEST(test_charging_detection_uses_current_ema);
  RUN_TEST(test_low_alert_once);
  RUN_TEST(test_low_alert_not_while_charging);
  RUN_TEST(test_low_alert_rearms_after_charge);
  RUN_TEST(test_low_first_sample_recovers_in_warmup);
  RUN_TEST(test_no_alert_during_warmup);
  RUN_TEST(test_nan_sample_ignored);
  return UNITY_END();
}
