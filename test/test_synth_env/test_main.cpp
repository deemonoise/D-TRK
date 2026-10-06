#include <unity.h>
#include "synth_env.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Number of next() calls until the stage differs from s.
static int samplesIn(Env& e, Env::Stage s, int limit = 100000) {
  int n = 0;
  while (e.stage() == s && n < limit) {
    e.next();
    ++n;
  }
  return n;
}

void test_idle_by_default() {
  Env e;
  TEST_ASSERT_TRUE(e.idle());
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.next());
}

void test_adsr_timing() {
  Env e;
  e.set(10, 10, 0.5f, 10);
  e.gate(true);
  TEST_ASSERT_EQUAL(static_cast<int>(Env::Stage::Attack), static_cast<int>(e.stage()));
  TEST_ASSERT_INT_WITHIN(1, 320, samplesIn(e, Env::Stage::Attack));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, e.level());
  TEST_ASSERT_INT_WITHIN(1, 320, samplesIn(e, Env::Stage::Decay));
  TEST_ASSERT_EQUAL(static_cast<int>(Env::Stage::Sustain), static_cast<int>(e.stage()));
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, e.next());
  e.gate(false);
  TEST_ASSERT_INT_WITHIN(1, 320, samplesIn(e, Env::Stage::Release));
  TEST_ASSERT_TRUE(e.idle());
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.level());
}

void test_attack_is_monotonic() {
  Env e;
  e.set(5, 0, 1.f, 0);
  e.gate(true);
  float prev = 0;
  for (int i = 0; i < 160; ++i) {
    const float v = e.next();
    TEST_ASSERT_TRUE(v >= prev);
    prev = v;
  }
}

void test_retrigger_from_release_no_jump() {
  Env e;
  e.set(10, 0, 1.f, 50);
  e.gate(true);
  for (int i = 0; i < 400; ++i) e.next();
  e.gate(false);
  for (int i = 0; i < 300; ++i) e.next();
  const float l = e.level();
  TEST_ASSERT_TRUE(l > 0.5f && l < 1.f);
  e.gate(true);
  TEST_ASSERT_EQUAL(static_cast<int>(Env::Stage::Attack), static_cast<int>(e.stage()));
  TEST_ASSERT_TRUE(e.next() >= l);
}

void test_zero_times_are_instant() {
  Env e;
  e.set(0, 0, 1.f, 0);
  e.gate(true);
  TEST_ASSERT_EQUAL_FLOAT(1.f, e.next());
  e.gate(false);
  TEST_ASSERT_TRUE(e.idle());
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.next());
}

void test_zero_attack_then_decay() {
  Env e;
  e.set(0, 10, 0.f, 0);
  e.gate(true);
  TEST_ASSERT_EQUAL_FLOAT(1.f, e.next());
  TEST_ASSERT_INT_WITHIN(2, 320, samplesIn(e, Env::Stage::Decay));
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.level());
  TEST_ASSERT_TRUE(e.idle());  // sustain at zero: the note is over, the voice frees
}

void test_kill() {
  Env e;
  e.set(0, 0, 1.f, 1000);
  e.gate(true);
  e.next();
  e.kill();
  TEST_ASSERT_TRUE(e.idle());
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.next());
}

void test_gate_off_when_idle_stays_idle() {
  Env e;
  e.set(10, 10, 0.5f, 10);
  e.gate(false);
  TEST_ASSERT_TRUE(e.idle());
}

// fade: Release over its own time, whatever the envelope's release is.
void test_fade_uses_its_time() {
  Env e;
  e.set(0, 0, 1.f, 1000);
  e.gate(true);
  e.next();
  e.fade(3);
  TEST_ASSERT_EQUAL(static_cast<int>(Env::Stage::Release), static_cast<int>(e.stage()));
  TEST_ASSERT_INT_WITHIN(1, 96, samplesIn(e, Env::Stage::Release));
  TEST_ASSERT_TRUE(e.idle());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_by_default);
  RUN_TEST(test_fade_uses_its_time);
  RUN_TEST(test_adsr_timing);
  RUN_TEST(test_attack_is_monotonic);
  RUN_TEST(test_retrigger_from_release_no_jump);
  RUN_TEST(test_zero_times_are_instant);
  RUN_TEST(test_zero_attack_then_decay);
  RUN_TEST(test_kill);
  RUN_TEST(test_gate_off_when_idle_stays_idle);
  return UNITY_END();
}
