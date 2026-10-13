#include <string.h>
#include <unity.h>
#include "gen_util.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_token_round_trip() {
  const char* text = "Xs~^:100:50:-3";
  const char* p = text;
  ArpStep a;
  char mods[8];
  TEST_ASSERT_TRUE(parseGenToken(p, a, mods, sizeof(mods)));
  TEST_ASSERT_EQUAL_PTR(text + strlen(text), p);
  TEST_ASSERT_EQUAL(ArpKind::Note, a.kind);
  TEST_ASSERT_EQUAL(ArpAcc::Accent, a.acc);
  TEST_ASSERT_EQUAL(ArpLen::Short, a.len);
  TEST_ASSERT_TRUE(a.slide);
  TEST_ASSERT_EQUAL_STRING("^", mods);
  TEST_ASSERT_EQUAL(100, a.velPct);
  TEST_ASSERT_EQUAL(50, a.gatePct);
  TEST_ASSERT_EQUAL(-3, a.nudge);
  char out[32];
  int n = 0;
  TEST_ASSERT_TRUE(formatGenToken(a, mods, out, sizeof(out), n));
  out[n] = '\0';
  TEST_ASSERT_EQUAL_STRING(text, out);
  // Too small: fails, keeps room for '\0'.
  n = 0;
  TEST_ASSERT_FALSE(formatGenToken(a, mods, out, 5, n));
  TEST_ASSERT_TRUE(n < 5);
}

void test_mods_and_blanks() {
  const char* p = "x13w a";
  ArpStep a;
  char mods[3];  // two chars + '\0': the third is dropped
  TEST_ASSERT_TRUE(parseGenToken(p, a, mods, sizeof(mods)));
  TEST_ASSERT_EQUAL_STRING("13", mods);
  const char* q = "x^v^r^";
  char all[8];
  TEST_ASSERT_TRUE(parseGenToken(q, a, all, sizeof(all)));
  TEST_ASSERT_EQUAL_STRING("vr^", all);  // distinct, at their last position
  const char* end = "";
  const char* e = end;
  TEST_ASSERT_FALSE(parseGenToken(e, a, all, sizeof(all)));  // end of text: p stays
  TEST_ASSERT_EQUAL_PTR(end, e);
  TEST_ASSERT_EQUAL(' ', *p);  // trailing blank not skipped
  TEST_ASSERT_TRUE(isBlankChar(*p));
  TEST_ASSERT_TRUE(isBlankChar(','));
  TEST_ASSERT_FALSE(isBlankChar('x'));
}

void test_unknown_kind() {
  const char* text = "?zz x";
  const char* p = text;
  ArpStep a;
  char mods[4];
  TEST_ASSERT_FALSE(parseGenToken(p, a, mods, sizeof(mods)));
  TEST_ASSERT_EQUAL_PTR(text + 3, p);
}

void test_arp_long_token_keeps_mods() {
  ArpPattern pat;
  TEST_ASSERT_TRUE(parseArpPattern("x!!!!!!!!!!!!!!!!r x^v^ xrpr", pat));
  TEST_ASSERT_EQUAL(3, pat.len);
  TEST_ASSERT_EQUAL(ArpPitch::Repeat, pat.steps[0].pitch);
  TEST_ASSERT_EQUAL(1, pat.steps[1].oct);
  TEST_ASSERT_EQUAL(ArpPitch::Repeat, pat.steps[2].pitch);
}

void test_velocity() {
  ArpStep a;
  a.kind = ArpKind::Note;
  a.acc = ArpAcc::Ghost;
  TEST_ASSERT_EQUAL(60, genVelocity(a, 60, 120));
  a.acc = ArpAcc::Norm;
  TEST_ASSERT_EQUAL(90, genVelocity(a, 60, 120));
  a.acc = ArpAcc::Accent;
  TEST_ASSERT_EQUAL(120, genVelocity(a, 60, 120));
  a.velPct = 64;
  TEST_ASSERT_EQUAL(60, genVelocity(a, 60, 120));  // 120 * 64 / 128
  a.velPct = 200;
  TEST_ASSERT_EQUAL(127, genVelocity(a, 60, 120));  // clamped
}

void test_gate() {
  ArpStep a;
  a.kind = ArpKind::Note;
  a.len = ArpLen::Short;
  TEST_ASSERT_EQUAL(50, genGatePct(a, 50, 2));  // 25 x 2
  TEST_ASSERT_EQUAL(10, genGatePct(a, 6, 2));   // half under 5 -> 5
  a.len = ArpLen::Norm;
  TEST_ASSERT_EQUAL(100, genGatePct(a, 50, 2));
  a.len = ArpLen::Long;
  TEST_ASSERT_EQUAL(200, genGatePct(a, 50, 2));
  a.gatePct = 30;  // exact: ignores Long
  TEST_ASSERT_EQUAL(60, genGatePct(a, 50, 2));
  TEST_ASSERT_EQUAL(120, genGatePct(a, 100, 2));
}

void test_put_and_clear() {
  Step tr[2];
  tr[0].note = 60;
  tr[0].vel = 100;
  TEST_ASSERT_TRUE(putFx(tr[0], Fx::GAT, 40));
  TEST_ASSERT_TRUE(putFx(tr[0], Fx::GAT, 70));
  TEST_ASSERT_EQUAL(70, tr[0].find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(Fx::None, tr[0].fx[1].cmd);
  tr[1].note = 62;
  clearGenRange(tr, 0, 1);
  TEST_ASSERT_EQUAL(kNoteEmpty, tr[0].note);
  TEST_ASSERT_EQUAL(kVelDefault, tr[0].vel);
  TEST_ASSERT_NULL(tr[0].find(Fx::GAT));
  TEST_ASSERT_EQUAL(kNoteEmpty, tr[1].note);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_token_round_trip);
  RUN_TEST(test_mods_and_blanks);
  RUN_TEST(test_unknown_kind);
  RUN_TEST(test_arp_long_token_keeps_mods);
  RUN_TEST(test_velocity);
  RUN_TEST(test_gate);
  RUN_TEST(test_put_and_clear);
  return UNITY_END();
}
