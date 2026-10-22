#include <unity.h>
#include <vector>
#include "edit_ops.h"
#include "shift_gen.h"
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

void test_song_insert_into_empty() {
  static Project p;
  p.reset();
  TEST_ASSERT_TRUE(songInsert(p, 5, 3, 0x0101));  // `at` clamps to songLen
  TEST_ASSERT_EQUAL(1, p.songLen);
  TEST_ASSERT_EQUAL(3, p.song[0]);
  TEST_ASSERT_EQUAL_HEX16(0x0101, p.songMute[0]);
}

void test_song_insert_middle_shifts() {
  static Project p;
  p.reset();
  songInsert(p, 0, 1, 0x0001);
  songInsert(p, 1, 2, 0x0002);
  TEST_ASSERT_TRUE(songInsert(p, 1, 7, 0x8000));
  TEST_ASSERT_EQUAL(3, p.songLen);
  TEST_ASSERT_EQUAL(1, p.song[0]);
  TEST_ASSERT_EQUAL(7, p.song[1]);
  TEST_ASSERT_EQUAL_HEX16(0x8000, p.songMute[1]);
  TEST_ASSERT_EQUAL(2, p.song[2]);
  TEST_ASSERT_EQUAL_HEX16(0x0002, p.songMute[2]);
}

void test_song_insert_full_fails() {
  static Project p;
  p.reset();
  for (int i = 0; i < kSongMax; ++i) TEST_ASSERT_TRUE(songInsert(p, i, i % kPatterns, i));
  TEST_ASSERT_FALSE(songInsert(p, 0, 9, 0xFFFF));
  TEST_ASSERT_EQUAL(kSongMax, p.songLen);
  TEST_ASSERT_EQUAL(0, p.song[0]);
  TEST_ASSERT_EQUAL_HEX16(0, p.songMute[0]);
  TEST_ASSERT_EQUAL((kSongMax - 1) % kPatterns, p.song[kSongMax - 1]);
  TEST_ASSERT_EQUAL_HEX16(kSongMax - 1, p.songMute[kSongMax - 1]);
}

void test_song_delete_middle_and_past_end() {
  static Project p;
  p.reset();
  songInsert(p, 0, 1, 0x0001);
  songInsert(p, 1, 2, 0x0002);
  songInsert(p, 2, 3, 0x0004);
  songDelete(p, 1);
  TEST_ASSERT_EQUAL(2, p.songLen);
  TEST_ASSERT_EQUAL(1, p.song[0]);
  TEST_ASSERT_EQUAL(3, p.song[1]);
  TEST_ASSERT_EQUAL_HEX16(0x0004, p.songMute[1]);
  TEST_ASSERT_EQUAL(0, p.song[2]);  // the freed slot is zeroed
  TEST_ASSERT_EQUAL_HEX16(0, p.songMute[2]);
  songDelete(p, 2);  // past the end: nothing
  songDelete(p, -1);
  TEST_ASSERT_EQUAL(2, p.songLen);
  TEST_ASSERT_EQUAL(3, p.song[1]);
}

void test_song_insert_clamps_pattern() {
  static Project p;
  p.reset();
  TEST_ASSERT_TRUE(songInsert(p, 0, 99, 0));
  TEST_ASSERT_EQUAL(kPatterns - 1, p.song[0]);
}

void test_pattern_track_mask() {
  static Project p;
  p.reset();
  TEST_ASSERT_EQUAL_HEX16(0, patternTrackMask(p, 2));
  p.patterns[2].length = 16;
  p.patterns[2].steps[0][0].note = 60;
  p.patterns[2].steps[5][15].note = kNoteOff;
  p.patterns[2].steps[9][16].note = 60;  // past the length: ignored
  TEST_ASSERT_EQUAL_HEX16((1 << 0) | (1 << 5), patternTrackMask(p, 2));
  TEST_ASSERT_EQUAL_HEX16(0, patternTrackMask(p, 3));
}

static GenCfg capCfg(uint8_t mode) {
  GenCfg c;
  c.mode = mode;
  c.len = 5;
  c.flip = 100;  // ignored by the capture
  c.density = 50;
  c.seed = 42;
  return c;
}

// The loop with Flip 0 from st: note (or kNoteEmpty when GATE says no) per shift.
static std::vector<int> capExpect(const GenCfg& c, GenState st, int n, bool gate) {
  std::vector<int> v;
  for (int i = 0; i < n; ++i) {
    genShift(st, c, 3, 0);
    v.push_back(!gate || genGate(st, c) ? genNote(st, c, 0, ScaleType::Minor) : kNoteEmpty);
  }
  return v;
}

void test_capture_gen_pitch() {
  const GenCfg c = capCfg(static_cast<uint8_t>(GenMode::Pitch));
  GenState st;
  genReset(st, c, 3);
  const GenState before = st;
  for (int i = 0; i < 16; i += 2) {
    pat.steps[3][i].note = 60;
    pat.steps[3][i].vel = 77;
    pat.steps[3][i].fx[0] = {Fx::PRB, 30};
  }
  pat.steps[3][3].note = kNoteOff;
  captureGen(pat, 3, c, st, 0, 0, ScaleType::Minor);
  TEST_ASSERT_EQUAL(before.reg, st.reg);
  const auto want = capExpect(c, st, 8, false);
  for (int i = 0; i < 16; ++i) {
    if (i % 2 == 0) {
      TEST_ASSERT_EQUAL(want[i / 2], pat.steps[3][i].note);
      TEST_ASSERT_EQUAL(77, pat.steps[3][i].vel);
      TEST_ASSERT_TRUE(pat.steps[3][i].fx[0].cmd == Fx::PRB);
    } else {
      TEST_ASSERT_EQUAL(i == 3 ? kNoteOff : kNoteEmpty, pat.steps[3][i].note);
    }
  }
  TEST_ASSERT_EQUAL(kNoteEmpty, pat.steps[2][0].note);  // other tracks untouched
}

void test_capture_gen_gate_from_step_and_track_len() {
  const GenCfg c = capCfg(static_cast<uint8_t>(GenMode::Gate));
  GenState st;
  genReset(st, c, 3);
  pat.trackLen[3] = 12;
  pat.steps[3][1].note = kNoteOff;
  pat.steps[3][7].note = 50;
  pat.steps[3][7].fx[1] = {Fx::FLT, 9};
  pat.steps[3][13].note = 61;  // past the track length: untouched
  captureGen(pat, 3, c, st, 9, 0, ScaleType::Minor);
  const auto want = capExpect(c, st, 12, true);
  int gated = 0;
  for (int k = 0; k < 12; ++k) {
    const int i = (9 + k) % 12;
    if (i == 1) {
      TEST_ASSERT_EQUAL(kNoteOff, pat.steps[3][i].note);
      continue;
    }
    TEST_ASSERT_EQUAL(want[k], pat.steps[3][i].note);
    gated += want[k] != kNoteEmpty;
  }
  TEST_ASSERT_TRUE(gated > 0 && gated < 11);
  TEST_ASSERT_TRUE(pat.steps[3][7].fx[1].cmd == Fx::FLT);
  TEST_ASSERT_EQUAL(61, pat.steps[3][13].note);
}

static SongFollow jumpTo(int t) {
  SongFollow f;
  f.act = static_cast<uint8_t>(FollowAct::Jump);
  f.target = static_cast<uint8_t>(t);
  return f;
}

// Follow moves with its slot; JUMP targets keep pointing at the same slot; others keep their bytes.
void test_song_insert_moves_follow_and_jump_targets() {
  static Project p;
  p.reset();
  songInsert(p, 0, 1, 0);
  songInsert(p, 1, 2, 0);
  songInsert(p, 2, 3, 0);
  p.songFol[0] = jumpTo(2);  // set directly: an insert would remap a target passed with it
  p.songFol[2] = jumpTo(0);
  p.songFol[1].chance = 30;  // NEXT, target 0: not remapped
  TEST_ASSERT_TRUE(songInsert(p, 1, 7, 0, jumpTo(2)));  // the new slot's own target is remapped too
  TEST_ASSERT_EQUAL(4, p.songLen);
  TEST_ASSERT_EQUAL(3, p.songFol[0].target);  // slot 2 moved to 3
  TEST_ASSERT_EQUAL(3, p.songFol[1].target);
  TEST_ASSERT_EQUAL(static_cast<int>(FollowAct::Next), p.songFol[2].act);
  TEST_ASSERT_EQUAL(30, p.songFol[2].chance);
  TEST_ASSERT_EQUAL(0, p.songFol[2].target);
  TEST_ASSERT_EQUAL(0, p.songFol[3].target);  // before `at`: unchanged
}

void test_song_delete_moves_follow_and_jump_targets() {
  static Project p;
  p.reset();
  for (int i = 0; i < 4; ++i) songInsert(p, i, static_cast<uint8_t>(i + 1), 0);
  p.songFol[0] = jumpTo(3);
  p.songFol[1] = jumpTo(1);
  p.songFol[2] = jumpTo(0);
  p.songFol[3] = jumpTo(2);
  songDelete(p, 1);  // 0 1 2 3 -> 0 2 3; slot 1 targeted itself: gone with it
  TEST_ASSERT_EQUAL(3, p.songLen);
  TEST_ASSERT_EQUAL(2, p.songFol[0].target);  // 3 -> 2
  TEST_ASSERT_EQUAL(0, p.songFol[1].target);  // the old slot 2, its target 0 unchanged
  TEST_ASSERT_EQUAL(1, p.songFol[2].target);  // 2 -> 1
  TEST_ASSERT_EQUAL(static_cast<int>(FollowAct::Next), p.songFol[3].act);  // the freed slot is zeroed
  TEST_ASSERT_EQUAL(100, p.songFol[3].chance);
  songDelete(p, 2);  // the last slot: targets on it go to the new last
  TEST_ASSERT_EQUAL(1, p.songFol[0].target);
  songDelete(p, 1);  // slot 0 targets slot 1, the last: the new last (0)
  TEST_ASSERT_EQUAL(0, p.songFol[0].target);
  TEST_ASSERT_EQUAL(1, p.songLen);
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
  RUN_TEST(test_song_insert_into_empty);
  RUN_TEST(test_song_insert_middle_shifts);
  RUN_TEST(test_song_insert_full_fails);
  RUN_TEST(test_song_delete_middle_and_past_end);
  RUN_TEST(test_song_insert_clamps_pattern);
  RUN_TEST(test_song_insert_moves_follow_and_jump_targets);
  RUN_TEST(test_song_delete_moves_follow_and_jump_targets);
  RUN_TEST(test_pattern_track_mask);
  RUN_TEST(test_capture_gen_pitch);
  RUN_TEST(test_capture_gen_gate_from_step_and_track_len);
  return UNITY_END();
}
