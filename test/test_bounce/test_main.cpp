// One-shot bounce: BounceNotes' events, its stop rules, HOLD in blocks, the names.
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "bounce.h"
#include "render.h"
#include "sample_set.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Project* mkProj() {  // static storage: a Project is large
  static Project p;
  p.reset();
  return &p;
}

static void checkEv(const BounceEv& e, uint8_t a, uint8_t b, uint8_t c, uint8_t len) {
  TEST_ASSERT_EQUAL(kPreviewTrack, e.track);
  TEST_ASSERT_EQUAL(len, e.len);
  TEST_ASSERT_EQUAL_HEX8(a, e.b[0]);
  TEST_ASSERT_EQUAL(b, e.b[1]);
  if (len > 2) TEST_ASSERT_EQUAL(c, e.b[2]);
}

// Block 0: insert, program, note-on; note-off on holdBlocks; the rest empty.
void test_events_block0_and_note_off() {
  BounceSpec s;
  s.instr = 5;
  s.note = 64;
  s.vel = 100;
  s.insTrack = 3;
  s.holdBlocks = 11;
  BounceNotes q(s);
  BounceEv e[BounceNotes::kMaxEvents];
  TEST_ASSERT_EQUAL(3, q.events(0, e));
  checkEv(e[0], 0xF7, 3, 0, 2);
  checkEv(e[1], 0xC0, 5, 0, 2);
  checkEv(e[2], 0x90, 64, 100, 3);
  for (uint32_t b = 1; b < 11; ++b) TEST_ASSERT_EQUAL(0, q.events(b, e));
  TEST_ASSERT_EQUAL(1, q.events(11, e));
  checkEv(e[0], 0x80, 64, 0, 3);
  TEST_ASSERT_EQUAL(0, q.events(12, e));
  TEST_ASSERT_EQUAL(0, q.events(s.maxBlocks + 5, e));
}

// The dry default: the insert byte is kTracks (none).
void test_events_default_dry() {
  const BounceSpec s;
  BounceNotes q(s);
  BounceEv e[BounceNotes::kMaxEvents];
  TEST_ASSERT_EQUAL(3, q.events(0, e));
  checkEv(e[0], 0xF7, kTracks, 0, 2);
  checkEv(e[2], 0x90, 60, 127, 3);
}

// HOLD 0: the note-off follows the note-on in block 0.
void test_hold_zero_same_block() {
  BounceSpec s;
  s.holdBlocks = 0;
  BounceNotes q(s);
  BounceEv e[BounceNotes::kMaxEvents];
  TEST_ASSERT_EQUAL(4, q.events(0, e));
  checkEv(e[2], 0x90, 60, 127, 3);
  checkEv(e[3], 0x80, 60, 0, 3);
}

// Quiet stops the render only once the note-off block has been rendered (block > holdBlocks).
void test_quiet_stops_after_note_off() {
  BounceSpec s;
  s.holdBlocks = 20;
  BounceNotes q(s);
  TEST_ASSERT_FALSE(q.done(0, 0));
  TEST_ASSERT_FALSE(q.done(10, 100));  // quiet before the note-off: ignored
  TEST_ASSERT_FALSE(q.done(20, 100));  // the note-off block not rendered yet
  TEST_ASSERT_TRUE(q.done(21, s.quietStop));
  TEST_ASSERT_FALSE(q.done(30, s.quietStop - 1));  // below quietStop: on
  TEST_ASSERT_TRUE(q.done(31, s.quietStop));
  TEST_ASSERT_FALSE(q.hitMax());
}

// The quiet count resets on a loud block: the render goes on.
void test_quiet_reset() {
  BounceSpec s;
  s.holdBlocks = 5;
  BounceNotes q(s);
  TEST_ASSERT_FALSE(q.done(40, s.quietStop - 1));
  TEST_ASSERT_FALSE(q.done(41, 0));  // loud again
  TEST_ASSERT_FALSE(q.done(41 + s.quietStop - 1, s.quietStop - 1));
  TEST_ASSERT_TRUE(q.done(41 + s.quietStop, s.quietStop));
}

// maxBlocks ends it and says so.
void test_stops_at_max() {
  BounceSpec s;
  s.holdBlocks = 3;
  s.maxBlocks = 13;
  BounceNotes q(s);
  TEST_ASSERT_FALSE(q.done(12, 0));
  TEST_ASSERT_FALSE(q.hitMax());
  TEST_ASSERT_TRUE(q.done(13, 0));
  TEST_ASSERT_TRUE(q.hitMax());
}

void test_defaults_match_times() {
  BounceSpec s;
  TEST_ASSERT_EQUAL_UINT32(250, kBounceBlocksPerSec);
  TEST_ASSERT_EQUAL_UINT32(125, s.holdBlocks);   // 0.5 s
  TEST_ASSERT_EQUAL_UINT32(2500, s.maxBlocks);   // 10 s
  TEST_ASSERT_EQUAL_UINT32(13, s.quietStop);     // ~50 ms
  TEST_ASSERT_TRUE(s.normalize);
  TEST_ASSERT_EQUAL_UINT32(125, bounceHoldBlocks(0.5f));
  TEST_ASSERT_EQUAL_UINT32(bounceHoldBlocks(0.05f), bounceHoldBlocks(0.f));
  TEST_ASSERT_EQUAL_UINT32(13, bounceHoldBlocks(0.05f));
  TEST_ASSERT_EQUAL_UINT32(bounceHoldBlocks(5.f), bounceHoldBlocks(60.f));
  TEST_ASSERT_EQUAL_UINT32(1250, bounceHoldBlocks(5.f));
}

void test_tap_scales() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32767.f * 0.25f * 1.27f, kPrevTapScale);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 31653.f, kNormPeakHot);
  TEST_ASSERT_TRUE(kNormPeakHot > kNormPeak);
}

void test_names() {
  Project& p = *mkProj();
  char n[kSampleNameMax + 1];
  nextBounceName(p, "BD", n);
  TEST_ASSERT_EQUAL_STRING("BD_1", n);
  TEST_ASSERT_EQUAL(0, projSampleSet(p, "bd_1", 1, 100));  // taken, any case
  nextBounceName(p, "BD", n);
  TEST_ASSERT_EQUAL_STRING("BD_2", n);
  nextBounceName(p, "", n);
  TEST_ASSERT_EQUAL_STRING("SHOT_1", n);
  nextBounceName(p, nullptr, n);
  TEST_ASSERT_EQUAL_STRING("SHOT_1", n);
  nextBounceName(p, "E.PNO 2", n);  // not a sample name's characters
  TEST_ASSERT_EQUAL_STRING("E_PNO_2_1", n);
  nextBounceName(p, "ABCDEFGHIJKLMNOPQRS", n);  // longer than a name: cut
  TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMN_1", n);
  TEST_ASSERT_TRUE(strlen(n) <= static_cast<size_t>(kSampleNameMax));
  for (int i = 1; i <= 9; ++i) {
    char t[kSampleNameMax + 1];
    snprintf(t, sizeof t, "ABCDEFGHIJKLMN_%d", i);
    TEST_ASSERT_TRUE(projSampleSet(p, t, static_cast<uint32_t>(i + 10), 100) >= 0);
  }
  nextBounceName(p, "ABCDEFGHIJKLMNOPQRS", n);
  TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLM_10", n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_events_block0_and_note_off);
  RUN_TEST(test_events_default_dry);
  RUN_TEST(test_hold_zero_same_block);
  RUN_TEST(test_quiet_stops_after_note_off);
  RUN_TEST(test_quiet_reset);
  RUN_TEST(test_stops_at_max);
  RUN_TEST(test_defaults_match_times);
  RUN_TEST(test_tap_scales);
  RUN_TEST(test_names);
  return UNITY_END();
}
