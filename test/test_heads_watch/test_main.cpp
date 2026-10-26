#include <unity.h>
#include "heads_watch.h"
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static SynthModel* mk(int n, const uint32_t* crcs) {  // static storage: a SynthModel is large
  static SynthModel m;
  m = SynthModel();
  m.sampleCount = static_cast<uint8_t>(n);
  for (int i = 0; i < n; ++i) m.samples[i].crc = crcs[i];
  return &m;
}

void test_sig_set() {
  const uint32_t a[] = {0x11111111u, 0x22222222u, 0x33333333u};
  const uint32_t b[] = {0x33333333u, 0x11111111u, 0x22222222u};
  const uint32_t c[] = {0x11111111u, 0x22222222u, 0x33333334u};
  const uint32_t sa = sampleSetSig(*mk(3, a));
  TEST_ASSERT_EQUAL_HEX32(sa, sampleSetSig(*mk(3, b)));  // order ignored
  TEST_ASSERT_NOT_EQUAL(sa, sampleSetSig(*mk(3, c)));
  TEST_ASSERT_NOT_EQUAL(sa, sampleSetSig(*mk(2, a)));
  TEST_ASSERT_NOT_EQUAL(sampleSetSig(*mk(0, a)), sampleSetSig(*mk(1, a)));
  // entries past the count do not count
  SynthModel* m = mk(2, a);
  m->samples[2].crc = 0xDEADBEEFu;
  TEST_ASSERT_EQUAL_HEX32(sampleSetSig(*mk(2, a)), sampleSetSig(*m));
}

void test_unchanged_never_due() {
  HeadsWatch w;
  w.done(5);
  for (uint32_t t = 0; t < 5000; t += 100) TEST_ASSERT_FALSE(w.due(5, true, t));
  TEST_ASSERT_FALSE(w.pending());
}

void test_due_after_settle() {
  HeadsWatch w;
  w.done(5);
  TEST_ASSERT_FALSE(w.due(6, true, 1000));
  TEST_ASSERT_TRUE(w.pending());
  TEST_ASSERT_FALSE(w.due(6, true, 1000 + HeadsWatch::kSettleMs - 1));
  TEST_ASSERT_TRUE(w.due(6, true, 1000 + HeadsWatch::kSettleMs));
  w.done(6);
  TEST_ASSERT_FALSE(w.pending());
  TEST_ASSERT_FALSE(w.due(6, true, 9000));
}

void test_change_restarts_settle() {
  HeadsWatch w;
  w.done(5);
  w.due(6, true, 1000);
  TEST_ASSERT_FALSE(w.due(7, true, 1400));  // another piece: settle from here
  TEST_ASSERT_FALSE(w.due(7, true, 1600));
  TEST_ASSERT_TRUE(w.due(7, true, 1900));
}

void test_busy_defers() {
  HeadsWatch w;
  w.done(5);
  w.due(6, false, 1000);
  TEST_ASSERT_FALSE(w.due(6, false, 3000));  // playing / a job: wait
  TEST_ASSERT_FALSE(w.due(6, true, 3100));   // idle from now
  TEST_ASSERT_FALSE(w.due(6, true, 3500));
  TEST_ASSERT_TRUE(w.due(6, true, 3600));
  TEST_ASSERT_FALSE(w.due(6, false, 3700));  // busy again: idle starts over
  TEST_ASSERT_FALSE(w.due(6, true, 3800));
  TEST_ASSERT_TRUE(w.due(6, true, 4300));
}

void test_back_to_done_clears() {
  HeadsWatch w;
  w.done(5);
  w.due(6, true, 1000);
  TEST_ASSERT_FALSE(w.due(5, true, 1200));  // the set went back (undo): nothing to do
  TEST_ASSERT_FALSE(w.pending());
  TEST_ASSERT_FALSE(w.due(5, true, 5000));
}

void test_done_elsewhere() {  // a BankSync wrote the heads for the new set meanwhile
  HeadsWatch w;
  w.done(5);
  w.due(6, false, 1000);
  w.done(6);
  TEST_ASSERT_FALSE(w.due(6, true, 5000));
}

void test_wraps() {
  HeadsWatch w;
  w.done(5);
  w.due(6, true, 0xFFFFFF00u);
  TEST_ASSERT_FALSE(w.due(6, true, 0xFFFFFFFFu));
  TEST_ASSERT_TRUE(w.due(6, true, 0x00000200u));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sig_set);
  RUN_TEST(test_unchanged_never_due);
  RUN_TEST(test_due_after_settle);
  RUN_TEST(test_change_restarts_settle);
  RUN_TEST(test_busy_defers);
  RUN_TEST(test_back_to_done_clears);
  RUN_TEST(test_done_elsewhere);
  RUN_TEST(test_wraps);
  return UNITY_END();
}
