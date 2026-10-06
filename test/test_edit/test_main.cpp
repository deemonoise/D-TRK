#include <unity.h>
#include "edit_ops.h"
#include "undo.h"

using namespace mt;

// Large objects live in static storage, not on the stack.
static Pattern pat;
static Clipboard cb;

void setUp() {
  pat.clear();
  pat.length = 16;
  cb = Clipboard();
}
void tearDown() {}

static void fillMarked(Pattern& p) {
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < kMaxSteps; ++s) {
      p.steps[t][s].note = static_cast<uint8_t>((t * 13 + s) % 128);
      p.steps[t][s].vel = static_cast<uint8_t>(t + 1);
    }
}

void test_make_sel_normalizes() {
  Sel s = makeSel(5, 10, 2, 3);
  TEST_ASSERT_EQUAL(2, s.t0);
  TEST_ASSERT_EQUAL(5, s.t1);
  TEST_ASSERT_EQUAL(3, s.s0);
  TEST_ASSERT_EQUAL(10, s.s1);
}

void test_make_sel_clamps() {
  Sel s = makeSel(99, 200, -1, -5);
  TEST_ASSERT_EQUAL(0, s.t0);
  TEST_ASSERT_EQUAL(kTracks - 1, s.t1);
  TEST_ASSERT_EQUAL(0, s.s0);
  TEST_ASSERT_EQUAL(127, s.s1);
}

void test_copy_paste_clipped() {
  static Pattern src;
  src.clear();
  fillMarked(src);
  copySel(src, makeSel(0, 0, 1, 2), cb);  // 2 tracks x 3 steps
  TEST_ASSERT_EQUAL(2, cb.tracks);
  TEST_ASSERT_EQUAL(3, cb.steps);

  pasteAt(pat, cb, 6, 14);
  // Pasted: tracks 6-7, steps 14-15.
  for (int dt = 0; dt < 2; ++dt)
    for (int ds = 0; ds < 2; ++ds) {
      const Step& got = pat.steps[6 + dt][14 + ds];
      TEST_ASSERT_EQUAL(src.steps[dt][ds].note, got.note);
      TEST_ASSERT_EQUAL(src.steps[dt][ds].vel, got.vel);
    }
  // Step 16 (beyond length) untouched.
  TEST_ASSERT_TRUE(pat.steps[6][16].isEmpty());
  TEST_ASSERT_TRUE(pat.steps[7][16].isEmpty());
  // Everything else untouched.
  int nonEmpty = 0;
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < kMaxSteps; ++s)
      if (!pat.steps[t][s].isEmpty()) ++nonEmpty;
  TEST_ASSERT_EQUAL(4, nonEmpty);
}

void test_paste_clipped_at_track_16() {
  static Pattern src;
  src.clear();
  fillMarked(src);
  copySel(src, makeSel(0, 0, 2, 0), cb);  // 3 tracks x 1 step
  TEST_ASSERT_EQUAL(3, cb.tracks);

  pasteAt(pat, cb, 14, 0);
  // Pasted: tracks 14-15; the third clipboard track falls off the end.
  TEST_ASSERT_EQUAL(src.steps[0][0].note, pat.steps[14][0].note);
  TEST_ASSERT_EQUAL(src.steps[1][0].note, pat.steps[15][0].note);
  int nonEmpty = 0;
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < kMaxSteps; ++s)
      if (!pat.steps[t][s].isEmpty()) ++nonEmpty;
  TEST_ASSERT_EQUAL(2, nonEmpty);
}

void test_paste_empty_clipboard_noop() {
  pasteAt(pat, cb, 0, 0);
  TEST_ASSERT_TRUE(pat.isEmpty());
}

void test_clear_sel_only_selection() {
  fillMarked(pat);
  clearSel(pat, makeSel(1, 2, 2, 4));
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < kMaxSteps; ++s) {
      bool inSel = t >= 1 && t <= 2 && s >= 2 && s <= 4;
      TEST_ASSERT_EQUAL(inSel, pat.steps[t][s].isEmpty());
    }
}

// A selection may straddle the two LED halves (tracks 0-7 / 8-15).
void test_selection_spans_halves() {
  Sel a = makeSel(5, 0, 10, 3);
  Sel b = makeSel(10, 3, 5, 0);
  TEST_ASSERT_EQUAL(5, a.t0);
  TEST_ASSERT_EQUAL(10, a.t1);
  TEST_ASSERT_EQUAL(5, b.t0);
  TEST_ASSERT_EQUAL(10, b.t1);

  fillMarked(pat);
  clearSel(pat, a);
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < kMaxSteps; ++s) {
      bool inSel = t >= 5 && t <= 10 && s <= 3;
      TEST_ASSERT_EQUAL(inSel, pat.steps[t][s].isEmpty());
    }
}

void test_transpose_semitones() {
  pat.steps[0][0].note = 60;
  pat.steps[0][1].note = kNoteOff;
  pat.steps[0][2].note = 120;
  pat.steps[0][4].note = 10;  // outside selection
  transposeSel(pat, makeSel(0, 0, 0, 3), 12, false, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(72, pat.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteOff, pat.steps[0][1].note);
  TEST_ASSERT_EQUAL(127, pat.steps[0][2].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, pat.steps[0][3].note);
  TEST_ASSERT_EQUAL(10, pat.steps[0][4].note);
}

void test_transpose_down_clamps_to_zero() {
  pat.steps[0][0].note = 5;
  transposeSel(pat, makeSel(0, 0, 0, 0), -12, false, 0, ScaleType::Chromatic);
  TEST_ASSERT_EQUAL(0, pat.steps[0][0].note);
}

void test_transpose_degrees() {
  pat.steps[0][0].note = 60;
  pat.steps[0][1].note = kNoteOff;
  transposeSel(pat, makeSel(0, 0, 0, 1), 1, true, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(62, pat.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteOff, pat.steps[0][1].note);
}

void test_undo_lifo() {
  Undo::Entry* store = new Undo::Entry[Undo::kDepth];
  Undo u(store);
  static Pattern a, out;
  uint8_t idx = 0;
  TEST_ASSERT_FALSE(u.pop(idx, out));
  for (int i = 0; i < 3; ++i) {
    a.clear();
    a.steps[0][0].note = static_cast<uint8_t>(40 + i);
    u.push(static_cast<uint8_t>(i), a);
  }
  TEST_ASSERT_EQUAL(3, u.size());
  for (int i = 2; i >= 0; --i) {
    TEST_ASSERT_TRUE(u.pop(idx, out));
    TEST_ASSERT_EQUAL(i, idx);
    TEST_ASSERT_EQUAL(40 + i, out.steps[0][0].note);
  }
  TEST_ASSERT_FALSE(u.pop(idx, out));
  delete[] store;
}

void test_undo_overflow() {
  Undo::Entry* store = new Undo::Entry[Undo::kDepth];
  Undo u(store);
  static Pattern a, out;
  constexpr int kPushes = Undo::kDepth + 8;  // wraps: the first 8 are lost
  for (int i = 0; i < kPushes; ++i) {
    a.clear();
    a.steps[0][0].note = static_cast<uint8_t>(i);
    u.push(static_cast<uint8_t>(i % kPatterns), a);
  }
  TEST_ASSERT_EQUAL(Undo::kDepth, u.size());
  uint8_t idx = 0;
  for (int i = kPushes - 1; i >= kPushes - Undo::kDepth; --i) {
    TEST_ASSERT_TRUE(u.pop(idx, out));
    TEST_ASSERT_EQUAL(i, out.steps[0][0].note);
    TEST_ASSERT_EQUAL(i % kPatterns, idx);
  }
  TEST_ASSERT_FALSE(u.pop(idx, out));  // (kDepth + 1)th pop
  TEST_ASSERT_EQUAL(0, u.size());
  delete[] store;
}

void test_undo_drop() {
  Undo::Entry* store = new Undo::Entry[Undo::kDepth];
  Undo u(store);
  static Pattern a, out;
  TEST_ASSERT_FALSE(u.drop());
  for (int i = 0; i < 2; ++i) {
    a.clear();
    a.steps[0][0].note = static_cast<uint8_t>(50 + i);
    u.push(static_cast<uint8_t>(i), a);
  }
  TEST_ASSERT_TRUE(u.drop());
  TEST_ASSERT_EQUAL(1, u.size());
  uint8_t idx = 9;
  TEST_ASSERT_TRUE(u.pop(idx, out));
  TEST_ASSERT_EQUAL(0, idx);
  TEST_ASSERT_EQUAL(50, out.steps[0][0].note);
  TEST_ASSERT_FALSE(u.drop());
  delete[] store;
}

void test_undo_drop_after_wrap() {
  Undo::Entry* store = new Undo::Entry[Undo::kDepth];
  Undo u(store);
  static Pattern a, out;
  constexpr int kPushes = Undo::kDepth + 8;  // wraps: 8..kPushes-1 kept
  for (int i = 0; i < kPushes; ++i) {
    a.clear();
    a.steps[0][0].note = static_cast<uint8_t>(i);
    u.push(static_cast<uint8_t>(i % kPatterns), a);
  }
  TEST_ASSERT_TRUE(u.drop());  // forgets the newest
  TEST_ASSERT_EQUAL(Undo::kDepth - 1, u.size());
  a.clear();
  a.steps[0][0].note = 100;
  u.push(1, a);  // reuses the dropped slot
  uint8_t idx = 0;
  TEST_ASSERT_TRUE(u.pop(idx, out));
  TEST_ASSERT_EQUAL(100, out.steps[0][0].note);
  for (int i = kPushes - 2; i >= kPushes - Undo::kDepth; --i) {
    TEST_ASSERT_TRUE(u.pop(idx, out));
    TEST_ASSERT_EQUAL(i, out.steps[0][0].note);
  }
  TEST_ASSERT_FALSE(u.pop(idx, out));
  delete[] store;
}

void test_transpose_skips_drum_tracks() {
  pat.steps[0][0].note = 60;
  pat.steps[1][0].note = 100;
  pat.steps[1][0].vel = 0xA5;
  bool drum[kTracks] = {false, true};
  transposeSel(pat, makeSel(0, 0, 1, 0), 12, false, 0, ScaleType::Chromatic, drum);
  TEST_ASSERT_EQUAL(72, pat.steps[0][0].note);
  TEST_ASSERT_EQUAL(100, pat.steps[1][0].note);
  TEST_ASSERT_EQUAL_HEX8(0xA5, pat.steps[1][0].vel);
}

void test_copy_paste_drum_step_byte_exact() {
  static Pattern src;
  src.clear();
  src.steps[2][3].note = 100;
  src.steps[2][3].vel = 0xA5;
  src.steps[2][3].fx[0] = {Fx::ACC, 0x81};
  copySel(src, makeSel(2, 3, 2, 3), cb);
  pasteAt(pat, cb, 4, 5);
  TEST_ASSERT_EQUAL_MEMORY(&src.steps[2][3], &pat.steps[4][5], sizeof(Step));
}

void test_chain_insert_delete_move_all_fields() {
  static Project p;
  p.reset();
  p.chainLen = 2;
  p.chain[0] = 1; p.chainTr[0] = 3; p.chainRep[0] = 2; p.chainScene[0] = 1;
  p.chain[1] = 4; p.chainTr[1] = -2; p.chainRep[1] = 5; p.chainScene[1] = 2;
  TEST_ASSERT_TRUE(chainInsert(p, 1, 9));
  TEST_ASSERT_EQUAL(3, p.chainLen);
  TEST_ASSERT_EQUAL(9, p.chain[1]);
  TEST_ASSERT_EQUAL(0, p.chainTr[1]);
  TEST_ASSERT_EQUAL(1, p.chainRep[1]);
  TEST_ASSERT_EQUAL(0, p.chainScene[1]);
  TEST_ASSERT_EQUAL(4, p.chain[2]);
  TEST_ASSERT_EQUAL(-2, p.chainTr[2]);
  TEST_ASSERT_EQUAL(5, p.chainRep[2]);
  TEST_ASSERT_EQUAL(2, p.chainScene[2]);
  chainDelete(p, 0);
  TEST_ASSERT_EQUAL(2, p.chainLen);
  TEST_ASSERT_EQUAL(9, p.chain[0]);
  TEST_ASSERT_EQUAL(-2, p.chainTr[1]);
  TEST_ASSERT_EQUAL(1, p.chainRep[2]);  // the freed row is reset
  TEST_ASSERT_EQUAL(0, p.chain[2]);
  chainDelete(p, 5);  // past the end: nothing
  TEST_ASSERT_EQUAL(2, p.chainLen);
  p.chainLen = kChainMax;
  TEST_ASSERT_FALSE(chainInsert(p, 0, 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_make_sel_normalizes);
  RUN_TEST(test_make_sel_clamps);
  RUN_TEST(test_copy_paste_clipped);
  RUN_TEST(test_paste_clipped_at_track_16);
  RUN_TEST(test_paste_empty_clipboard_noop);
  RUN_TEST(test_clear_sel_only_selection);
  RUN_TEST(test_selection_spans_halves);
  RUN_TEST(test_transpose_semitones);
  RUN_TEST(test_transpose_down_clamps_to_zero);
  RUN_TEST(test_transpose_degrees);
  RUN_TEST(test_undo_lifo);
  RUN_TEST(test_undo_overflow);
  RUN_TEST(test_undo_drop);
  RUN_TEST(test_undo_drop_after_wrap);
  RUN_TEST(test_transpose_skips_drum_tracks);
  RUN_TEST(test_copy_paste_drum_step_byte_exact);
  RUN_TEST(test_chain_insert_delete_move_all_fields);
  return UNITY_END();
}
