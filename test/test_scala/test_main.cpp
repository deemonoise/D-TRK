#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "scala.h"
#include "tuning_factory.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

ParseErr parse(const char* s, SclTable& t, int* line = nullptr) {
  int l = -1;
  const ParseErr e = parseScl(s, static_cast<int>(strlen(s)), t, &l);
  if (line) *line = l;
  return e;
}

}  // namespace

void test_parses_cents_ratios_and_integers() {
  const char* s =
      "! meantone.scl\n"
      "!\n"
      "Test scale\n"
      " 5\n"
      "!\n"
      " 100.0\n"
      " 3/2   fifth\n"
      "-5.0\n"
      "5.\n"
      "2\n";
  SclTable t;
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse(s, t)));
  TEST_ASSERT_EQUAL_STRING("Test scale", t.desc);
  TEST_ASSERT_EQUAL(5, t.count);
  TEST_ASSERT_EQUAL(100000, t.mc[0]);
  TEST_ASSERT_EQUAL(701955, t.mc[1]);
  TEST_ASSERT_EQUAL(-5000, t.mc[2]);
  TEST_ASSERT_EQUAL(5000, t.mc[3]);
  TEST_ASSERT_EQUAL(1200000, t.mc[4]);
}

void test_line_endings_and_extras() {
  SclTable t;
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse("a\r\n2\r\n.5\r\n2/1\r\nextra junk\r\n", t)));
  TEST_ASSERT_EQUAL(2, t.count);
  TEST_ASSERT_EQUAL(500, t.mc[0]);
  TEST_ASSERT_EQUAL(1200000, t.mc[1]);
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse("a\r2\r150.\r1200.", t)));
  TEST_ASSERT_EQUAL(150000, t.mc[0]);
  // Empty description, count with a tail, tabs, no final newline.
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse("\n\t1 notes\n\t3/1", t)));
  TEST_ASSERT_EQUAL_STRING("", t.desc);
  TEST_ASSERT_EQUAL(1, t.count);
  TEST_ASSERT_EQUAL(1901955, t.mc[0]);
}

void test_description_cut() {
  SclTable t;
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok),
                    static_cast<int>(parse("0123456789012345678901234567890123456789\n1\n2/1\n", t)));
  TEST_ASSERT_EQUAL(kSclDescMax, strlen(t.desc));
}

void test_64_degrees_ok_65_too_many() {
  char buf[2048];
  int n = snprintf(buf, sizeof buf, "x\n64\n");
  for (int i = 1; i <= 64; ++i) n += snprintf(buf + n, sizeof buf - n, "%d.0\n", i * 18);
  SclTable t;
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse(buf, t)));
  TEST_ASSERT_EQUAL(64, t.count);
  int line = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::TooMany), static_cast<int>(parse("x\n65\n", t, &line)));
  TEST_ASSERT_EQUAL(2, line);
}

void test_errors_with_lines() {
  SclTable t;
  int line = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::NoCount), static_cast<int>(parse("", t, &line)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::NoCount), static_cast<int>(parse("! c\ndesc\n! only\n", t, &line)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadCount), static_cast<int>(parse("d\nabc\n", t, &line)));
  TEST_ASSERT_EQUAL(2, line);
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadCount), static_cast<int>(parse("d\n-3\n", t, &line)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Empty), static_cast<int>(parse("d\n0\n", t, &line)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n2\n100.0\nabc\n", t, &line)));
  TEST_ASSERT_EQUAL(4, line);
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n3/0\n", t, &line)));
  TEST_ASSERT_EQUAL(3, line);
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n0/2\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n-3/2\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n99999999999/1\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n3/\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n.\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n1.2.3\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadValue), static_cast<int>(parse("d\n1\n99999.0\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Truncated), static_cast<int>(parse("d\n3\n100.0\n! c\n200.0\n", t, &line)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadPeriod), static_cast<int>(parse("d\n2\n100.0\n0.0\n", t, &line)));
  TEST_ASSERT_EQUAL(4, line);
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadPeriod), static_cast<int>(parse("d\n1\n-1200.0\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::BadPeriod), static_cast<int>(parse("d\n1\n32/1\n", t)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse("d\n1\n16/1\n", t)));
  // A comment line between the values is skipped, not counted.
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse("d\n2\n100.0\n!x\n2/1\n", t)));
  TEST_ASSERT_TRUE(strlen(sclErrName(ParseErr::TooMany)) > 0);
}

void test_scl_to_tuning() {
  SclTable s;
  TEST_ASSERT_EQUAL(static_cast<int>(ParseErr::Ok), static_cast<int>(parse("d\n2\n600.\n2/1\n", s)));
  TuningTable t;
  t.root = 50;
  sclToTuning(s, "MY_TRITONE_SCALE_LONG", t);
  TEST_ASSERT_EQUAL(2, t.count);
  TEST_ASSERT_EQUAL(50, t.root);  // kept
  TEST_ASSERT_EQUAL_STRING("MY_TRITONE_SCALE", t.name);
  TEST_ASSERT_EQUAL(600000, t.mc[0]);
  TEST_ASSERT_EQUAL(1200000, t.mc[1]);
  TEST_ASSERT_EQUAL(0, t.mc[2]);
  TEST_ASSERT_TRUE(tuningValid(t));
}

void test_factory_tunings() {
  TEST_ASSERT_EQUAL(10, factoryTuningCount());
  for (int i = 0; i < factoryTuningCount(); ++i) {
    TuningTable t;
    TEST_ASSERT_TRUE_MESSAGE(factoryTuning(i, t), factoryTuningName(i));
    TEST_ASSERT_TRUE_MESSAGE(tuningValid(t), factoryTuningName(i));
    TEST_ASSERT_EQUAL_STRING(factoryTuningName(i), t.name);
    TEST_ASSERT_TRUE(strlen(t.name) <= 16);
  }
  TuningTable t;
  TEST_ASSERT_FALSE(factoryTuning(10, t));
  TEST_ASSERT_FALSE(factoryTuning(-1, t));
  TEST_ASSERT_EQUAL(-1, factoryTuningFind("NOPE"));
  int i = factoryTuningFind("JUST 5-LIMIT");
  TEST_ASSERT_TRUE(i >= 0 && factoryTuning(i, t));
  TEST_ASSERT_EQUAL(12, t.count);
  TEST_ASSERT_EQUAL(386314, t.mc[3]);  // 5/4
  TEST_ASSERT_EQUAL(1200000, t.mc[11]);
  i = factoryTuningFind("31-EDO");
  TEST_ASSERT_TRUE(factoryTuning(i, t));
  TEST_ASSERT_EQUAL(31, t.count);
  TEST_ASSERT_INT_WITHIN(1, 38710, t.mc[0]);
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind("BOHLEN-PIERCE"), t));
  TEST_ASSERT_EQUAL(13, t.count);
  TEST_ASSERT_EQUAL(1901955, t.mc[12]);
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind("PYTHAGOREAN"), t));
  TEST_ASSERT_EQUAL(701955, t.mc[6]);
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind("SLENDRO"), t));
  TEST_ASSERT_EQUAL(5, t.count);
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind("PELOG"), t));
  TEST_ASSERT_EQUAL(7, t.count);
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind("MEANTONE 1/4"), t));
  TEST_ASSERT_INT_WITHIN(2, 696578, t.mc[6]);  // the meantone fifth
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind("24-EDO"), t));
  TEST_ASSERT_EQUAL(50000, t.mc[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_cents_ratios_and_integers);
  RUN_TEST(test_line_endings_and_extras);
  RUN_TEST(test_description_cut);
  RUN_TEST(test_64_degrees_ok_65_too_many);
  RUN_TEST(test_errors_with_lines);
  RUN_TEST(test_scl_to_tuning);
  RUN_TEST(test_factory_tunings);
  return UNITY_END();
}
